#include "fastfile_inspection.h"
#include <platform/bo1_bytecode_io.h>
#include "fastfile_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace bo1::fastfile
{
namespace detail
{

bool ReadMetadata(Reader& reader, Inspection& out, const InspectionLimits& limits, MetadataState& state)
{
    unsigned char bytes[XFileHeaderSize];
    if (!reader.Read(bytes, XFileHeaderSize))
        return reader.Fail(InspectionResult::InvalidFormat, "XFile header is truncated.");
    ParseXFileHeader(bytes, XFileHeaderSize, out.file);
    const uint64_t declared = XFileHeaderSize + uint64_t(out.file.size);
    if (declared > limits.maxExpandedBytes)
        return reader.Fail(InspectionResult::LimitExceeded, "Declared payload size exceeds the decompressed byte limit.");
    if (out.file.size < AssetListHeaderSize || !reader.Read(bytes, AssetListHeaderSize)
        || ParseAssetListHeader(bytes, AssetListHeaderSize, out.list) != ParseResult::Ok)
        return reader.Fail(InspectionResult::InvalidFormat, "Invalid or truncated asset list header.");

    const uint64_t stringCount = out.list.scriptStringsTag ? out.list.scriptStringCount : 0;
    const uint64_t assetCount = out.list.assetsTag ? out.list.assetCount : 0;
    if (stringCount * 4 + assetCount * 8 > out.file.size - AssetListHeaderSize
        || stringCount * 4 + assetCount * 8 > out.file.blockSize[4])
        return reader.Fail(InspectionResult::InvalidFormat, "Asset table counts exceed the package or stream block.");

    using InlineString = std::pair<uint32_t, uint32_t>;
    uint64_t retained = stringCount * (sizeof(ScriptString) + sizeof(InlineString)) + assetCount * sizeof(AssetEntry);
    if (retained > limits.maxMetadataBytes)
        return reader.Fail(InspectionResult::LimitExceeded, "Metadata exceeds the retained byte limit.");
    out.scriptStrings.resize(static_cast<size_t>(stringCount));
    auto& initializedStrings = state.strings;
    initializedStrings.reserve(static_cast<size_t>(stringCount));
    for (auto& string : out.scriptStrings)
    {
        if (!reader.Read(bytes, 4))
            return reader.Fail(InspectionResult::InvalidFormat, "Script string table is truncated.");
        string.serializedTag = bytecode::Load<uint32_t>(bytes);
    }

    uint64_t blockCursor = stringCount * 4;
    for (size_t index = 0; index < out.scriptStrings.size(); ++index)
    {
        auto& string = out.scriptStrings[index];
        const auto tag = string.serializedTag;
        if (!tag)
            continue;
        string.present = true;
        if (tag == UINT32_MAX)
        {
            const uint64_t start = blockCursor;
            for (;;)
            {
                if (blockCursor >= out.file.blockSize[4] || reader.consumed >= declared)
                    return reader.Fail(InspectionResult::InvalidFormat, "Script string exceeds package or stream block.");
                unsigned char character;
                if (!reader.Read(&character, 1))
                    return reader.Fail(InspectionResult::InvalidFormat, "Unterminated script string.");
                ++blockCursor;
                if (++retained > limits.maxMetadataBytes)
                    return reader.Fail(InspectionResult::LimitExceeded, "Script strings exceed the retained byte limit.");
                if (!character)
                    break;
                string.value.push_back(static_cast<char>(character));
            }
            initializedStrings.emplace_back(static_cast<uint32_t>(start), static_cast<uint32_t>(index));
        }
        else
        {
            const uint32_t block = (tag - 1) >> 29;
            const uint32_t offset = (tag - 1) & 0x1fffffff;
            const auto found = std::lower_bound(initializedStrings.begin(), initializedStrings.end(), offset,
                [](const InlineString& entry, uint32_t key) { return entry.first < key; });
            if (block != 4 || found == initializedStrings.end() || found->first != offset)
                return reader.Fail(InspectionResult::UnsupportedReference, "Unsupported script string reference: expected a previously decoded block-4 string.");
            const auto& value = out.scriptStrings[found->second].value;
            if (value.size() + 1 > limits.maxMetadataBytes - retained)
                return reader.Fail(InspectionResult::LimitExceeded, "Referenced strings exceed the retained byte limit.");
            retained += value.size() + 1;
            string.value = value;
        }
    }

    // Alignment belongs to the block layout, not to the sequential file bytes.
    const uint64_t assetStart = (blockCursor + 3) & ~uint64_t(3);
    if (out.list.assetsTag && (assetStart > out.file.blockSize[4]
        || assetCount * 8 > out.file.blockSize[4] - assetStart
        || reader.consumed > declared || assetCount * 8 > declared - reader.consumed))
        return reader.Fail(InspectionResult::InvalidFormat, "Asset directory exceeds the package or stream block.");
    out.assets.resize(static_cast<size_t>(assetCount));
    for (auto& asset : out.assets)
    {
        if (!reader.Read(bytes, 8))
            return reader.Fail(InspectionResult::InvalidFormat, "Asset directory is truncated.");
        asset.type = bytecode::Load<uint32_t>(bytes);
        asset.serializedTag = bytecode::Load<uint32_t>(bytes + 4);
        if (asset.type >= 43)
            return reader.Fail(InspectionResult::InvalidFormat, "Asset directory contains an invalid type.");
    }
    state.directoryStart = assetStart;
    state.block4Cursor = out.list.assetsTag ? assetStart + assetCount * 8 : blockCursor;
    out.metadataBytes = reader.consumed;
    return true;
}
}

InspectionResult detail::ReadFile(const char* path, Inspection& output, std::string& error, const InspectionLimits& limits, AfterMetadata callback, void* context)
{
    output = {};
    error.clear();
    try
    {
        std::unique_ptr<FILE, decltype(&std::fclose)> input(path ? std::fopen(path, "rb") : nullptr, &std::fclose);
        if (!input)
        {
            error = "Could not open fastfile.";
            return InspectionResult::IoError;
        }
        if (limits.maxFileBytes < FileHeaderSize)
        {
            error = "Fastfile exceeds the input byte limit.";
            return InspectionResult::LimitExceeded;
        }
        Inspection candidate{};
        unsigned char envelope[FileHeaderSize];
        const auto headerResult = ParseFileHeader(envelope, std::fread(envelope, 1, sizeof(envelope), input.get()), candidate.header);
        if (headerResult != ParseResult::Ok)
        {
            error = "Invalid fastfile header (version " + std::to_string(candidate.header.version) + ").";
            return InspectionResult::InvalidFormat;
        }
        if (candidate.header.authenticated)
        {
            error = "Authenticated fastfiles are unsupported by this loader.";
            return InspectionResult::InvalidFormat;
        }
        Reader reader(input.get(), limits, error);
        MetadataState state;
        if (!ReadMetadata(reader, candidate, limits, state)
            || (callback && !callback(reader, candidate, state, context)) || !reader.Finish())
            return reader.result;
        candidate.expandedBytes = reader.produced;
        if (candidate.expandedBytes != XFileHeaderSize + uint64_t(candidate.file.size))
        {
            error = "Declared payload size does not match the decompressed stream.";
            return InspectionResult::InvalidFormat;
        }
        output = std::move(candidate);
        return InspectionResult::Ok;
    }
    catch (const std::bad_alloc&)
    {
        error = "Could not allocate inspection metadata.";
        return InspectionResult::OutOfMemory;
    }
}

InspectionResult InspectFile(const char* path, Inspection& output, std::string& error, const InspectionLimits& limits)
{
    return detail::ReadFile(path, output, error, limits);
}

const char* AssetTypeName(uint32_t type) noexcept
{
    // Keep the IDs from database/db_assetnames.cpp; assets remain serialized.
    static const char* const names[] = {
        "xmodelpieces", "physpreset", "physconstraints", "destructibledef", "xanim", "xmodel",
        "material", "techset", "image", "sound", "sound_patch", "col_map_sp", "col_map_mp",
        "com_map", "game_map_sp", "game_map_mp", "map_ents", "gfx_map", "lightdef", "ui_map",
        "font", "menufile", "menu", "localize", "weapon", "weapondef", "weaponvariant",
        "snddriverglobals", "fx", "impactfx", "aitype", "mptype", "mpbody", "mphead",
        "character", "xmodelalias", "rawfile", "stringtable", "packindex", "xGlobals", "ddl", "glasses", "emblemset"
    };
    static_assert(sizeof(names) / sizeof(names[0]) == 43);
    return type < 43 ? names[type] : "unknown";
}
}
