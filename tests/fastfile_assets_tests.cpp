#include "native_asset_fixture.h"
#include <database/fastfile_assets.h>
#include <platform/bo1_bytecode_io.h>
#include <chrono>
#include <cstdio>
#include <cstring>
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
Bytes Pack(const Bytes& payload)
{
    Bytes file = {'I','W','f','f','u','1','0','0',0xd9,1,0,0,0x78,1,1};
    const auto size = static_cast<uint16_t>(payload.size());
    file.push_back(size & 255); file.push_back(size >> 8);
    file.push_back((~size) & 255); file.push_back(((~size) >> 8) & 255);
    file.insert(file.end(), payload.begin(), payload.end());
    uint32_t a = 1, b = 0;
    for (auto byte : payload) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    const uint32_t checksum = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8) file.push_back(checksum >> shift);
    return file;
}
}

int main()
{
    using namespace bo1::fastfile;
    const auto path = std::filesystem::temp_directory_path() /
        ("bo1-native-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ff");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    std::string error;
    const bool fixtureValid = bo1::tests::CheckNativeAssetFixture(path.string().c_str(), error);
    Check(fixtureValid, error.c_str());
    Check(!std::filesystem::exists(path), "shared native self-test removes temporary file");
    const Bytes fixture = bo1::tests::NativeAssetFixture();
    const Bytes pristine(fixture.begin() + 19, fixture.end() - 4);
    Check(pristine.size() == 221, "fixture size");

    auto write = [&](const Bytes& file) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(file.data()), file.size());
        Check(bool(output), "write fixture");
    };
    AssetPrefix result;
    auto load = [&](Bytes payload, InspectionResult expected, AssetLoadLimits limits = {}) {
        write(Pack(payload));
        result.entries.push_back({}); // Failure must clear previous visible state.
        const auto actual = LoadAssetPrefix(path.string().c_str(), result, error, limits);
        Check(actual == expected, error.empty() ? "unexpected loader status" : error.c_str());
        if (expected != InspectionResult::Ok)
            Check(result.entries.empty() && result.inspection.assets.empty()
                && result.nativeBytes == 0 && !error.empty(), "failure publishes no partial native state");
    };
    auto mutate = [&](size_t offset, uint32_t value, InspectionResult expected) {
        auto payload = pristine;
        Put32(payload, offset, value);
        load(payload, expected);
    };

    mutate(8, 20, InspectionResult::Ok); // Block 0 rewinds: never sum all roots.
    Check(result.entries.size() == 4, "TEMP stream resets per root");
    mutate(8, 19, InspectionResult::InvalidFormat);
    mutate(24, 135, InspectionResult::InvalidFormat);
    mutate(88, UINT32_MAX, InspectionResult::InvalidFormat);
    mutate(88, INT32_MAX, InspectionResult::InvalidFormat);
    mutate(124, UINT32_MAX, InspectionResult::InvalidFormat);
    mutate(128, INT32_MAX, InspectionResult::InvalidFormat);
    mutate(132, 0, InspectionResult::InvalidFormat);
    mutate(84, 0x80000022, InspectionResult::UnsupportedReference);
    mutate(150, 0x8000006f, InspectionResult::UnsupportedReference); // Future gamma.
    mutate(158, 0x80000022, InspectionResult::UnsupportedReference); // Interior of raw name.
    mutate(158, 0x60000021, InspectionResult::UnsupportedReference);

    auto payload = pristine;
    bo1::bytecode::Store<int16_t>(payload.data() + 195, -1);
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    bo1::bytecode::Store<int16_t>(payload.data() + 195, 4);
    load(payload, InspectionResult::InvalidFormat);
    payload = pristine;
    bo1::bytecode::Store<int16_t>(payload.data() + 195, 2);
    load(payload, InspectionResult::Ok); // Preserve disk order, as the engine loader does.

    mutate(52, 8, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedType && result.nextAsset == 0
        && result.decodedAssetBytes == 0, "unsupported first body is never consumed");
    mutate(60, 8, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedType && result.nextAsset == 1
        && result.decodedAssetBytes == 36 && result.entries[0].rawfile,
        "supported prefix survives unknown second type");
    mutate(80, 0x60000001, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedRootReference && result.nextAsset == 3,
        "unsupported root reference stops before dereference");
    payload = pristine;
    Put32(payload, 76, 37);
    Put32(payload, 80, 0x80000039);
    load(payload, InspectionResult::Ok);
    Check(result.entries.size() == 4 && result.entries[3].alias
        && result.entries[3].stringTable == result.entries[1].stringTable,
        "inserted -2 alias slot resolves to the same object");
    mutate(76, 37, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedRootReference && result.nextAsset == 3,
        "root reference cannot change asset type");

    payload = pristine;
    payload[119] = 'X'; // Preserve raw len+1 bytes without imposing a NUL policy.
    load(payload, InspectionResult::Ok);
    Check(result.entries.size() == 4 && result.entries[0].rawfile->buffer[7] == 'X',
        "raw payload consumes len+1 bytes verbatim");
    AssetLoadLimits limits;
    limits.maxNativeBytes = 0;
    load(pristine, InspectionResult::LimitExceeded, limits);

    // Two native strings referencing the same metadata string must share the
    // same retained address, even after the metadata result itself is copied.
    payload.assign(77, 0);
    Put32(payload, 0, 41); Put32(payload, 8, 8); Put32(payload, 24, 32);
    Put32(payload, 36, 1); Put32(payload, 40, UINT32_MAX);
    Put32(payload, 44, 1); Put32(payload, 48, UINT32_MAX);
    Put32(payload, 52, UINT32_MAX);
    std::memcpy(payload.data() + 56, "seed", 5);
    Put32(payload, 61, 23); Put32(payload, 65, UINT32_MAX);
    Put32(payload, 69, 0x80000005); Put32(payload, 73, 0x80000005);
    load(payload, InspectionResult::Ok);
    Check(result.entries.size() == 1 && result.entries[0].localize
        && result.entries[0].localize->name == result.entries[0].localize->value
        && std::strcmp(result.entries[0].localize->name, "seed") == 0,
        "metadata string aliases preserve pointer identity");

    payload = pristine;
    Put32(payload, 52, 8);
    payload.resize(60000, 0);
    Put32(payload, 0, payload.size() - 36);
    auto damaged = Pack(payload);
    damaged.back() ^= 1;
    write(damaged);
    Check(LoadAssetPrefix(path.string().c_str(), result, error) == InspectionResult::CorruptStream
        && result.entries.empty() && result.inspection.assets.empty(),
        "late checksum failure invalidates even an otherwise supported prefix");

    payload = pristine;
    payload.push_back(0);
    Put32(payload, 0, payload.size() - 36);
    load(payload, InspectionResult::InvalidFormat);
    // An XModel header is useful diagnostic evidence even before its
    // surface/material child bodies are supported. Do not call it loaded.
    Bytes modelPayload(36 + 16 + 8 + 252, 0);
    Put32(modelPayload, 0, static_cast<uint32_t>(modelPayload.size() - 36));
    Put32(modelPayload, 8, 252); // TEMP holds the 252-byte Win32 XModel.
    Put32(modelPayload, 24, 4096); // Directory in stream block 4.
    Put32(modelPayload, 36 + 8, 1); // One directory entry.
    Put32(modelPayload, 36 + 12, UINT32_MAX);
    Put32(modelPayload, 36 + 16, 5); // Asset type: XModel.
    Put32(modelPayload, 36 + 20, UINT32_MAX); // Inline root.
    constexpr size_t modelStart = 36 + 16 + 8;
    modelPayload[modelStart + 4] = 8; // bones
    modelPayload[modelStart + 5] = 2; // root bones
    modelPayload[modelStart + 6] = 12; // surfaces
    bo1::bytecode::Store<uint16_t>(modelPayload.data() + modelStart + 44, 12);
    bo1::bytecode::Store<int16_t>(modelPayload.data() + modelStart + 216, 1);
    Put32(modelPayload, modelStart + 32, UINT32_MAX); // Surface array.
    Put32(modelPayload, modelStart + 36, UINT32_MAX); // Material pointers.
    Put32(modelPayload, modelStart + 172, UINT32_MAX); // Collision surfaces.
    Put32(modelPayload, modelStart + 176, 3);
    // Load_XSurfaceArray reads 12 serialized 68-byte headers as one block.
    modelPayload.insert(modelPayload.end(), 12 * XSurfaceDiskHeaderSize, 0);
    Put32(modelPayload, 0, static_cast<uint32_t>(modelPayload.size() - 36));
    load(modelPayload, InspectionResult::Ok);
    Check(result.stop == AssetStop::UnsupportedModelBody && result.nextAsset == 0
        && result.entries.empty() && result.pendingXModel.has_value()
        && result.pendingXModelSkeleton.has_value()
        && result.pendingXModelSkeleton->inlineArrayBytes == 0
        && result.pendingXModelSkeleton->surfaceHeaders.size() == 12
        && result.pendingXModelSkeleton->surfaceHeaderBytes == 816
        && result.pendingXModel->numBones == 8 && result.pendingXModel->numSurfaces == 12
        && result.pendingXModel->numCollisionSurfaces == 3
        && result.decodedAssetBytes == 252 + 816,
        "XModel and surface headers preview stops before vertex data and materials");
    Check(result.pendingXModel && CalculateXModelInlineBytes(*result.pendingXModel).surfaceHeaders == 816,
        "pending XModel metadata exposes bounded surface array estimate");

    // The native loader must read XString followed by the six fixed skeletal
    // arrays in exactly the original Load_XModel child order, without padding
    // in the compressed stream. Block-4 alignment is tracked independently.
    auto modelWithBones = Bytes(modelPayload.begin(),
        modelPayload.begin() + modelStart + XModelDiskHeaderSize);
    modelWithBones[modelStart + 4] = 3; // three bones, one root.
    modelWithBones[modelStart + 5] = 1;
    Put32(modelWithBones, modelStart, UINT32_MAX); // XString.
    for (size_t field : {size_t(8), size_t(12), size_t(16), size_t(20), size_t(24), size_t(28)})
        Put32(modelWithBones, modelStart + field, UINT32_MAX);
    const char name[] = "model";
    modelWithBones.insert(modelWithBones.end(), name, name + sizeof(name));
    for (size_t i = 0; i < 6; ++i) modelWithBones.push_back(static_cast<uint8_t>(i + 1));
    modelWithBones.push_back(0); modelWithBones.push_back(1); // parent bytes.
    for (size_t i = 0; i < 16 + 32 + 3 + 96; ++i)
        modelWithBones.push_back(static_cast<uint8_t>((i + 3) & 255));
    modelWithBones.insert(modelWithBones.end(), 12 * XSurfaceDiskHeaderSize, 0);
    Put32(modelWithBones, 0, static_cast<uint32_t>(modelWithBones.size() - 36));
    load(modelWithBones, InspectionResult::Ok);
    Check(result.pendingXModelSkeleton && result.pendingXModelSkeleton->name == "model"
        && result.pendingXModelSkeleton->boneNameBytes.size() == 6
        && result.pendingXModelSkeleton->boneNameBytes[0] == 1
        && result.pendingXModelSkeleton->parentList.size() == 2
        && result.pendingXModelSkeleton->parentList[1] == 1
        && result.pendingXModelSkeleton->quaternions.size() == 16
        && result.pendingXModelSkeleton->translations.size() == 32
        && result.pendingXModelSkeleton->classifications.size() == 3
        && result.pendingXModelSkeleton->baseMatrices.size() == 96
        && result.pendingXModelSkeleton->inlineArrayBytes == 155
        && result.pendingXModelSkeleton->unresolvedFields == 0
        && result.pendingXModelSkeleton->surfaceHeaders.size() == 12
        && result.decodedAssetBytes == 252 + 6 + 155 + 816,
        "XModel skeleton preview reads ordered inline children without Win32 pointer casts");

    auto referencedModel = modelPayload;
    Put32(referencedModel, modelStart + 8, 0x80000029u); // Previously stored bone array.
    load(referencedModel, InspectionResult::Ok);
    Check(result.pendingXModelSkeleton && result.pendingXModelSkeleton->unresolvedFields == 1u
        && result.pendingXModelSkeleton->boneNameBytes.empty()
        && result.decodedAssetBytes == 252 + 816,
        "referenced bone array remains unresolved without consuming FILE data");

    auto withGeometry = modelPayload;
    constexpr size_t surfStart = modelStart + XModelDiskHeaderSize;
    bo1::bytecode::Store<uint16_t>(withGeometry.data() + surfStart + 4, 123);
    bo1::bytecode::Store<uint16_t>(withGeometry.data() + surfStart + 6, 45);
    bo1::bytecode::Store<uint16_t>(withGeometry.data() + surfStart + 2, 2);
    load(withGeometry, InspectionResult::Ok);
    Check(result.pendingXModelSkeleton && result.pendingXModelSkeleton->surfaceHeaders.size() == 12
        && result.pendingXModelSkeleton->surfaceHeaders[0].vertCount == 123
        && result.pendingXModelSkeleton->surfaceHeaders[0].triCount == 45
        && result.pendingXModelSkeleton->surfaceHeaders[0].flags == 2
        && result.pendingXModelSkeleton->declaredVertices == 123
        && result.pendingXModelSkeleton->declaredTriangles == 45,
        "XModel geometry preview decodes a serialized XSurface array");
    auto malformedSurface = modelPayload;
    bo1::bytecode::Store<int16_t>(malformedSurface.data() + surfStart + 16, -1);
    load(malformedSurface, InspectionResult::InvalidFormat);
    Check(!result.pendingXModel && !result.pendingXModelSkeleton,
        "invalid XSurface header does not leak partial model data");

    auto invalidModel = modelPayload;
    invalidModel[modelStart + 5] = 9; // More root bones than total bones.
    load(invalidModel, InspectionResult::InvalidFormat);
    Check(!result.pendingXModel, "invalid XModel header does not publish preview");

    auto damagedModel = Pack(modelPayload);
    damagedModel.back() ^= 1;
    write(damagedModel);
    Check(LoadAssetPrefix(path.string().c_str(), result, error) == InspectionResult::CorruptStream
        && !result.pendingXModel && result.entries.empty(),
        "late checksum failure discards pending XModel preview");

    std::printf("%u native asset checks, %u failures\n", checks, failures);
    return failures != 0;
}
