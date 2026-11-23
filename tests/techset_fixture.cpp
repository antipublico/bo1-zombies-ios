#include "techset_fixture.h"
#include <database/fastfile_assets.h>
#include <platform/bo1_bytecode_io.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace bo1::tests
{
namespace
{
using Bytes = std::vector<unsigned char>;
constexpr uint32_t Inline = UINT32_MAX;
constexpr uint32_t Insert = UINT32_MAX - 1;
constexpr uint32_t Block4(uint32_t offset) { return 0x80000001u + offset; }
void U16(Bytes& bytes, uint16_t value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}
void U32(Bytes& bytes, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}
void Put32(Bytes& bytes, size_t offset, uint32_t value)
{
    bytecode::Store<uint32_t>(bytes.data() + offset, value);
}
bool Matches(const char* actual, const char* expected)
{
    return actual && std::strcmp(actual, expected) == 0;
}

bool Verify(const fastfile::AssetPrefix& loaded, std::string& error)
{
    const auto check = [&error](bool condition, const char* reason) {
        if (!condition) error = std::string("Technique fixture: ") + reason;
        return condition;
    };
    if (!check(loaded.stop == fastfile::AssetStop::Complete && loaded.entries.size() == 4
        && loaded.nextAsset == 4, "wrong materialized count")
        || !check(loaded.inspection.metadataBytes == 84
            && loaded.inspection.metadataBytes + loaded.decodedAssetBytes == loaded.inspection.expandedBytes,
            "stream alignment changed the file cursor")) return false;
    const auto* set = loaded.entries[0].techniqueSet;
    const auto* shared = loaded.entries[2].techniqueSet;
    if (!check(set && shared && set != shared && loaded.entries[0].type == 7
        && !loaded.entries[0].alias && loaded.entries[1].alias
        && loaded.entries[1].techniqueSet == set && !loaded.entries[2].alias,
        "inserted root alias or native set identity differs")
        || !check(Matches(set->name, "fixture_set") && set->worldVertFormat == 3
            && set->unused[0] == 0xA5 && set->techsetFlags == 0x9182
            && Matches(shared->name, "shared_set") && shared->worldVertFormat == 4
            && shared->techsetFlags == 0x4321, "set scalar fields differ")) return false;
    const auto* technique = set->techniques[0];
    if (!check(technique && technique == set->techniques[1] && technique == set->techniques[129]
        && technique == shared->techniques[0] && technique == shared->techniques[129]
        && !set->techniques[128] && !shared->techniques[1], "technique references lost shared identity")
        || !check(Matches(technique->name, "fixture_technique") && technique->flags == 0x8123
            && technique->passCount == 2, "technique header differs")) return false;
    const auto* first = fastfile::TechniquePassAt(*technique, 0);
    const auto* second = fastfile::TechniquePassAt(*technique, 1);
    if (!check(first && second && !fastfile::TechniquePassAt(*technique, 2), "pass access bounds differ")
        || !check(first->perPrimArgCount == 1 && first->perObjArgCount == 2
            && first->stableArgCount == 3 && first->customSamplerFlags == 0x81
            && second->perPrimArgCount == 1 && second->perObjArgCount == 0
            && second->stableArgCount == 0 && second->customSamplerFlags == 0x42,
            "pass scalar fields differ")) return false;
    const auto* declaration = first->vertexDecl;
    const auto* vertex = first->vertexShader;
    const auto* pixel = first->pixelShader;
    if (!check(declaration && vertex && pixel && second->vertexDecl == declaration
        && second->vertexShader == vertex && second->pixelShader == pixel,
        "shared shader or declaration pointers differ")
        || !check(declaration->streamCount == 2 && declaration->hasOptionalSource
            && !declaration->isLoaded, "declaration flags imply an initialized renderer")) return false;
    for (size_t i = 0; i < 16; ++i)
        if (!check(declaration->routing.data[i].source == i
            && declaration->routing.data[i].dest == 15 - i, "vertex routing bytes differ")) return false;
    for (auto* handle : declaration->routing.decl)
        if (!check(!handle, "serialized vertex declaration became a GPU pointer")) return false;
    if (!check(Matches(vertex->name, "fixture_vs") && !vertex->prog.vs
        && vertex->prog.loadDef.programSize == 3 && vertex->prog.loadDef.program,
        "vertex shader header differs")
        || !check(Matches(pixel->name, "fixture_ps") && !pixel->prog.ps
            && pixel->prog.loadDef.programSize == 2 && pixel->prog.loadDef.program,
            "pixel shader header differs")
        || !check(vertex->prog.loadDef.program[0] == 0xfffe0300u
            && vertex->prog.loadDef.program[1] == 0x12345678u
            && vertex->prog.loadDef.program[2] == 0x0000ffffu
            && pixel->prog.loadDef.program[0] == 0xffff0300u
            && pixel->prog.loadDef.program[1] == 0x0000ffffu,
            "four-byte shader tokens differ")) return false;
    const auto* args = first->args;
    if (!check(args && second->args && args != second->args, "argument arrays missing")
        || !check(args[0].type == 1 && args[0].dest == 9 && args[0].u.literalConst
            && args[1].type == 7 && args[1].dest == 10
            && args[0].u.literalConst == args[1].u.literalConst
            && second->args[0].type == 7 && second->args[0].dest == 21
            && second->args[0].u.literalConst == args[0].u.literalConst,
            "literal aliases lost identity")
        || !check(args[0].u.literalConst[0] == 1.25f && args[0].u.literalConst[1] == -2.0f
            && args[0].u.literalConst[2] == 0.0f && args[0].u.literalConst[3] == 42.0f,
            "literal values differ")) return false;
    uint32_t negativeZero;
    std::memcpy(&negativeZero, args[0].u.literalConst + 2, sizeof(negativeZero));
    if (!check(negativeZero == 0x80000000u, "literal negative-zero bits differ")
        || !check(args[2].type == 3 && args[2].dest == 11 && args[2].u.codeConst.index == 0x1234
            && args[2].u.codeConst.firstRow == 2 && args[2].u.codeConst.rowCount == 3
            && args[3].type == 5 && args[3].dest == 12 && args[3].u.codeConst.index == 0x5678
            && args[3].u.codeConst.firstRow == 1 && args[3].u.codeConst.rowCount == 4,
            "code-constant union fields differ")
        || !check(args[4].type == 4 && args[4].dest == 13 && args[4].u.codeSampler == 0x89abcdefu
            && args[5].type == 0 && args[5].dest == 14 && args[5].u.nameHash == 0x76543210u,
            "scalar argument union fields differ")) return false;
    const auto* raw = loaded.entries[3].rawfile;
    return check(loaded.entries[3].type == 36 && raw && Matches(raw->name, "after.txt")
        && raw->len == 4 && raw->buffer && std::memcmp(raw->buffer, "end!", 5) == 0,
        "asset after technique graph was not reconstructed");
}
}

Bytes TechsetFixture(TechsetFixtureOffsets* outputOffsets)
{
    TechsetFixtureOffsets offsets;
    Bytes payload(52, 0);
    Put32(payload, 8, 528); // Largest root TEMP header, not the sum of roots.
    Put32(payload, 44, 4); Put32(payload, 48, Inline);
    offsets.firstRoot = payload.size(); U32(payload, 7); U32(payload, Insert);
    offsets.aliasRoot = payload.size(); U32(payload, 7); U32(payload, Block4(32));
    offsets.secondRoot = payload.size(); U32(payload, 7); U32(payload, Inline);
    offsets.rawRoot = payload.size(); U32(payload, 36); U32(payload, Inline);
    uint32_t block = 36; // Directory 32 + inserted alias slot 4.
    const auto reserve = [&](uint32_t bytes, uint32_t alignment = 4) {
        block = (block + alignment - 1) & ~(alignment - 1);
        const auto start = block;
        block += bytes;
        return start;
    };
    const auto text = [&](const char* value) {
        const auto size = std::strlen(value) + 1;
        payload.insert(payload.end(), value, value + size);
        block += static_cast<uint32_t>(size);
    };
    const auto setHeader = [&](uint8_t format, uint16_t flags) {
        const auto at = payload.size();
        payload.resize(at + 528, 0);
        Put32(payload, at, Inline);
        payload[at + 4] = format; payload[at + 5] = 0xA5;
        bytecode::Store<uint16_t>(payload.data() + at + 6, flags);
        return at;
    };
    offsets.firstSet = setHeader(3, 0x9182);
    text("fixture_set");
    offsets.techniqueBlock = reserve(8);
    Put32(payload, offsets.firstSet + 8, Inline);
    Put32(payload, offsets.firstSet + 8 + 4, Block4(offsets.techniqueBlock));
    Put32(payload, offsets.firstSet + 8 + 129 * 4, Block4(offsets.techniqueBlock));
    offsets.technique = payload.size();
    U32(payload, Inline); U16(payload, 0x8123); U16(payload, 2);
    reserve(40);
    offsets.passes = payload.size();
    payload.resize(payload.size() + 40, 0);
    Put32(payload, offsets.passes, Inline);
    Put32(payload, offsets.passes + 4, Inline);
    Put32(payload, offsets.passes + 8, Inline);
    payload[offsets.passes + 12] = 1; payload[offsets.passes + 13] = 2;
    payload[offsets.passes + 14] = 3; payload[offsets.passes + 15] = 0x81;
    Put32(payload, offsets.passes + 16, 1); // Presence marker, not an alias.
    payload[offsets.passes + 32] = 1; payload[offsets.passes + 35] = 0x42;
    Put32(payload, offsets.passes + 36, Inline);
    offsets.declarationBlock = reserve(108);
    offsets.declaration = payload.size();
    payload.resize(payload.size() + 108, 0);
    payload[offsets.declaration] = 2;
    payload[offsets.declaration + 1] = 1;
    payload[offsets.declaration + 2] = 1; // Serialized runtime state must be reset.
    for (uint32_t i = 0; i < 16; ++i)
    {
        payload[offsets.declaration + 4 + i * 2] = static_cast<unsigned char>(i);
        payload[offsets.declaration + 5 + i * 2] = static_cast<unsigned char>(15 - i);
    }
    for (uint32_t i = 0; i < 18; ++i)
        Put32(payload, offsets.declaration + 36 + i * 4, 0x10203040u + i);
    offsets.vertexShaderBlock = reserve(16);
    offsets.vertexShader = payload.size();
    U32(payload, Inline); U32(payload, 0x13579bdf); U32(payload, 1); U16(payload, 3); U16(payload, 0x5aa5);
    text("fixture_vs"); reserve(12);
    offsets.vertexProgram = payload.size();
    U32(payload, 0xfffe0300u); U32(payload, 0x12345678u); U32(payload, 0x0000ffffu);
    offsets.pixelShaderBlock = reserve(16);
    offsets.pixelShader = payload.size();
    U32(payload, Inline); U32(payload, 0x2468ace0); U32(payload, Inline); U16(payload, 2); U16(payload, 0xa55a);
    text("fixture_ps"); reserve(8);
    offsets.pixelProgram = payload.size();
    U32(payload, 0xffff0300u); U32(payload, 0x0000ffffu);
    reserve(48); offsets.arguments = payload.size();
    U16(payload, 1); U16(payload, 9); U32(payload, Inline);
    // Literal address is known before its bytes are appended.
    offsets.literalBlock = (block + 3) & ~3u;
    U16(payload, 7); U16(payload, 10); U32(payload, Block4(offsets.literalBlock));
    U16(payload, 3); U16(payload, 11); U16(payload, 0x1234); payload.push_back(2); payload.push_back(3);
    U16(payload, 5); U16(payload, 12); U16(payload, 0x5678); payload.push_back(1); payload.push_back(4);
    U16(payload, 4); U16(payload, 13); U32(payload, 0x89abcdef);
    U16(payload, 0); U16(payload, 14); U32(payload, 0x76543210);
    reserve(16); offsets.literal = payload.size();
    U32(payload, 0x3fa00000); U32(payload, 0xc0000000); U32(payload, 0x80000000); U32(payload, 0x42280000);
    Put32(payload, offsets.passes + 20, Block4(offsets.declarationBlock));
    Put32(payload, offsets.passes + 24, Block4(offsets.vertexShaderBlock));
    Put32(payload, offsets.passes + 28, Block4(offsets.pixelShaderBlock));
    reserve(8); offsets.secondArguments = payload.size();
    U16(payload, 7); U16(payload, 21); U32(payload, Block4(offsets.literalBlock));
    text("fixture_technique");
    offsets.secondSet = setHeader(4, 0x4321);
    Put32(payload, offsets.secondSet + 8, Block4(offsets.techniqueBlock));
    Put32(payload, offsets.secondSet + 8 + 129 * 4, Block4(offsets.techniqueBlock));
    text("shared_set");
    U32(payload, Inline); U32(payload, 4); U32(payload, Inline);
    text("after.txt"); text("end!");
    offsets.block4Size = block;
    Put32(payload, 0, static_cast<uint32_t>(payload.size() - 36));
    Put32(payload, 24, block);
    if (outputOffsets) *outputOffsets = offsets;

    Bytes file = {'I','W','f','f','u','1','0','0'};
    U32(file, 473); file.push_back(0x78); file.push_back(1); file.push_back(1);
    U16(file, static_cast<uint16_t>(payload.size())); U16(file, static_cast<uint16_t>(~payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
    uint32_t a = 1, b = 0;
    for (auto byte : payload) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    const auto checksum = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8) file.push_back(static_cast<unsigned char>(checksum >> shift));
    return file;
}

bool CheckTechsetFixture(const char* temporaryPath, std::string& error)
{
    error.clear();
    if (!temporaryPath) { error = "Technique fixture: missing temporary path"; return false; }
    struct RemoveFile { const char* path; ~RemoveFile() { std::remove(path); } } cleanup{temporaryPath};
    const auto bytes = TechsetFixture();
    {
        std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(temporaryPath, "wb"), &std::fclose);
        if (!file || std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()
            || std::fflush(file.get()) != 0)
        { error = "Technique fixture: could not write temporary file"; return false; }
    }
    fastfile::AssetPrefix original;
    if (fastfile::LoadAssetPrefix(temporaryPath, original, error) != fastfile::InspectionResult::Ok
        || !Verify(original, error)) return false;
    const auto* retained = original.entries[0].techniqueSet;
    fastfile::AssetPrefix copied(original);
    original = {};
    if (!Verify(copied, error) || copied.entries[0].techniqueSet != retained) return false;
    fastfile::AssetPrefix moved(std::move(copied));
    copied = {};
    if (!Verify(moved, error)) return false;
    fastfile::AssetPrefix assigned;
    assigned = moved;
    moved.entries[0].techniqueSet = nullptr;
    if (!Verify(assigned, error)) return false;
    moved = {};
    copied = std::move(assigned);
    assigned = {};
    return Verify(copied, error);
}
}
