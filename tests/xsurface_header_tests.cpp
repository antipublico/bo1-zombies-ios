#include <database/db_xsurface_header.h>
#include <platform/bo1_bytecode_io.h>
#include <array>
#include <cstdint>
#include <cstdio>

namespace
{
using Data = std::array<unsigned char, bo1::fastfile::XSurfaceDiskHeaderSize + 1>;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* label)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
template<class T> void Put(Data& bytes, std::size_t offset, T value)
{
    bo1::bytecode::Store<T>(bytes.data() + 1 + offset, value);
}
}

int main()
{
    using namespace bo1::fastfile;
    Data disk{};
    disk[1] = 2; // tileMode
    disk[2] = 3; // vertListCount
    Put(disk, 2, std::uint16_t(0x8002));
    Put(disk, 4, std::uint16_t(25));
    Put(disk, 6, std::uint16_t(8));
    Put(disk, 8, std::uint16_t(11));
    Put(disk, 10, std::uint16_t(13));
    Put(disk, 12, UINT32_MAX);
    Put(disk, 16, std::int16_t(1));
    Put(disk, 18, std::int16_t(2));
    Put(disk, 20, std::int16_t(3));
    Put(disk, 22, std::int16_t(4));
    Put(disk, 24, UINT32_MAX);
    Put(disk, 28, UINT32_MAX);
    Put(disk, 32, UINT32_MAX);
    Put(disk, 36, 0x80000041u); // D3D runtime handles are not native pointers.
    Put(disk, 40, UINT32_MAX);
    Put(disk, 44, 0xFFFFFFFFu);
    Put(disk, 48, std::int32_t(-1));
    Put(disk, 52, std::int32_t(0x12345678));
    Put(disk, 64, std::int32_t(9));

    XSurfaceHeader s;
    Check(DecodeXSurfaceHeader(disk.data() + 1, XSurfaceDiskHeaderSize, s),
        "parse intentionally unaligned 68-byte header");
    Check(s.tileMode == 2 && s.vertListCount == 3 && s.flags == 0x8002,
        "tile, rigid list count, flags");
    Check(s.vertCount == 25 && s.triCount == 8 && s.baseTriIndex == 11
        && s.baseVertIndex == 13, "surface index counts");
    Check(s.triIndicesTag == UINT32_MAX && s.vertsBlendTag == UINT32_MAX
        && s.tensionDataTag == UINT32_MAX && s.packedVerticesTag == UINT32_MAX
        && s.rigidVertListTag == UINT32_MAX, "inline body tags");
    Check(s.serializedVertexBuffer == 0x80000041u
        && s.serializedIndexBuffer == UINT32_MAX, "D3D handle tags remain integers");
    Check(s.blendVertCounts[0] == 1 && s.blendVertCounts[1] == 2
        && s.blendVertCounts[2] == 3 && s.blendVertCounts[3] == 4,
        "weighted vertex group counts");
    Check(s.partBits[0] == -1 && s.partBits[1] == 0x12345678
        && s.partBits[4] == 9, "all part bit fields");

    auto bytes = CalculateXSurfaceInlineBytes(s);
    Check(bytes.blendIndices == 100, "blending index byte budget");
    Check(bytes.tensionData == 480, "tension array byte budget");
    Check(bytes.packedVertices == 800, "32-byte packed vertices");
    Check(bytes.rigidLists == 36, "12-byte rigid vertex lists");
    Check(bytes.triangleIndices == 48, "three 16-bit indices per triangle");
    Check(bytes.blendIndices + bytes.tensionData + bytes.packedVertices
        + bytes.rigidLists + bytes.triangleIndices == 1464, "total inline byte budget");

    auto valid = disk;
    Put(disk, 12, 0x80000011u); // Shared data from a prior stream block.
    Put(disk, 24, 0u);
    Put(disk, 28, 0x80000001u);
    Put(disk, 32, 0u);
    Put(disk, 40, 0x80000019u);
    Check(DecodeXSurfaceHeader(disk.data() + 1, XSurfaceDiskHeaderSize, s),
        "decode non-inline and absent child tags");
    bytes = CalculateXSurfaceInlineBytes(s);
    Check(bytes.blendIndices == 0 && bytes.tensionData == 0 && bytes.packedVertices == 0
        && bytes.rigidLists == 0 && bytes.triangleIndices == 0,
        "non-inline references do not reserve inline bytes");

    disk = valid;
    Check(!DecodeXSurfaceHeader(disk.data() + 1, XSurfaceDiskHeaderSize - 1, s)
        && s.vertCount == 0, "reject truncated header without publishing state");
    Check(!DecodeXSurfaceHeader(nullptr, XSurfaceDiskHeaderSize, s),
        "reject null buffer");
    Put(disk, 16, std::int16_t(-1));
    Check(!DecodeXSurfaceHeader(disk.data() + 1, XSurfaceDiskHeaderSize, s),
        "reject negative skin vertex count");
    disk = valid;
    Put(disk, 22, std::int16_t(26));
    Check(!DecodeXSurfaceHeader(disk.data() + 1, XSurfaceDiskHeaderSize, s),
        "reject blended vertex count exceeding surface vertices");
    disk = valid;
    Put(disk, 4, std::uint16_t(10));
    Check(DecodeXSurfaceHeader(disk.data() + 1, XSurfaceDiskHeaderSize, s),
        "allow all surface vertices to be skinned");

    std::printf("%u XSurface header checks, %u failures\n", checks, failures);
    return failures != 0;
}
