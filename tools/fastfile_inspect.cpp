#include <database/fastfile_inspection.h>
#include <database/fastfile_assets.h>
#include <platform/bo1_texture_bc.h>
#include <platform/bo1_iwi_image.h>
#include <inttypes.h>
#include <cstdio>
#include <cstring>
#include <set>
#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

int main(int argc, char** argv)
{
    using namespace bo1::fastfile;
    const bool iwiOnly = argc == 3 && std::strcmp(argv[1], "--inspect-iwi") == 0;
    const bool iwiDecode = argc == 4 && std::strcmp(argv[1], "--decode-iwi") == 0;
    if (iwiOnly || iwiDecode)
    {
        std::ifstream stream(argv[2], std::ios::binary | std::ios::ate);
        if (!stream)
        {
            std::fprintf(stderr, "Could not open IWI image.\n");
            return 1;
        }
        const auto last = stream.tellg();
        if (last < static_cast<std::streamoff>(bo1::texture::IwiHeaderBytes))
        {
            std::fprintf(stderr, "IWI image is shorter than its 48-byte header.\n");
            return 1;
        }
        const std::uint64_t total = static_cast<std::uint64_t>(last);
        stream.seekg(0, std::ios::beg);
        std::array<unsigned char, bo1::texture::IwiHeaderBytes> disk{};
        stream.read(reinterpret_cast<char*>(disk.data()), disk.size());
        bo1::texture::IwiHeader header{};
        if (!stream || bo1::texture::ParseIwiHeader(disk.data(), disk.size(), total, header)
                != bo1::texture::IwiParseResult::Ok)
        {
            std::fprintf(stderr, "IWI signature, version, dimensions or file lengths are invalid.\n");
            return 1;
        }
        std::printf("IWI version %u format %u (%s) flags 0x%02x\n",
            unsigned(header.version), unsigned(header.format),
            bo1::texture::IwiFormatName(header.format), unsigned(header.flags));
        std::printf("IWI dimensions %ux%ux%u gamma %.3f total bytes %" PRIu64 "\n",
            unsigned(header.width), unsigned(header.height),
            unsigned(header.depth), header.gamma, header.fileBytes);
        std::printf("IWI picmip file bytes");
        for (const auto s : header.fileSizeForPicmip) std::printf(" %" PRIu32, s);
        std::printf("\n");
        std::uint64_t begin = 0;
        std::size_t compressedBytes = 0;
        const bool topMip = bo1::texture::IwiBC2TopMipRange(
            header, begin, compressedBytes);
        if (topMip)
            std::printf("IWI DXT3 highest-resolution mip offset %" PRIu64
                " BC2 bytes %zu (can decode RGBA8)\n", begin, compressedBytes);
        if (!iwiDecode) return 0;
        if (!topMip)
        {
            std::fprintf(stderr, "This IWI cannot be decoded as a 2D DXT3/BC2 image.\n");
            return 1;
        }
        std::vector<std::uint8_t> compressed(compressedBytes);
        stream.seekg(static_cast<std::streamoff>(begin), std::ios::beg);
        stream.read(reinterpret_cast<char*>(compressed.data()), compressed.size());
        std::vector<std::uint8_t> rgba;
        if (!stream || !bo1::texture::DecodeBC2RGBA(compressed.data(),
            compressed.size(), header.width, header.height, rgba))
        {
            std::fprintf(stderr, "Could not decode the IWI BC2 pixels.\n");
            return 1;
        }
        // Uncompressed, 32-bit BGRA TGA with top-left origin. No third-party
        // image codec is needed; keep the source IWI untouched.
        std::array<std::uint8_t, 18> tga{};
        tga[2] = 2;
        tga[12] = static_cast<std::uint8_t>(header.width);
        tga[13] = static_cast<std::uint8_t>(header.width >> 8);
        tga[14] = static_cast<std::uint8_t>(header.height);
        tga[15] = static_cast<std::uint8_t>(header.height >> 8);
        tga[16] = 32;
        tga[17] = 0x28;
        for (std::size_t i = 0; i < rgba.size(); i += 4)
            std::swap(rgba[i], rgba[i + 2]);
        std::ofstream result(argv[3], std::ios::binary | std::ios::trunc);
        result.write(reinterpret_cast<const char*>(tga.data()), tga.size());
        result.write(reinterpret_cast<const char*>(rgba.data()), rgba.size());
        if (!result)
        {
            std::fprintf(stderr, "Could not write the TGA preview.\n");
            return 1;
        }
        std::printf("IWI decoded to BGRA8 TGA at the requested output path.\n");
        return 0;
    }
    const bool loadAssets = argc == 3 && std::strcmp(argv[1], "--load-assets") == 0;
    if (argc != 2 && !loadAssets)
    {
        std::fprintf(stderr, "Usage: bo1_fastfile_inspect [--load-assets] <zone.ff>\n"
            "       bo1_fastfile_inspect --inspect-iwi <image.iwi>\n"
            "       bo1_fastfile_inspect --decode-iwi <image.iwi> <preview.tga>\n");
        return 2;
    }
    Inspection inspection;
    AssetPrefix native;
    std::string error;
    AssetLoadLimits loadLimits;
    loadLimits.previewFirstMaterialHeader = true; // Local diagnostic only.
    loadLimits.previewFirstImageHeader = true;
    const auto result = loadAssets ? LoadAssetPrefix(argv[2], native, error, loadLimits)
                                   : InspectFile(argv[1], inspection, error);
    if (result != InspectionResult::Ok)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (loadAssets)
        inspection = std::move(native.inspection);
    std::printf("Version %" PRIu32 "\n", inspection.header.version);
    std::printf("Declared payload bytes %" PRIu32 "\n", inspection.file.size);
    std::printf("External bytes %" PRIu32 "\n", inspection.file.externalSize);
    std::printf("Inflated bytes %" PRIu64 "\n", inspection.expandedBytes);
    std::printf("Script strings %" PRIu32 " (table %s)\n", inspection.list.scriptStringCount, inspection.list.scriptStringsTag ? "present" : "absent");
    std::printf("Assets %" PRIu32 " (table %s)\n", inspection.list.assetCount, inspection.list.assetsTag ? "present" : "absent");
    std::printf("Metadata bytes %" PRIu64 "\n", inspection.metadataBytes);
    size_t populated = 0;
    for (const auto& string : inspection.scriptStrings)
        populated += string.present;
    std::printf("Decoded script strings %zu\n", populated);
    size_t counts[43]{};
    for (const auto& asset : inspection.assets)
        ++counts[asset.type];
    for (uint32_t type = 0; type < 43; ++type)
        if (counts[type])
            std::printf("Asset type %" PRIu32 " %s count %zu\n", type, AssetTypeName(type), counts[type]);
    for (size_t index = 0; index < 7; ++index)
        std::printf("Block %zu bytes %" PRIu32 "\n", index, inspection.file.blockSize[index]);
    if (loadAssets)
    {
        size_t raw = 0, tables = 0, cells = 0, localized = 0, aliases = 0;
        size_t techsets = 0, passes = 0, shaderWords = 0;
        std::set<const MaterialTechnique*> techniques;
        std::set<const MaterialVertexShader*> vertexShaders;
        std::set<const MaterialPixelShader*> pixelShaders;
        for (const auto& asset : native.entries)
        {
            raw += asset.rawfile != nullptr;
            tables += asset.stringTable != nullptr;
            localized += asset.localize != nullptr;
            aliases += asset.alias;
            if (asset.techniqueSet)
            {
                ++techsets;
                for (const auto* technique : asset.techniqueSet->techniques)
                {
                    if (!technique || !techniques.insert(technique).second) continue;
                    passes += technique->passCount;
                    for (size_t i = 0; i < technique->passCount; ++i)
                    {
                        const auto* pass = TechniquePassAt(*technique, i);
                        if (pass->vertexShader && vertexShaders.insert(pass->vertexShader).second
                            && pass->vertexShader->prog.loadDef.program)
                            shaderWords += pass->vertexShader->prog.loadDef.programSize;
                        if (pass->pixelShader && pixelShaders.insert(pass->pixelShader).second
                            && pass->pixelShader->prog.loadDef.program)
                            shaderWords += pass->pixelShader->prog.loadDef.programSize;
                    }
                }
            }
            if (asset.stringTable)
                cells += size_t(asset.stringTable->rowCount) * size_t(asset.stringTable->columnCount);
        }
        std::printf("Native roots %zu of %zu\n", native.entries.size(), inspection.assets.size());
        std::printf("Native rawfiles %zu tables %zu cells %zu localized %zu aliases %zu\n", raw, tables, cells, localized, aliases);
        std::printf("Native techsets %zu unique techniques %zu passes %zu vertex shaders %zu pixel shaders %zu shader words %zu\n",
            techsets, techniques.size(), passes, vertexShaders.size(), pixelShaders.size(), shaderWords);
        std::printf("Native accounted bytes %" PRIu64 "\n", native.nativeBytes);
        std::printf("Asset body bytes %" PRIu64 "\n", native.decodedAssetBytes);
        if (native.stop == AssetStop::Complete)
            std::printf("Native prefix complete. Engine registration and gameplay remain pending.\n");
        else
            std::printf("Native prefix stopped at index %zu type %" PRIu32 " (%s), reason %s.\n", native.nextAsset,
                inspection.assets[native.nextAsset].type, AssetTypeName(inspection.assets[native.nextAsset].type),
                native.stop == AssetStop::UnsupportedType ? "unsupported type" :
                native.stop == AssetStop::UnsupportedModelBody ? "XModel child resources not registered" :
                "unsupported root reference");
        if (native.pendingXModel)
        {
            const auto& m = *native.pendingXModel;
            std::printf("XModel header bones %u rootBones %u surfaces %u LODs %d collisionSurfaces %d\n",
                unsigned(m.numBones), unsigned(m.numRootBones), unsigned(m.numSurfaces),
                int(m.numLods), int(m.numCollisionSurfaces));
            if (native.pendingXModelSkeleton)
            {
                const auto& skel = *native.pendingXModelSkeleton;
                std::printf("XModel name %s\n", skel.name.empty() ? "(not resolved)" : skel.name.c_str());
                std::printf("XModel retained skeleton bytes %" PRIu64 " unresolved child bits 0x%08" PRIx32 "\n",
                    skel.inlineArrayBytes, skel.unresolvedFields);
                std::printf("XModel XSurface headers %zu declared vertices %" PRIu64
                    " declared triangles %" PRIu64 " retained geometry bytes %" PRIu64 "\n",
                    skel.surfaceHeaders.size(), skel.declaredVertices, skel.declaredTriangles,
                    skel.decodedGeometryBytes);
                size_t missing = 0;
                for (const auto& surf : skel.surfaceBodies)
                    missing += surf.unresolvedFields != 0;
                std::printf("XModel surfaces with unresolved shared child references %zu\n", missing);
                std::printf("XModel material slots %zu inline %u referenced %u empty %u (unresolved)\n",
                    skel.materialHandleTags.size(), skel.inlineMaterials,
                    skel.referencedMaterials, skel.emptyMaterialSlots);
                for (size_t i = 0; i < skel.materialHandleTags.size() && i < 12; ++i)
                    std::printf("XModel material slot %zu Win32 tag 0x%08" PRIx32 "\n",
                        i, skel.materialHandleTags[i]);
                if (skel.firstInlineMaterial)
                {
                    const auto& material = *skel.firstInlineMaterial;
                    std::printf("First inline Material slot %zu name %s\n", material.slot,
                        material.name.empty() ? "(not resolved)" : material.name.c_str());
                    std::printf("First inline Material counts textures %u constants %u stateBits %u sortKey %u\n",
                        unsigned(material.textureCount), unsigned(material.constantCount),
                        unsigned(material.stateBitsCount), unsigned(material.sortKey));
                    std::printf("First inline Material tags technique 0x%08" PRIx32
                        " textures 0x%08" PRIx32 " constants 0x%08" PRIx32
                        " stateBits 0x%08" PRIx32 "\n",
                        material.techniqueSetTag, material.textureTableTag,
                        material.constantTableTag, material.stateBitsTableTag);
                    std::printf("First inline Material texture records %zu bytes %" PRIu64
                        " pending images/water %s inline techset pending %s shared table %s\n",
                        material.textureHeaders.size(), material.textureHeaderBytes,
                        material.textureChildrenPending ? "yes" : "no",
                        material.nestedTechniqueSetPending ? "yes" : "no",
                        material.textureTableReferenced ? "yes" : "no");
                    for (size_t i = 0; i < material.textureHeaders.size() && i < 24; ++i)
                    {
                        const auto& texture = material.textureHeaders[i];
                        std::printf("First inline Material texture %zu nameHash 0x%08" PRIx32
                            " semantic %u sampler %u image/water tag 0x%08" PRIx32 "\n",
                            i, texture.nameHash, unsigned(texture.semantic),
                            unsigned(texture.samplerState), texture.imageOrWaterTag);
                    }
                    if (material.firstInlineImage)
                    {
                        const auto& image = *material.firstInlineImage;
                        std::printf("First inline GfxImage texture slot %zu name %s\n",
                            image.textureSlot,
                            image.name.empty() ? "(not resolved)" : image.name.c_str());
                        if (image.category == 3 && !image.name.empty())
                            std::printf("Expected external IWI path images/%s.iwi (may be inside an IWD archive)\n",
                                image.name.c_str());
                        std::printf("First inline GfxImage dimensions %ux%ux%u levels %u mapType %u semantic %u category %u streaming %u\n",
                            unsigned(image.width), unsigned(image.height),
                            unsigned(image.depth), unsigned(image.levelCount),
                            unsigned(image.mapType), unsigned(image.semantic),
                            unsigned(image.category), unsigned(image.streaming));
                        std::printf("First inline GfxImage sizes base %" PRIu32 " loaded %" PRIu32
                            " hash 0x%08" PRIx32 "\n",
                            image.baseSize, image.loadedSize, image.hash);
                        std::printf("First inline GfxImage tags texture 0x%08" PRIx32
                            " pixels 0x%08" PRIx32 " name 0x%08" PRIx32 "\n",
                            image.textureTag, image.pixelsTag, image.nameTag);
                        if (image.inlineLoadDefHeader)
                            std::printf("First inline GfxImageLoadDef levels %u flags 0x%02x format %" PRId32
                                " resource bytes %" PRId32 " (pixel payload unread)\n",
                                unsigned(image.loadDefLevels), unsigned(image.loadDefFlags),
                                image.loadDefFormat, image.loadDefResourceSize);
                        if (image.inlineLoadDefHeader)
                        {
                            std::printf("First inline GfxImageLoadDef format name %s\n",
                                bo1::texture::FormatName(
                                    static_cast<std::uint32_t>(image.loadDefFormat)));
                            if (static_cast<std::uint32_t>(image.loadDefFormat)
                                == bo1::texture::DXT3)
                            {
                                std::size_t bc2Size = 0, rgbaSize = 0;
                                if (bo1::texture::BC2ByteSizes(
                                    image.width, image.height, bc2Size, rgbaSize))
                                    std::printf("First inline GfxImageLoadDef DXT3/BC2 expected compressed bytes %zu RGBA8 bytes %zu\n",
                                        bc2Size, rgbaSize);
                            }
                            if (!image.loadDefResourceSize && image.streaming)
                                std::printf("First inline GfxImageLoadDef streaming image has zero inline pixel bytes; pixel source still unknown.\n");
                        }
                        if (image.loadDefInsertedAlias)
                            std::printf("First inline GfxImageLoadDef inserted 4-byte block-4 alias slot (-2 tag).\n");
                        else if (image.loadDefPending)
                            std::printf("First inline GfxImageLoadDef not embedded or not yet decoded.\n");
                    }
                    if (material.inlineWaterBeforeImage)
                        std::printf("First inline GfxImage skipped: inline water_t precedes the image.\n");
                    std::printf("First inline Material is a CPU-only diagnostic; pixel bytes and shaders remain unresolved.\n");
                }
                for (size_t i = 0; i < skel.surfaceHeaders.size() && i < 12; ++i)
                {
                    const auto& s = skel.surfaceHeaders[i];
                    std::printf("XSurface %zu vertices %u triangles %u flags 0x%04x\n",
                        i, unsigned(s.vertCount), unsigned(s.triCount), unsigned(s.flags));
                }
            }
            std::printf("XModel is a CPU-only preview; native rendering and gameplay are not enabled.\n");
        }
    }
    std::printf("Envelope, compressed stream and metadata verified.\n");
    return 0;
}
