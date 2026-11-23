#include <platform/bo1_model_mesh.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static bo1::fastfile::AssetPrefix MakeFixture()
{
    bo1::fastfile::AssetPrefix prefix{};
    bo1::fastfile::XModelHeader model{};
    model.numSurfaces = 1;
    model.numLods = 1;
    model.lods[0].firstSurface = 0;
    model.lods[0].surfaceCount = 1;
    prefix.pendingXModel = model;
    bo1::fastfile::XModelSkeletonPreview preview{};
    preview.name = "synthetic_quad";
    bo1::fastfile::XSurfaceHeader surface{};
    surface.vertCount = 4;
    surface.triCount = 2;
    surface.packedVerticesTag = UINT32_MAX;
    surface.triIndicesTag = UINT32_MAX;
    preview.surfaceHeaders.push_back(surface);
    bo1::fastfile::XSurfaceBodyPreview body{};
    body.packedVertices.resize(4 * 32);
    body.triangleIndices.resize(2 * 6);
    const float xyz[4][3]{{0,0,0},{2,0,0},{2,1,0},{0,1,0}};
    const std::uint16_t uv[4][2]{{0x0000,0x0000},{0x3c00,0x0000},{0x3c00,0x3c00},{0x0000,0x3c00}};
    for (int i = 0; i < 4; ++i)
    {
        auto* p = body.packedVertices.data() + i * 32;
        for (int axis = 0; axis < 3; ++axis)
            bo1::bytecode::Store<float>(p + axis * 4, xyz[i][axis]);
        bo1::bytecode::Store<std::uint32_t>(p + 20,
            (std::uint32_t(uv[i][0]) << 16) | uv[i][1]);
    }
    const std::uint16_t tri[]{0,1,2,0,2,3};
    for (int i = 0; i < 6; ++i)
        bo1::bytecode::Store<std::uint16_t>(body.triangleIndices.data() + 2 * i, tri[i]);
    preview.surfaceBodies.push_back(std::move(body));
    prefix.pendingXModelSkeleton = std::move(preview);
    return prefix;
}

int main()
{
    using namespace bo1::model;
    assert(HalfToFloat(0x3c00) == 1.0f);
    assert(HalfToFloat(0xbc00) == -1.0f);
    assert(HalfToFloat(0x3800) == 0.5f);
    assert(HalfToFloat(0x0000) == 0);
    assert(HalfToFloat(0x0001) > 0);
    assert(!std::isfinite(HalfToFloat(0x7c00)));
    assert(std::isnan(HalfToFloat(0x7e00)));
    Mesh out{};
    std::string error;
    auto fixture = MakeFixture();
    assert(ExtractLOD0(fixture, out, error));
    assert(out.name == "synthetic_quad");
    assert(out.surfaceCount == 1 && out.vertices.size() == 4 && out.indices.size() == 6);
    assert(out.vertices[2].x == 2 && out.vertices[2].y == 1);
    assert(out.vertices[2].u == 1 && out.vertices[2].v == 1);
    assert(out.vertices[3].u == 0 && out.vertices[3].v == 1);
    assert(out.indices[5] == 3 && out.maxs[0] == 2 && out.maxs[1] == 1);
    fixture.pendingXModelSkeleton->surfaceBodies[0].triangleIndices[0] = 9;
    assert(!ExtractLOD0(fixture, out, error) && out.indices.empty());
    fixture = MakeFixture();
    fixture.pendingXModelSkeleton->surfaceBodies[0].packedVertices.resize(3 * 32);
    assert(!ExtractLOD0(fixture, out, error));
    fixture = MakeFixture();
    fixture.pendingXModelSkeleton->surfaceBodies[0].unresolvedFields = 4;
    assert(!ExtractLOD0(fixture, out, error));
    fixture = MakeFixture();
    fixture.pendingXModelSkeleton->surfaceHeaders[0].flags = 2;
    assert(!ExtractLOD0(fixture, out, error));
    fixture = MakeFixture();
    bo1::bytecode::Store<float>(
        fixture.pendingXModelSkeleton->surfaceBodies[0].packedVertices.data(),
        std::numeric_limits<float>::quiet_NaN());
    assert(!ExtractLOD0(fixture, out, error));
    fixture = MakeFixture();
    fixture.pendingXModel->lods[0].firstSurface = 1;
    assert(!ExtractLOD0(fixture, out, error));
    std::puts("XModel packed LOD0 mesh extraction passed");
}
