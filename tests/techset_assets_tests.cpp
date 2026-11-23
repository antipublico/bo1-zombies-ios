#include "techset_fixture.h"
#include <database/fastfile_assets.h>
#include <platform/bo1_bytecode_io.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
using Bytes = std::vector<unsigned char>;
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* name)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
void Put32(Bytes& bytes, size_t offset, uint32_t value)
{
    bo1::bytecode::Store<uint32_t>(bytes.data() + offset, value);
}
constexpr uint32_t Block4(uint32_t offset) { return 0x80000001u + offset; }
Bytes Pack(const Bytes& payload)
{
    Bytes file = {'I','W','f','f','u','1','0','0',0xd9,1,0,0,0x78,1,1};
    const auto size = static_cast<uint16_t>(payload.size());
    file.push_back(size & 255); file.push_back(size >> 8);
    file.push_back((~size) & 255); file.push_back(((~size) >> 8) & 255);
    file.insert(file.end(), payload.begin(), payload.end());
    uint32_t a = 1, b = 0;
    for (auto byte : payload) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    const auto checksum = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8) file.push_back(static_cast<unsigned char>(checksum >> shift));
    return file;
}
}

int main()
{
    using namespace bo1::fastfile;
    const auto path = std::filesystem::temp_directory_path() /
        ("bo1-techset-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ff");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    std::string error;
    const bool fixtureValid = bo1::tests::CheckTechsetFixture(path.string().c_str(), error);
    Check(fixtureValid, error.c_str());
    Check(!std::filesystem::exists(path), "shared technique self-test removes temporary file");
    bo1::tests::TechsetFixtureOffsets offsets;
    const auto fixture = bo1::tests::TechsetFixture(&offsets);
    const Bytes pristine(fixture.begin() + 19, fixture.end() - 4);
    auto write = [&](const Bytes& file) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(file.data()), file.size());
        Check(bool(output), "write technique fixture");
    };
    AssetPrefix result;
    auto load = [&](Bytes payload, InspectionResult expected, AssetLoadLimits limits = {}) {
        write(Pack(payload));
        result.entries.push_back({});
        const auto actual = LoadAssetPrefix(path.string().c_str(), result, error, limits);
        Check(actual == expected, error.empty() ? "unexpected technique loader status" : error.c_str());
        if (expected != InspectionResult::Ok)
            Check(result.entries.empty() && result.inspection.assets.empty() && result.nativeBytes == 0
                && !error.empty(), "failed technique graph publishes no pointers");
    };
    auto mutate = [&](size_t offset, uint32_t value, InspectionResult expected) {
        auto payload = pristine;
        Put32(payload, offset, value);
        load(payload, expected);
    };
    load(pristine, InspectionResult::Ok);
    const auto nativeBytes = result.nativeBytes;
    Check(result.entries.size() == 4 && result.entries[3].rawfile,
        "complete technique graph leaves the next root synchronized");
    Check(result.inspection.file.blockSize[0] == 528 && offsets.block4Size == 396,
        "fixture uses one TEMP header and independent aligned block 4 cursor");

    mutate(8, 527, InspectionResult::InvalidFormat);
    mutate(24, offsets.block4Size - 1, InspectionResult::InvalidFormat);
    mutate(24, 35, InspectionResult::InvalidFormat);
    mutate(offsets.firstSet + 8, Block4(offsets.techniqueBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.firstSet + 12, Block4(offsets.declarationBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.firstSet + 8, UINT32_MAX - 1, InspectionResult::UnsupportedReference);
    mutate(offsets.passes + 20, Block4(offsets.vertexShaderBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.passes + 24, Block4(offsets.pixelShaderBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.passes + 28, Block4(offsets.vertexShaderBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.passes + 20, Block4(offsets.declarationBlock + 4), InspectionResult::UnsupportedReference);
    mutate(offsets.arguments + 12, Block4(offsets.vertexShaderBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.arguments + 4, Block4(offsets.literalBlock), InspectionResult::UnsupportedReference);
    mutate(offsets.arguments + 4, UINT32_MAX - 1, InspectionResult::UnsupportedReference);
    mutate(offsets.secondSet + 8 + 129 * 4, 0x00000001, InspectionResult::UnsupportedReference);

    auto payload = pristine;
    payload[offsets.declaration] = 17;
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    payload[offsets.declaration + 1] = 0xff;
    load(payload, InspectionResult::Ok);
    if (!result.entries.empty())
        Check(TechniquePassAt(*result.entries[0].techniqueSet->techniques[0], 0)->vertexDecl->hasOptionalSource,
            "serialized optional-source byte is normalized to a C++ bool");
    payload = pristine;
    bo1::bytecode::Store<uint16_t>(payload.data() + offsets.technique + 6, UINT16_MAX);
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    bo1::bytecode::Store<uint16_t>(payload.data() + offsets.vertexShader + 12, UINT16_MAX);
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    bo1::bytecode::Store<uint16_t>(payload.data() + offsets.pixelShader + 12, UINT16_MAX);
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    payload[offsets.passes + 12] = 255;
    payload[offsets.passes + 13] = 255;
    payload[offsets.passes + 14] = 255;
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    payload.resize(offsets.literal + 15);
    Put32(payload, 0, static_cast<uint32_t>(payload.size() - 36));
    load(payload, InspectionResult::InvalidFormat);

    AssetLoadLimits limits;
    limits.maxNativeBytes = 0;
    load(pristine, InspectionResult::LimitExceeded, limits);
    limits.maxNativeBytes = static_cast<size_t>(nativeBytes - 1);
    load(pristine, InspectionResult::LimitExceeded, limits);
    limits.maxNativeBytes = static_cast<size_t>(nativeBytes);
    load(pristine, InspectionResult::Ok, limits);

    mutate(offsets.aliasRoot, 36, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedRootReference && result.nextAsset == 1,
        "inserted root reference preserves its asset type");
    mutate(offsets.aliasRoot + 4, Block4(4), InspectionResult::Ok);
    Check(result.entries.size() == 4 && result.entries[1].alias
        && result.entries[1].techniqueSet == result.entries[0].techniqueSet,
        "root directory slot alias resolves the reconstructed set");
    mutate(offsets.rawRoot, 8, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedType && result.nextAsset == 3,
        "unsupported asset preserves the completed technique prefix");

    // A root with all null techniques is valid and consumes no persistent body.
    payload.assign(52 + 8 + 528, 0);
    Put32(payload, 0, static_cast<uint32_t>(payload.size() - 36));
    Put32(payload, 8, 528); Put32(payload, 24, 8);
    Put32(payload, 44, 1); Put32(payload, 48, UINT32_MAX);
    Put32(payload, 52, 7); Put32(payload, 56, UINT32_MAX);
    load(payload, InspectionResult::Ok);
    Check(result.entries.size() == 1 && result.entries[0].techniqueSet
        && !result.entries[0].techniqueSet->name && !result.entries[0].techniqueSet->techniques[129],
        "null technique slots do not consume stream bytes");

    // Corruption occurs after a successfully materialized prefix and must still
    // discard its shared native graph when zlib finishes validating the file.
    payload = pristine;
    Put32(payload, offsets.rawRoot, 8);
    payload.resize(60000, 0);
    Put32(payload, 0, static_cast<uint32_t>(payload.size() - 36));
    auto corrupted = Pack(payload);
    corrupted.back() ^= 1;
    write(corrupted);
    Check(LoadAssetPrefix(path.string().c_str(), result, error) == InspectionResult::CorruptStream
        && result.entries.empty() && result.inspection.assets.empty(),
        "late checksum failure discards reconstructed technique graphs");
    std::printf("%u technique asset checks, %u failures\n", checks, failures);
    return failures != 0;
}
