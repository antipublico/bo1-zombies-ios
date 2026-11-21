#include <database/fastfile_inspection.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
unsigned checks = 0, failures = 0;
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
}

int main()
{
    using namespace bo1::fastfile;
    unsigned char fixture[] = {
        0x49,0x57,0x66,0x66,0x75,0x31,0x30,0x30,0xD9,0x01,0x00,0x00,
        0x78,0x9C,0xB3,0x62,0xC0,0x0E,0x1C,0x90,0xD8,0xCC,0x40,0xFC,
        0x1F,0x08,0x98,0xA0,0x34,0x03,0x94,0x06,0x81,0x8C,0xD4,0x9C,
        0x9C,0x7C,0x86,0xAA,0xFC,0xDC,0xA4,0xCC,0xD4,0x62,0x06,0x15,
        0xA8,0x0E,0x55,0x28,0x0D,0x00,0x39,0xEE,0x15,0xC6
    };
    const auto path = std::filesystem::temp_directory_path() /
        ("bo1-inspection-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ff");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    const auto write = [&]() {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(fixture), sizeof(fixture));
        return bool(stream);
    };
    Check(write(), "write synthetic fixture");
    Inspection inspection;
    std::string error;
    const auto result = InspectFile(path.string().c_str(), inspection, error);
    Check(result == InspectionResult::Ok, error.c_str());
    Check(error.empty(), "successful call clears diagnostic");
    Check(inspection.header.version == 473 && inspection.file.size == 58, "disk headers decoded");
    Check(inspection.expandedBytes == 94 && inspection.metadataBytes == 94, "metadata and stream consumption");
    Check(inspection.scriptStrings.size() == 3 && !inspection.scriptStrings[0].present
        && inspection.scriptStrings[1].value == "hello" && inspection.scriptStrings[2].value == "zombies", "inline and null strings");
    Check(inspection.assets.size() == 2 && inspection.assets[0].type == 36
        && inspection.assets[1].type == 37 && inspection.assets[0].serializedTag == 0, "asset types and serialized tags");
    Check(std::string(AssetTypeName(36)) == "rawfile" && std::string(AssetTypeName(43)) == "unknown", "asset name bounds");

    auto failure = [&](InspectionResult expected, InspectionLimits limits) {
        inspection.header.version = 123;
        inspection.assets.resize(1);
        Check(InspectFile(path.string().c_str(), inspection, error, limits) == expected, "expected failure category");
        Check(!error.empty(), "failure provides a diagnostic");
        Check(inspection.header.version == 0 && inspection.expandedBytes == 0
            && inspection.assets.empty() && inspection.scriptStrings.empty(), "no partial output after failure");
    };
    InspectionLimits limits;
    limits.maxFileBytes = sizeof(fixture) - 1;
    failure(InspectionResult::LimitExceeded, limits);
    limits = {};
    limits.maxExpandedBytes = 93;
    failure(InspectionResult::LimitExceeded, limits);
    limits = {};
    limits.maxMetadataBytes = 1;
    failure(InspectionResult::LimitExceeded, limits);
    fixture[sizeof(fixture) - 1] ^= 1;
    Check(write(), "write checksum-damaged fixture");
    failure(InspectionResult::CorruptStream, {});
    Check(InspectFile(nullptr, inspection, error) == InspectionResult::IoError, "null path is rejected");
    std::printf("%u inspection checks, %u failures\n", checks, failures);
    return failures != 0;
}
