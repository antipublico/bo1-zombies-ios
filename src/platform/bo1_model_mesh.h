#pragma once

#include <database/fastfile_assets.h>
#include <platform/bo1_bytecode_io.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// CPU-only, bounded XModel LOD0 extraction. Vertex serialization matches
// xanim/xanim.h GfxPackedVertex (32 bytes): float xyz @0, PackedTexCoords @20;
// triIndices are 3 little-endian u16 indices per triangle. T5 coordinates are
// VU-packed IEEE754 half floats (low16=V, high16=U), not native floats.
// No engine registration, animation, shader execution or D3D buffers.
namespace bo1::model
{
struct Vertex
{
    float x = 0, y = 0, z = 0, u = 0, v = 0;
};
static_assert(sizeof(Vertex) == 20, "Portable preview vertex ABI changed");

struct Mesh
{
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    float mins[3]{};
    float maxs[3]{};
    std::size_t surfaceCount = 0;
};

inline float HalfToFloat(std::uint16_t half) noexcept
{
    const float sign = (half & 0x8000u) ? -1.0f : 1.0f;
    const unsigned mantissa = half & 0x03ffu;
    const unsigned exponent = (half >> 10) & 31u;
    if (exponent == 0) return sign * std::ldexp(float(mantissa), -24);
    if (exponent == 31) return mantissa ? std::nanf("") : sign * INFINITY;
    return sign * std::ldexp(float(1024u + mantissa), int(exponent) - 25);
}

inline bool ExtractLOD0(const bo1::fastfile::AssetPrefix& prefix,
    Mesh& output, std::string& error)
{
    output = {};
    error.clear();
    if (!prefix.pendingXModel || !prefix.pendingXModelSkeleton)
    {
        error = "El FF aún no contiene un XModel con geometría decodificada.";
        return false;
    }
    const auto& header = *prefix.pendingXModel;
    const auto& preview = *prefix.pendingXModelSkeleton;
    if (!header.numLods || !header.lods[0].surfaceCount)
    {
        error = "El primer XModel no declara superficies LOD0.";
        return false;
    }
    const auto begin = std::size_t(header.lods[0].firstSurface);
    const auto count = std::size_t(header.lods[0].surfaceCount);
    if (begin > preview.surfaceHeaders.size() ||
        count > preview.surfaceHeaders.size() - begin ||
        begin > preview.surfaceBodies.size() ||
        count > preview.surfaceBodies.size() - begin)
    {
        error = "Faltan cabeceras o cuerpos XSurface para el LOD0.";
        return false;
    }
    Mesh mesh{};
    mesh.name = preview.name;
    bool first = true;
    constexpr std::size_t MaxVertices = 131072;
    constexpr std::size_t MaxTriangles = 262144;
    for (std::size_t surfaceIndex = begin; surfaceIndex < begin + count; ++surfaceIndex)
    {
        const auto& s = preview.surfaceHeaders[surfaceIndex];
        const auto& body = preview.surfaceBodies[surfaceIndex];
        // No skinning or external shared vertex streams until the engine's
        // bone transforms and pointer alias registry can be reconstructed.
        if (s.flags & (0x0002u | 0x8000u))
        {
            error = "Superficie con skinning/streaming sin soporte 3D aún.";
            return false;
        }
        if (!s.vertCount || !s.triCount || body.unresolvedFields ||
            body.packedVertices.size() != std::size_t(s.vertCount) * 32 ||
            body.triangleIndices.size() != std::size_t(s.triCount) * 6)
        {
            error = "La superficie necesita datos inline completos de vértices e índices.";
            return false;
        }
        if (mesh.vertices.size() + s.vertCount > MaxVertices ||
            mesh.indices.size() / 3 + s.triCount > MaxTriangles)
        {
            error = "Modelo demasiado grande para la vista 3D de diagnóstico.";
            return false;
        }
        const std::uint32_t base = std::uint32_t(mesh.vertices.size());
        for (std::size_t v = 0; v < s.vertCount; ++v)
        {
            const auto* bytes = body.packedVertices.data() + v * 32;
            Vertex out{};
            out.x = bo1::bytecode::Load<float>(bytes + 0);
            out.y = bo1::bytecode::Load<float>(bytes + 4);
            out.z = bo1::bytecode::Load<float>(bytes + 8);
            const auto packed = bo1::bytecode::Load<std::uint32_t>(bytes + 20);
            out.u = HalfToFloat(std::uint16_t(packed >> 16));
            out.v = HalfToFloat(std::uint16_t(packed & 0xffffu));
            if (!std::isfinite(out.x) || !std::isfinite(out.y) ||
                !std::isfinite(out.z) || !std::isfinite(out.u) ||
                !std::isfinite(out.v) || std::fabs(out.x) > 1000000.f ||
                std::fabs(out.y) > 1000000.f || std::fabs(out.z) > 1000000.f)
            {
                error = "Vértices o coordenadas UV inválidos en la superficie.";
                return false;
            }
            const float values[3] = {out.x, out.y, out.z};
            for (int axis = 0; axis < 3; ++axis)
            {
                if (first)
                {
                    mesh.mins[axis] = values[axis];
                    mesh.maxs[axis] = values[axis];
                }
                else
                {
                    if (values[axis] < mesh.mins[axis]) mesh.mins[axis] = values[axis];
                    if (values[axis] > mesh.maxs[axis]) mesh.maxs[axis] = values[axis];
                }
            }
            first = false;
            mesh.vertices.push_back(out);
        }
        for (std::size_t t = 0; t < std::size_t(s.triCount) * 3; ++t)
        {
            const auto idx = bo1::bytecode::Load<std::uint16_t>(
                body.triangleIndices.data() + t * 2);
            if (idx >= s.vertCount)
            {
                error = "Índice de triángulo fuera del rango de XSurface.";
                return false;
            }
            mesh.indices.push_back(base + idx);
        }
        ++mesh.surfaceCount;
    }
    if (mesh.indices.empty())
    {
        error = "El modelo no contiene triángulos LOD0.";
        return false;
    }
    output = std::move(mesh);
    return true;
}
}
