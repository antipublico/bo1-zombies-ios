#include <database/fastfile_inspection.h>
#include <database/fastfile_assets.h>
#include <inttypes.h>
#include <cstdio>
#include <cstring>
#include <set>

int main(int argc, char** argv)
{
    using namespace bo1::fastfile;
    const bool loadAssets = argc == 3 && std::strcmp(argv[1], "--load-assets") == 0;
    if (argc != 2 && !loadAssets)
    {
        std::fprintf(stderr, "Usage: bo1_fastfile_inspect [--load-assets] <zone.ff>\n");
        return 2;
    }
    Inspection inspection;
    AssetPrefix native;
    std::string error;
    const auto result = loadAssets ? LoadAssetPrefix(argv[2], native, error)
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
