#include "fastfile_assets.h"
#include "fastfile_reader.h"
#include "db_temp_stream_cursor.h"
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
    std::unique_ptr<MaterialTechniqueSet> techniqueSet;
    std::unique_ptr<StringTableCell[]> cells;
    std::unique_ptr<int16_t[]> indices;
};

struct AssetStorage
{
    std::vector<std::unique_ptr<AssetNode>> nodes;
    std::vector<std::unique_ptr<char[]>> buffers;
    std::vector<std::shared_ptr<void>> children;
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
          block4(metadata.block4Cursor), temp(inspection.file.blockSize[0]),
          declared(XFileHeaderSize + uint64_t(inspection.file.size))
    {
        output.storage = std::make_shared<AssetStorage>();
    }

    bool Load()
    {
        if (!Charge(sizeof(AssetStorage)))
            return false;
        for (size_t index = 0; index < inspection.assets.size(); ++index)
        {
            if (!temp.ResetRoot())
                return Fail("Unbalanced temporary stream stack at asset boundary.");
            const AssetEntry& disk = inspection.assets[index];
            NativeAsset native{};
            native.type = disk.type;
            if (disk.serializedTag)
            {
                if (disk.type == 5 && disk.serializedTag >= UINT32_MAX - 1)
                {
                    // XModel is the first unsupported root in Kino. Reading
                    // its bounded TEMP header yields useful diagnostics but
                    // cannot advance past its nested surfaces/materials yet.
                    // Do not publish this as an engine-owned or usable asset.
                    unsigned char modelDisk[XModelDiskHeaderSize];
                    XModelHeader model{};
                    if (!Header(modelDisk, sizeof(modelDisk)))
                        return false;
                    if (!DecodeXModelHeader(modelDisk, sizeof(modelDisk), model))
                        return Fail("XModel header contains invalid bone, LOD or collision counts.");
                    XModelSkeletonPreview skeleton{};
                    if (!ModelSkeleton(model, skeleton)
                        || !ModelSurfaceHeaders(model, skeleton))
                        return false;
                    output.pendingXModel = model;
                    output.pendingXModelSkeleton = std::move(skeleton);
                    output.stop = AssetStop::UnsupportedModelBody;
                    break;
                }
                if (disk.type != 36 && disk.type != 37 && disk.type != 23 && disk.type != 7)
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
                    else if (disk.type == 23)
                    {
                        if (!Localized(*node)) return false;
                        native.localize = &node->localize;
                    }
                    else
                    {
                        if (!Techset(*node)) return false;
                        native.techniqueSet = node->techniqueSet.get();
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
        // TEMP positions are restored after nested headers and reset between
        // roots. Nested model/material loaders will use Push/Pop for children.
        if (!temp.Advance(size))
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

    // Decode only the ordered, fixed-size skeletal children of the XModel.
    // Stream position 4 is persistent; references are not dereferenced until
    // the per-type alias resolver exists. The following XSurface body is
    // deliberately NOT consumed, and no model object is published.
    bool ModelSkeleton(const XModelHeader& model, XModelSkeletonPreview& preview)
    {
        const char* name = nullptr;
        if (!String(model.nameTag, name)) return false;
        if (name)
        {
            const size_t length = std::strlen(name);
            if (!Charge(length + 1)) return false;
            preview.name.assign(name, length);
        }
        const size_t bones = model.numBones;
        const size_t children = bones - model.numRootBones;
        auto array = [&](uint32_t tag, size_t bytes, uint64_t alignment,
            std::vector<uint8_t>& dest, uint32_t unresolvedFlag) -> bool
        {
            if (tag != UINT32_MAX)
            {
                if (tag) preview.unresolvedFields |= unresolvedFlag;
                return true;
            }
            if (!AdvanceBlock(bytes, alignment) || !Charge(bytes)) return false;
            dest.resize(bytes);
            if (bytes && !Read(dest.data(), bytes)) return false;
            preview.inlineArrayBytes += bytes;
            return true;
        };
        return array(model.boneNamesTag, bones * 2, 2, preview.boneNameBytes, 1u)
            && array(model.parentListTag, children, 1, preview.parentList, 2u)
            && array(model.quaternionsTag, children * 8, 2, preview.quaternions, 4u)
            && array(model.translationsTag, children * 16, 4, preview.translations, 8u)
            && array(model.classificationsTag, bones, 1, preview.classifications, 16u)
            && array(model.baseMatricesTag, bones * 32, 4, preview.baseMatrices, 32u);
    }

    // Load_XSurfaceArray first reads every 68-byte surface header from block
    // 4, then follows nested vertex/triangle/collision payloads. Parse only
    // that first contiguous array here. Never advance past its child bodies.
    bool ModelSurfaceHeaders(const XModelHeader& model, XModelSkeletonPreview& preview)
    {
        if (!model.surfacesTag) return true;
        const size_t bytes = size_t(model.numSurfaces) * XSurfaceDiskHeaderSize;
        if (!AdvanceBlock(bytes, 4)
            || !Charge(bytes + size_t(model.numSurfaces) * sizeof(XSurfaceHeader)))
            return false;
        std::vector<uint8_t> raw(bytes);
        if (bytes && !Read(raw.data(), bytes)) return false;
        preview.surfaceHeaders.reserve(model.numSurfaces);
        for (size_t i = 0; i < model.numSurfaces; ++i)
        {
            XSurfaceHeader header;
            if (!DecodeXSurfaceHeader(raw.data() + i * XSurfaceDiskHeaderSize,
                    XSurfaceDiskHeaderSize, header))
                return Fail("XSurface header has invalid skin vertex group counts.");
            preview.declaredVertices += header.vertCount;
            preview.declaredTriangles += header.triCount;
            preview.surfaceHeaders.push_back(header);
        }
        preview.surfaceHeaderBytes = bytes;
        if (!Charge(size_t(model.numSurfaces) * sizeof(XSurfaceBodyPreview)))
            return false;
        preview.surfaceBodies.reserve(model.numSurfaces);
        for (const auto& surface : preview.surfaceHeaders)
        {
            XSurfaceBodyPreview body;
            if (!SurfaceBody(surface, body)) return false;
            preview.decodedGeometryBytes += body.inlineBodyBytes;
            preview.surfaceBodies.push_back(std::move(body));
        }
        return true;
    }

    // Every XSurface's children follow its header-array block in the exact
    // Load_XSurface order. Referenced/shared tags are left unresolved; they
    // consume no FILE bytes. Only serial -1 means an inline geometry array.
    bool SurfaceArray(uint32_t tag, size_t bytes, uint64_t alignment,
        std::vector<uint8_t>& outputBytes, XSurfaceBodyPreview& preview,
        uint32_t unresolvedFlag)
    {
        if (tag != UINT32_MAX)
        {
            if (tag) preview.unresolvedFields |= unresolvedFlag;
            return true;
        }
        if (!AdvanceBlock(bytes, alignment) || !Charge(bytes)) return false;
        outputBytes.resize(bytes);
        if (bytes && !Read(outputBytes.data(), bytes)) return false;
        preview.inlineBodyBytes += bytes;
        return true;
    }

    bool InlineCollisionTree(XSurfaceBodyPreview& preview)
    {
        unsigned char disk[40];
        if (!AdvanceBlock(sizeof(disk), 4) || !Charge(sizeof(disk)
            + sizeof(XSurfaceCollisionPreview)) || !Read(disk, sizeof(disk)))
            return false;
        preview.inlineBodyBytes += sizeof(disk);
        XSurfaceCollisionPreview tree{};
        tree.nodeCount = bytecode::Load<uint32_t>(disk + 24);
        const uint32_t nodeTag = bytecode::Load<uint32_t>(disk + 28);
        tree.leafCount = bytecode::Load<uint32_t>(disk + 32);
        const uint32_t leafTag = bytecode::Load<uint32_t>(disk + 36);
        // Load_XSurfaceCollisionTree treats any nonzero tag as inline,
        // unlike the sentinel-specific XSurface array children.
        const auto max = uint64_t(limits.maxNativeBytes);
        if ((nodeTag && uint64_t(tree.nodeCount) * 16 > max)
            || (leafTag && uint64_t(tree.leafCount) * 2 > max))
            return reader.Fail(InspectionResult::LimitExceeded,
                "XSurface collision tree exceeds geometry memory limit.");
        if (!SurfaceArray(nodeTag ? UINT32_MAX : 0, size_t(tree.nodeCount) * 16,
                16, tree.nodes, preview, 0)
            || !SurfaceArray(leafTag ? UINT32_MAX : 0, size_t(tree.leafCount) * 2,
                2, tree.leaves, preview, 0))
            return false;
        preview.inlineCollisionTrees.push_back(std::move(tree));
        return true;
    }

    bool SurfaceBody(const XSurfaceHeader& surface, XSurfaceBodyPreview& preview)
    {
        const auto sizes = CalculateXSurfaceInlineBytes(surface);
        if (!SurfaceArray(surface.vertsBlendTag, size_t(sizes.blendIndices), 2,
                preview.blendIndices, preview, 1)
            || !SurfaceArray(surface.tensionDataTag, size_t(sizes.tensionData), 4,
                preview.tensionData, preview, 2)
            || !SurfaceArray(surface.packedVerticesTag, size_t(sizes.packedVertices), 16,
                preview.packedVertices, preview, 4)
            || !SurfaceArray(surface.rigidVertListTag, size_t(sizes.rigidLists), 4,
                preview.rigidLists, preview, 8))
            return false;
        // XRigidVertListArray reads all 12-byte list records first and
        // subsequently loads each referenced collision-tree body.
        for (size_t i = 0; i + 12 <= preview.rigidLists.size(); i += 12)
        {
            const uint32_t tag = bytecode::Load<uint32_t>(preview.rigidLists.data() + i + 8);
            if (tag == UINT32_MAX)
            {
                if (!InlineCollisionTree(preview)) return false;
            }
            else if (tag)
                preview.unresolvedFields |= 32;
        }
        return SurfaceArray(surface.triIndicesTag, size_t(sizes.triangleIndices),
            16, preview.triangleIndices, preview, 16);
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

    // Child tags are direct offsets, unlike indirect root aliases. Keep their
    // Win32 locations and types separate from native addresses and sizes.
    enum class ChildKind { Technique, Declaration, VertexShader, PixelShader, Literal };
    struct ChildReference { uint32_t offset; ChildKind kind; void* pointer; };

    bool Remember(uint32_t offset, ChildKind kind, void* pointer)
    {
        if (!Charge(sizeof(ChildReference))) return false;
        children.push_back({offset, kind, pointer});
        return true;
    }

    template<class T> bool Resolve(uint32_t tag, ChildKind kind, T*& pointer)
    {
        pointer = nullptr;
        if (!tag) return true;
        const uint32_t offset = (tag - 1) & 0x1fffffff;
        const auto found = std::lower_bound(children.begin(), children.end(), offset,
            [](const ChildReference& item, uint32_t key) { return item.offset < key; });
        if (((tag - 1) >> 29) != 4 || found == children.end()
            || found->offset != offset || found->kind != kind)
            return reader.Fail(InspectionResult::UnsupportedReference,
                "Technique child reference does not identify a previously decoded object of the same type.");
        pointer = static_cast<T*>(found->pointer);
        return true;
    }

    template<class T> bool Allocate(size_t count, T*& pointer)
    {
        if (count > SIZE_MAX / sizeof(T)
            || !Charge(uint64_t(count) * sizeof(T) + sizeof(std::shared_ptr<void>))) return false;
        std::shared_ptr<T> owner(new T[count]{}, std::default_delete<T[]>());
        pointer = owner.get();
        output.storage->children.push_back(std::move(owner));
        return true;
    }

    bool Declaration(uint32_t tag, MaterialVertexDeclaration*& declaration)
    {
        if (tag != UINT32_MAX) return Resolve(tag, ChildKind::Declaration, declaration);
        unsigned char disk[108];
        if (!AdvanceBlock(sizeof(disk), 4) || !Read(disk, sizeof(disk))) return false;
        const auto offset = static_cast<uint32_t>(block4 - sizeof(disk));
        if (disk[0] > 16) return Fail("Vertex declaration exceeds its stream routing array.");
        if (!Allocate(1, declaration) || !Remember(offset, ChildKind::Declaration, declaration)) return false;
        declaration->streamCount = disk[0];
        declaration->hasOptionalSource = disk[1] != 0;
        // Serialized D3D pointers and the loaded flag cannot describe GPU
        // resources in this process. Keep routing, initialize handles to null.
        declaration->isLoaded = false;
        for (size_t i = 0; i < 16; ++i)
            declaration->routing.data[i] = {disk[4 + 2 * i], disk[5 + 2 * i]};
        return true;
    }

    template<class T> bool Shader(uint32_t tag, ChildKind kind, T*& shader)
    {
        if (tag != UINT32_MAX) return Resolve(tag, kind, shader);
        unsigned char disk[16];
        if (!AdvanceBlock(sizeof(disk), 4) || !Read(disk, sizeof(disk))) return false;
        const auto offset = static_cast<uint32_t>(block4 - sizeof(disk));
        if (!Allocate(1, shader) || !Remember(offset, kind, shader)
            || !String(bytecode::Load<uint32_t>(disk), shader->name)) return false;
        auto& load = shader->prog.loadDef;
        load.programSize = bytecode::Load<uint16_t>(disk + 12);
        // In the original loader, any nonzero program tag is inline data.
        if (bytecode::Load<uint32_t>(disk + 8))
        {
            if (!AdvanceBlock(uint64_t(load.programSize) * 4, 4)
                || !Allocate(load.programSize, load.program)) return false;
            for (size_t i = 0; i < load.programSize; ++i)
            {
                unsigned char word[4];
                if (!Read(word, 4)) return false;
                load.program[i] = bytecode::Load<uint32_t>(word);
            }
        }
        return true;
    }

    bool Literal(uint32_t tag, const float*& target)
    {
        float* literal = nullptr;
        if (tag != UINT32_MAX)
        {
            if (!Resolve(tag, ChildKind::Literal, literal)) return false;
        }
        else
        {
            if (!AdvanceBlock(16, 4) || !Allocate(4, literal)
                || !Remember(static_cast<uint32_t>(block4 - 16), ChildKind::Literal, literal)) return false;
            unsigned char disk[16];
            if (!Read(disk, sizeof(disk))) return false;
            for (size_t i = 0; i < 4; ++i) literal[i] = bytecode::Load<float>(disk + i * 4);
        }
        target = literal;
        return true;
    }

    bool Pass(const unsigned char* disk, MaterialPass& pass)
    {
        pass.perPrimArgCount = disk[12];
        pass.perObjArgCount = disk[13];
        pass.stableArgCount = disk[14];
        pass.customSamplerFlags = disk[15];
        if (!Declaration(bytecode::Load<uint32_t>(disk), pass.vertexDecl)
            || !Shader(bytecode::Load<uint32_t>(disk + 4), ChildKind::VertexShader, pass.vertexShader)
            || !Shader(bytecode::Load<uint32_t>(disk + 8), ChildKind::PixelShader, pass.pixelShader)) return false;
        if (bytecode::Load<uint32_t>(disk + 16))
        {
            const size_t count = size_t(pass.perPrimArgCount) + pass.perObjArgCount + pass.stableArgCount;
            if (!AdvanceBlock(count * 8, 4) || !Allocate(count, pass.args)
                || !Charge(count * sizeof(uint32_t))) return false;
            std::vector<uint32_t> tags(count);
            for (size_t i = 0; i < count; ++i)
            {
                unsigned char argument[8];
                if (!Read(argument, 8)) return false;
                auto& native = pass.args[i];
                native.type = bytecode::Load<uint16_t>(argument);
                native.dest = bytecode::Load<uint16_t>(argument + 2);
                tags[i] = bytecode::Load<uint32_t>(argument + 4);
                if (native.type == 3 || native.type == 5)
                    native.u.codeConst = {bytecode::Load<uint16_t>(argument + 4), argument[6], argument[7]};
                else if (native.type == 4)
                    native.u.codeSampler = tags[i];
                else if (native.type != 1 && native.type != 7)
                    native.u.nameHash = tags[i];
            }
            for (size_t i = 0; i < count; ++i)
                if ((pass.args[i].type == 1 || pass.args[i].type == 7)
                    && !Literal(tags[i], pass.args[i].u.literalConst)) return false;
        }
        return true;
    }

    bool Technique(uint32_t tag, MaterialTechnique*& technique)
    {
        if (tag != UINT32_MAX) return Resolve(tag, ChildKind::Technique, technique);
        unsigned char disk[8];
        if (!AdvanceBlock(sizeof(disk), 4) || !Read(disk, sizeof(disk))) return false;
        const auto offset = static_cast<uint32_t>(block4 - sizeof(disk));
        const auto count = bytecode::Load<uint16_t>(disk + 6);
        const size_t nativeSize = sizeof(MaterialTechnique)
            + (count ? size_t(count - 1) * sizeof(MaterialPass) : 0);
        if (!AdvanceBlock(uint64_t(count) * 20) || !Charge(nativeSize
            + sizeof(std::shared_ptr<void>) + size_t(count) * 20)) return false;
        // Construct the header (including its first pass) and each additional
        // trailing pass explicitly. All these engine types are trivial.
        std::shared_ptr<void> owner(::operator new(nativeSize), [](void* p) { ::operator delete(p); });
        technique = new (owner.get()) MaterialTechnique{};
        technique->flags = bytecode::Load<uint16_t>(disk + 4);
        technique->passCount = count;
        for (size_t i = 1; i < count; ++i)
            new (static_cast<unsigned char*>(owner.get()) + offsetof(MaterialTechnique, passArray)
                + i * sizeof(MaterialPass)) MaterialPass{};
        output.storage->children.push_back(std::move(owner));
        if (!Remember(offset, ChildKind::Technique, technique)) return false;
        std::vector<unsigned char> passes(size_t(count) * 20);
        if (!Read(passes.data(), passes.size())) return false;
        for (size_t i = 0; i < count; ++i)
        {
            auto* pass = const_cast<MaterialPass*>(TechniquePassAt(*technique, i));
            if (!Pass(passes.data() + i * 20, *pass)) return false;
        }
        return String(bytecode::Load<uint32_t>(disk), technique->name);
    }

    bool Techset(AssetNode& node)
    {
        unsigned char disk[528];
        if (!Header(disk, sizeof(disk)) || !Charge(sizeof(MaterialTechniqueSet))) return false;
        node.techniqueSet = std::make_unique<MaterialTechniqueSet>();
        auto& set = *node.techniqueSet;
        set.worldVertFormat = disk[4];
        set.unused[0] = disk[5];
        set.techsetFlags = bytecode::Load<uint16_t>(disk + 6);
        if (!String(bytecode::Load<uint32_t>(disk), set.name)) return false;
        for (size_t i = 0; i < 130; ++i)
            if (!Technique(bytecode::Load<uint32_t>(disk + 8 + i * 4), set.techniques[i])) return false;
        return true;
    }

    using ResolvedString = std::pair<uint32_t, const char*>;
    using InsertedAlias = std::pair<uint32_t, size_t>;
    Reader& reader;
    Inspection& inspection;
    const MetadataState& metadata;
    AssetPrefix& output;
    const AssetLoadLimits& limits;
    uint64_t block4;
    TempStreamCursor temp;
    uint64_t declared;
    uint64_t allocated = 0;
    std::vector<ResolvedString> strings;
    std::vector<InsertedAlias> insertedAliases;
    std::vector<const char*> scriptCopies;
    std::vector<ChildReference> children;
};
}

namespace bo1::fastfile
{
const MaterialPass* TechniquePassAt(const MaterialTechnique& technique, size_t index)
{
    if (index >= technique.passCount) return nullptr;
    return std::launder(reinterpret_cast<const MaterialPass*>(
        reinterpret_cast<const unsigned char*>(&technique) + offsetof(MaterialTechnique, passArray)
        + index * sizeof(MaterialPass)));
}

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
