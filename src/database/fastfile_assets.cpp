#include "fastfile_assets.h"
#include "fastfile_reader.h"
#include <platform/bo1_bytecode_io.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <new>
#include <utility>

namespace bo1::fastfile::detail
{
struct AssetNode
{
    RawFile raw{};
    StringTable table{};
    LocalizeEntry localize{};
    std::unique_ptr<StringTableCell[]> cells;
    std::unique_ptr<int16_t[]> indices;
};

struct AssetStorage
{
    std::vector<std::unique_ptr<AssetNode>> nodes;
    std::vector<std::unique_ptr<char[]>> buffers;
};

// Disk cursors and native storage are intentionally separate. Block 0 is the
// engine's temporary stream and rewinds after each root. Block 4 is persistent.
// The stream follows db_load.cpp; alignment and alias slots consume block space
// but no bytes from the decompressed file.
class AssetBuilder
{
public:
    AssetBuilder(Reader& reader, Inspection& inspection, const MetadataState& metadata,
        AssetPrefix& output, const AssetLoadLimits& limits)
        : reader(reader), inspection(inspection), metadata(metadata), output(output), limits(limits),
          block4(metadata.block4Cursor), declared(XFileHeaderSize + uint64_t(inspection.file.size))
    {
        output.storage = std::make_shared<AssetStorage>();
    }

    bool Load()
    {
        if (!Charge(sizeof(AssetStorage)))
            return false;
        for (size_t index = 0; index < inspection.assets.size(); ++index)
        {
            const AssetEntry& disk = inspection.assets[index];
            NativeAsset native{};
            native.type = disk.type;
            if (disk.serializedTag)
            {
                if (disk.type != 36 && disk.type != 37 && disk.type != 23)
                {
                    output.stop = AssetStop::UnsupportedType;
                    break;
                }
                if (disk.serializedTag < UINT32_MAX - 1)
                {
                    const NativeAsset* target = Alias(disk.serializedTag);
                    if (!target || target->type != disk.type)
                    {
                        output.stop = AssetStop::UnsupportedRootReference;
                        break;
                    }
                    native = *target;
                    native.alias = true;
                }
                else
                {
                    uint32_t inserted = 0;
                    if (disk.serializedTag == UINT32_MAX - 1)
                    {
                        if (!AdvanceBlock(4, 4))
                            return false;
                        inserted = static_cast<uint32_t>(block4 - 4);
                    }
                    if (!Charge(sizeof(AssetNode) + sizeof(std::unique_ptr<AssetNode>)))
                        return false;
                    auto node = std::make_unique<AssetNode>();
                    if (disk.type == 36)
                    {
                        if (!Raw(*node)) return false;
                        native.rawfile = &node->raw;
                    }
                    else if (disk.type == 37)
                    {
                        if (!Table(*node)) return false;
                        native.stringTable = &node->table;
                    }
                    else
                    {
                        if (!Localized(*node)) return false;
                        native.localize = &node->localize;
                    }
                    output.storage->nodes.push_back(std::move(node));
                    if (disk.serializedTag == UINT32_MAX - 1)
                    {
                        if (!Charge(sizeof(InsertedAlias))) return false;
                        insertedAliases.emplace_back(inserted, index);
                    }
                }
            }
            if (!Charge(sizeof(NativeAsset)))
                return false;
            output.entries.push_back(native);
        }
        output.nextAsset = output.entries.size();
        output.decodedAssetBytes = reader.consumed - inspection.metadataBytes;
        output.nativeBytes = allocated;
        if (output.stop == AssetStop::Complete && reader.consumed != declared)
            return Fail("Decoded bytes remain after all supported assets were reconstructed.");
        return true;
    }

private:
    bool Fail(const char* message) { return reader.Fail(InspectionResult::InvalidFormat, message); }

    bool Charge(uint64_t bytes)
    {
        if (allocated > limits.maxNativeBytes || bytes > limits.maxNativeBytes - allocated)
            return reader.Fail(InspectionResult::LimitExceeded, "Native assets exceed the retained byte limit.");
        allocated += bytes;
        return true;
    }

    bool Read(void* target, size_t bytes)
    {
        if (reader.consumed > declared || bytes > declared - reader.consumed)
            return Fail("Asset body exceeds the declared package size.");
        if (!reader.Read(target, bytes))
            return Fail("Asset body is truncated.");
        return true;
    }

    bool AdvanceBlock(uint64_t bytes, uint64_t alignment = 1)
    {
        const uint64_t start = (block4 + alignment - 1) & ~(alignment - 1);
        if (start > inspection.file.blockSize[4] || bytes > inspection.file.blockSize[4] - start)
            return Fail("Asset children exceed stream block 4.");
        block4 = start + bytes;
        return true;
    }

    bool Header(unsigned char* bytes, size_t size)
    {
        // PopStreamPos restores block 0 to the position preceding this root.
        if (size > inspection.file.blockSize[0])
            return Fail("Asset header exceeds temporary stream block 0.");
        return Read(bytes, size);
    }

    const NativeAsset* Alias(uint32_t tag) const
    {
        if (((tag - 1) >> 29) != 4)
            return nullptr;
        const uint32_t offset = (tag - 1) & 0x1fffffff;
        const uint64_t firstSlot = metadata.directoryStart + 4;
        if (offset >= firstSlot && (offset - firstSlot) % 8 == 0)
        {
            const uint64_t index = (offset - firstSlot) / 8;
            if (index < output.entries.size())
                return &output.entries[static_cast<size_t>(index)];
        }
        const auto found = std::lower_bound(insertedAliases.begin(), insertedAliases.end(), offset,
            [](const InsertedAlias& item, uint32_t key) { return item.first < key; });
        return found != insertedAliases.end() && found->first == offset
            ? &output.entries[found->second] : nullptr;
    }

    bool StoreString(const std::string& value, const char*& target)
    {
        if (!Charge(value.size() + 1 + sizeof(std::unique_ptr<char[]>)))
            return false;
        auto buffer = std::make_unique<char[]>(value.size() + 1);
        std::memcpy(buffer.get(), value.c_str(), value.size() + 1);
        target = buffer.get();
        output.storage->buffers.push_back(std::move(buffer));
        return true;
    }

    bool String(uint32_t tag, const char*& target)
    {
        target = nullptr;
        if (!tag)
            return true;
        if (tag == UINT32_MAX)
        {
            const uint32_t start = static_cast<uint32_t>(block4);
            std::string value;
            for (;;)
            {
                unsigned char byte;
                if (!AdvanceBlock(1) || !Read(&byte, 1))
                    return false;
                // Temporary string growth is also charged conservatively.
                if (!Charge(2))
                    return false;
                if (!byte)
                    break;
                value.push_back(static_cast<char>(byte));
            }
            if (!StoreString(value, target) || !Charge(sizeof(ResolvedString)))
                return false;
            strings.emplace_back(start, target);
            return true;
        }
        const uint32_t offset = (tag - 1) & 0x1fffffff;
        if (((tag - 1) >> 29) == 4)
        {
            const auto found = std::lower_bound(strings.begin(), strings.end(), offset,
                [](const ResolvedString& item, uint32_t key) { return item.first < key; });
            if (found != strings.end() && found->first == offset)
            {
                target = found->second;
                return true;
            }
            const auto script = std::lower_bound(metadata.strings.begin(), metadata.strings.end(), offset,
                [](const auto& item, uint32_t key) { return item.first < key; });
            if (script != metadata.strings.end() && script->first == offset)
            {
                if (scriptCopies.empty())
                {
                    if (!Charge(inspection.scriptStrings.size() * sizeof(const char*))) return false;
                    scriptCopies.resize(inspection.scriptStrings.size());
                }
                auto& cached = scriptCopies[script->second];
                if (!cached && !StoreString(inspection.scriptStrings[script->second].value, cached))
                    return false;
                target = cached;
                return true;
            }
        }
        return reader.Fail(InspectionResult::UnsupportedReference,
            "Asset string reference does not identify a previously decoded string start.");
    }

    bool Raw(AssetNode& node)
    {
        unsigned char header[12];
        if (!Header(header, sizeof(header))) return false;
        node.raw.len = bytecode::Load<int32_t>(header + 4);
        const uint32_t bufferTag = bytecode::Load<uint32_t>(header + 8);
        if (node.raw.len < 0 || (bufferTag && node.raw.len == INT32_MAX))
            return Fail("Invalid rawfile buffer length.");
        if (!String(bytecode::Load<uint32_t>(header), node.raw.name)) return false;
        if (bufferTag)
        {
            const size_t bytes = static_cast<size_t>(node.raw.len) + 1;
            if (!AdvanceBlock(bytes) || !Charge(bytes + sizeof(std::unique_ptr<char[]>))) return false;
            auto buffer = std::make_unique<char[]>(bytes);
            if (!Read(buffer.get(), bytes)) return false;
            // The disk length excludes one byte. Preserve that byte even if it
            // is nonzero, matching Load_ConstCharArray(len+1); no strlen here.
            node.raw.buffer = buffer.get();
            output.storage->buffers.push_back(std::move(buffer));
        }
        return true;
    }

    bool Table(AssetNode& node)
    {
        unsigned char header[20];
        if (!Header(header, sizeof(header))) return false;
        auto& table = node.table;
        table.columnCount = bytecode::Load<int32_t>(header + 4);
        table.rowCount = bytecode::Load<int32_t>(header + 8);
        if (table.columnCount < 0 || table.rowCount < 0)
            return Fail("Negative string table dimensions.");
        const uint64_t count = uint64_t(table.columnCount) * uint64_t(table.rowCount);
        if (count > INT32_MAX / 8)
            return Fail("String table dimensions overflow the disk array length.");
        if (!String(bytecode::Load<uint32_t>(header), table.name)) return false;
        if (bytecode::Load<uint32_t>(header + 12))
        {
            if (!AdvanceBlock(count * 8, 4)
                || !Charge(count * (sizeof(StringTableCell) + sizeof(uint32_t)))) return false;
            node.cells = std::make_unique<StringTableCell[]>(static_cast<size_t>(count));
            table.values = node.cells.get();
            std::vector<uint32_t> tags(static_cast<size_t>(count));
            for (size_t i = 0; i < count; ++i)
            {
                unsigned char cell[8];
                if (!Read(cell, 8)) return false;
                tags[i] = bytecode::Load<uint32_t>(cell);
                table.values[i].hash = bytecode::Load<int32_t>(cell + 4);
            }
            for (size_t i = 0; i < count; ++i)
                if (!String(tags[i], table.values[i].string)) return false;
        }
        if (bytecode::Load<uint32_t>(header + 16))
        {
            if (count && !table.values)
                return Fail("String table has indices but no cell array.");
            if (!AdvanceBlock(count * 2, 2) || !Charge(count * sizeof(int16_t))) return false;
            node.indices = std::make_unique<int16_t[]>(static_cast<size_t>(count));
            table.cellIndex = node.indices.get();
            for (size_t i = 0; i < count; ++i)
            {
                unsigned char index[2];
                if (!Read(index, 2)) return false;
                const auto cell = bytecode::Load<int16_t>(index);
                if (cell < 0 || uint64_t(cell) >= count)
                    return Fail("String table index is outside the cell array.");
                table.cellIndex[i] = cell;
            }
        }
        return true;
    }

    bool Localized(AssetNode& node)
    {
        unsigned char header[8];
        return Header(header, sizeof(header))
            && String(bytecode::Load<uint32_t>(header), node.localize.value)
            && String(bytecode::Load<uint32_t>(header + 4), node.localize.name);
    }

    using ResolvedString = std::pair<uint32_t, const char*>;
    using InsertedAlias = std::pair<uint32_t, size_t>;
    Reader& reader;
    Inspection& inspection;
    const MetadataState& metadata;
    AssetPrefix& output;
    const AssetLoadLimits& limits;
    uint64_t block4;
    uint64_t declared;
    uint64_t allocated = 0;
    std::vector<ResolvedString> strings;
    std::vector<InsertedAlias> insertedAliases;
    std::vector<const char*> scriptCopies;
};
}

namespace bo1::fastfile
{
InspectionResult LoadAssetPrefix(const char* path, AssetPrefix& output, std::string& error,
    const AssetLoadLimits& limits)
{
    output = {};
    AssetPrefix candidate;
    struct Context { AssetPrefix& output; const AssetLoadLimits& limits; } context{candidate, limits};
    const auto load = [](detail::Reader& reader, Inspection& inspection,
        const detail::MetadataState& state, void* opaque) -> bool {
        auto& context = *static_cast<Context*>(opaque);
        detail::AssetBuilder builder(reader, inspection, state, context.output, context.limits);
        return builder.Load();
    };
    const auto result = detail::ReadFile(path, candidate.inspection, error, limits.inspection, load, &context);
    if (result == InspectionResult::Ok)
        output = std::move(candidate);
    return result;
}
}
