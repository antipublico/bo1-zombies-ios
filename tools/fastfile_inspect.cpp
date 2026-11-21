#include <database/fastfile_inspection.h>
#include <inttypes.h>
#include <cstdio>

int main(int argc, char** argv)
{
    using namespace bo1::fastfile;
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: bo1_fastfile_inspect <zone.ff>\n");
        return 2;
    }
    Inspection inspection;
    std::string error;
    if (InspectFile(argv[1], inspection, error) != InspectionResult::Ok)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
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
    std::printf("Envelope, compressed stream and metadata verified. Asset reconstruction remains pending.\n");
    return 0;
}
