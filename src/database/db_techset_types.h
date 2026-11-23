#pragma once

#include <cstddef>
#include <cstdint>

// Native runtime types extracted from gfx_d3d/r_material.h. FastFile bodies
// retain Win32 pointer tags and must be decoded field by field on a 64-bit ABI.
// TechniqueSet layout checks follow ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e, src/database/db_techset_types.h.

struct IDirect3DVertexDeclaration9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;

// Preserve the exact DWORD pointee type used by the Windows renderer while
// keeping shader tokens four bytes on LP64 hosts such as iOS.
#if defined(_WIN32)
using Bo1ShaderWord = unsigned long;
#else
using Bo1ShaderWord = std::uint32_t;
#endif
static_assert(sizeof(Bo1ShaderWord) == 4, "Shader tokens retain four bytes");
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "Supported native pointer widths");
static_assert(sizeof(bool) == 1, "Vertex declaration flags retain one byte");

struct MaterialStreamRouting
{
    std::uint8_t source;
    std::uint8_t dest;
};

struct MaterialVertexStreamRouting
{
    MaterialStreamRouting data[16];
    IDirect3DVertexDeclaration9* decl[18];
};

struct MaterialVertexDeclaration
{
    std::uint8_t streamCount;
    bool hasOptionalSource;
    bool isLoaded;
    MaterialVertexStreamRouting routing;
};

struct GfxVertexShaderLoadDef
{
    Bo1ShaderWord* program;
    std::uint16_t programSize;
};

struct MaterialVertexShaderProgram
{
    IDirect3DVertexShader9* vs;
    GfxVertexShaderLoadDef loadDef;
};

struct MaterialVertexShader
{
    const char* name;
    MaterialVertexShaderProgram prog;
};

struct GfxPixelShaderLoadDef
{
    Bo1ShaderWord* program;
    std::uint16_t programSize;
};

struct MaterialPixelShaderProgram
{
    IDirect3DPixelShader9* ps;
    GfxPixelShaderLoadDef loadDef;
};

struct MaterialPixelShader
{
    const char* name;
    MaterialPixelShaderProgram prog;
};

struct MaterialArgumentCodeConst
{
    std::uint16_t index;
    std::uint8_t firstRow;
    std::uint8_t rowCount;
};

union MaterialArgumentDef
{
    const float* literalConst;
    MaterialArgumentCodeConst codeConst;
    std::uint32_t codeSampler;
    std::uint32_t nameHash;
};

struct MaterialShaderArgument
{
    std::uint16_t type;
    std::uint16_t dest;
    MaterialArgumentDef u;
};

struct MaterialPass
{
    MaterialVertexDeclaration* vertexDecl;
    MaterialVertexShader* vertexShader;
    union
    {
        MaterialPixelShader* pixelShader;
        MaterialPixelShader* localPixelShader;
    };
    std::uint8_t perPrimArgCount;
    std::uint8_t perObjArgCount;
    std::uint8_t stableArgCount;
    std::uint8_t customSamplerFlags;
    union
    {
        MaterialShaderArgument* localArgs;
        MaterialShaderArgument* args;
    };
};

struct MaterialTechnique
{
    const char* name;
    std::uint16_t flags;
    std::uint16_t passCount;
    // The engine allocates a trailing pass array. On 64-bit hosts the array
    // starts at byte 16; the serialized technique prefix is always eight bytes.
    MaterialPass passArray[1];
};

struct MaterialTechniqueSet
{
    const char* name;
    std::uint8_t worldVertFormat;
    std::uint8_t unused[1];
    std::uint16_t techsetFlags;
    MaterialTechnique* techniques[130];
};

static_assert(sizeof(MaterialStreamRouting) == 2);
static_assert(offsetof(MaterialVertexStreamRouting, data) == 0);
static_assert(offsetof(MaterialVertexStreamRouting, decl) == 32);
static_assert(sizeof(MaterialVertexStreamRouting) == (sizeof(void*) == 8 ? 176 : 104));
static_assert(offsetof(MaterialVertexDeclaration, streamCount) == 0);
static_assert(offsetof(MaterialVertexDeclaration, hasOptionalSource) == 1);
static_assert(offsetof(MaterialVertexDeclaration, isLoaded) == 2);
static_assert(offsetof(MaterialVertexDeclaration, routing) == sizeof(void*));
static_assert(sizeof(MaterialVertexDeclaration) == (sizeof(void*) == 8 ? 184 : 108));

static_assert(offsetof(GfxVertexShaderLoadDef, program) == 0);
static_assert(offsetof(GfxVertexShaderLoadDef, programSize) == sizeof(void*));
static_assert(sizeof(GfxVertexShaderLoadDef) == (sizeof(void*) == 8 ? 16 : 8));
static_assert(offsetof(GfxPixelShaderLoadDef, program) == 0);
static_assert(offsetof(GfxPixelShaderLoadDef, programSize) == sizeof(void*));
static_assert(sizeof(GfxPixelShaderLoadDef) == (sizeof(void*) == 8 ? 16 : 8));
static_assert(offsetof(MaterialVertexShaderProgram, vs) == 0);
static_assert(offsetof(MaterialVertexShaderProgram, loadDef) == sizeof(void*));
static_assert(sizeof(MaterialVertexShaderProgram) == (sizeof(void*) == 8 ? 24 : 12));
static_assert(offsetof(MaterialPixelShaderProgram, ps) == 0);
static_assert(offsetof(MaterialPixelShaderProgram, loadDef) == sizeof(void*));
static_assert(sizeof(MaterialPixelShaderProgram) == (sizeof(void*) == 8 ? 24 : 12));
static_assert(offsetof(MaterialVertexShader, name) == 0);
static_assert(offsetof(MaterialVertexShader, prog) == sizeof(void*));
static_assert(sizeof(MaterialVertexShader) == (sizeof(void*) == 8 ? 32 : 16));
static_assert(offsetof(MaterialPixelShader, name) == 0);
static_assert(offsetof(MaterialPixelShader, prog) == sizeof(void*));
static_assert(sizeof(MaterialPixelShader) == (sizeof(void*) == 8 ? 32 : 16));

static_assert(sizeof(MaterialArgumentCodeConst) == 4);
static_assert(offsetof(MaterialArgumentCodeConst, index) == 0);
static_assert(offsetof(MaterialArgumentCodeConst, firstRow) == 2);
static_assert(offsetof(MaterialArgumentCodeConst, rowCount) == 3);
static_assert(sizeof(MaterialArgumentDef) == sizeof(void*));
static_assert(offsetof(MaterialShaderArgument, type) == 0);
static_assert(offsetof(MaterialShaderArgument, dest) == 2);
static_assert(offsetof(MaterialShaderArgument, u) == sizeof(void*));
static_assert(sizeof(MaterialShaderArgument) == (sizeof(void*) == 8 ? 16 : 8));
static_assert(offsetof(MaterialPass, vertexDecl) == 0);
static_assert(offsetof(MaterialPass, vertexShader) == sizeof(void*));
static_assert(offsetof(MaterialPass, pixelShader) == 2 * sizeof(void*));
static_assert(offsetof(MaterialPass, perPrimArgCount) == 3 * sizeof(void*));
static_assert(offsetof(MaterialPass, perObjArgCount) == 3 * sizeof(void*) + 1);
static_assert(offsetof(MaterialPass, stableArgCount) == 3 * sizeof(void*) + 2);
static_assert(offsetof(MaterialPass, customSamplerFlags) == 3 * sizeof(void*) + 3);
static_assert(offsetof(MaterialPass, args) == 4 * sizeof(void*));
static_assert(sizeof(MaterialPass) == (sizeof(void*) == 8 ? 40 : 20));

static_assert(offsetof(MaterialTechnique, name) == 0);
static_assert(offsetof(MaterialTechnique, flags) == sizeof(void*));
static_assert(offsetof(MaterialTechnique, passCount) == sizeof(void*) + 2);
static_assert(offsetof(MaterialTechnique, passArray) == 2 * sizeof(void*));
static_assert(sizeof(MaterialTechnique) == (sizeof(void*) == 8 ? 56 : 28));
static_assert(offsetof(MaterialTechniqueSet, name) == 0);
static_assert(offsetof(MaterialTechniqueSet, worldVertFormat) == sizeof(void*));
static_assert(offsetof(MaterialTechniqueSet, techsetFlags) == sizeof(void*) + 2);
static_assert(offsetof(MaterialTechniqueSet, techniques) == 2 * sizeof(void*));
static_assert(sizeof(MaterialTechniqueSet) == (sizeof(void*) == 8 ? 1056 : 528));
