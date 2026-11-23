#pragma once

#include <platform/bo1_bytecode_io.h>
#include <cstddef>
#include <cstdint>

// The on-disk layout comes from xanim/xanim.h (XSurface, XSurfaceVertexInfo)
// and db_load.cpp (Load_XSurfaceArray). Serialized fields are Win32-sized,
// independent of the host iOS ARM64 ABI. This only decodes CPU metadata;
// it does not allocate geometry, dereference tags, or create GPU resources.
namespace bo1::fastfile
{
constexpr std::size_t XSurfaceDiskHeaderSize = 68;
constexpr std::uint32_t XSurfaceInlineTag = UINT32_MAX;

struct XSurfaceHeader
{
    std::uint8_t tileMode = 0;
    std::uint8_t vertListCount = 0;
    std::uint16_t flags = 0;
    std::uint16_t vertCount = 0;
    std::uint16_t triCount = 0;
    std::uint16_t baseTriIndex = 0;
    std::uint16_t baseVertIndex = 0;
    std::uint32_t triIndicesTag = 0;
    std::int16_t blendVertCounts[4]{};
    std::uint32_t vertsBlendTag = 0;
    std::uint32_t tensionDataTag = 0;
    std::uint32_t packedVerticesTag = 0;
    std::uint32_t serializedVertexBuffer = 0;
    std::uint32_t rigidVertListTag = 0;
    std::uint32_t serializedIndexBuffer = 0;
    std::int32_t partBits[5]{};
};

// Sizes of consecutive inline child arrays *only*. Does not include padding,
// nested collision-tree allocations or referenced/shared children.
struct XSurfaceInlineBytes
{
    std::uint64_t blendIndices = 0;
    std::uint64_t tensionData = 0;
    std::uint64_t packedVertices = 0;
    std::uint64_t rigidLists = 0;
    std::uint64_t triangleIndices = 0;
};

inline bool DecodeXSurfaceHeader(const unsigned char* disk, std::size_t bytes,
    XSurfaceHeader& out) noexcept
{
    out = {};
    if (!disk || bytes < XSurfaceDiskHeaderSize) return false;
    XSurfaceHeader s{};
    s.tileMode = disk[0];
    s.vertListCount = disk[1];
    s.flags = bytecode::Load<std::uint16_t>(disk + 2);
    s.vertCount = bytecode::Load<std::uint16_t>(disk + 4);
    s.triCount = bytecode::Load<std::uint16_t>(disk + 6);
    s.baseTriIndex = bytecode::Load<std::uint16_t>(disk + 8);
    s.baseVertIndex = bytecode::Load<std::uint16_t>(disk + 10);
    s.triIndicesTag = bytecode::Load<std::uint32_t>(disk + 12);
    std::uint32_t weighted = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        s.blendVertCounts[i] = bytecode::Load<std::int16_t>(disk + 16 + i * 2);
        if (s.blendVertCounts[i] < 0) return false;
        weighted += static_cast<std::uint16_t>(s.blendVertCounts[i]);
    }
    if (weighted > s.vertCount) return false;
    s.vertsBlendTag = bytecode::Load<std::uint32_t>(disk + 24);
    s.tensionDataTag = bytecode::Load<std::uint32_t>(disk + 28);
    s.packedVerticesTag = bytecode::Load<std::uint32_t>(disk + 32);
    s.serializedVertexBuffer = bytecode::Load<std::uint32_t>(disk + 36);
    s.rigidVertListTag = bytecode::Load<std::uint32_t>(disk + 40);
    s.serializedIndexBuffer = bytecode::Load<std::uint32_t>(disk + 44);
    for (std::size_t i = 0; i < 5; ++i)
        s.partBits[i] = bytecode::Load<std::int32_t>(disk + 48 + i * 4);
    out = s;
    return true;
}

// Every term is bounded by the native 16-bit counts, so uint64_t arithmetic is
// exact. The weights (1,3,5,7), tension (12 floats per skinned vertex),
// vertex size (32), rigid list size (12), and tri index (3 u16 per triangle)
// follow Load_XSurfaceVertexInfo and Load_XSurface in db_load.cpp.
inline XSurfaceInlineBytes CalculateXSurfaceInlineBytes(const XSurfaceHeader& s) noexcept
{
    XSurfaceInlineBytes bytes{};
    const auto blendWords = std::uint64_t(s.blendVertCounts[0])
        + 3ull * s.blendVertCounts[1]
        + 5ull * s.blendVertCounts[2]
        + 7ull * s.blendVertCounts[3];
    const auto skinnedVerts = std::uint64_t(s.blendVertCounts[0])
        + s.blendVertCounts[1] + s.blendVertCounts[2] + s.blendVertCounts[3];
    if (s.vertsBlendTag == XSurfaceInlineTag) bytes.blendIndices = blendWords * 2;
    if (s.tensionDataTag == XSurfaceInlineTag) bytes.tensionData = skinnedVerts * 48;
    if (s.packedVerticesTag == XSurfaceInlineTag) bytes.packedVertices = std::uint64_t(s.vertCount) * 32;
    if (s.rigidVertListTag == XSurfaceInlineTag) bytes.rigidLists = std::uint64_t(s.vertListCount) * 12;
    if (s.triIndicesTag == XSurfaceInlineTag) bytes.triangleIndices = std::uint64_t(s.triCount) * 6;
    return bytes;
}
}
