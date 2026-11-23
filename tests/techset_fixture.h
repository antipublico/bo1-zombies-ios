#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bo1::tests
{
// Positions in the inflated synthetic payload, for corruption tests. Block
// offsets are tracked independently: stream alignment adds no file bytes.
struct TechsetFixtureOffsets
{
    size_t firstRoot = 0, aliasRoot = 0, secondRoot = 0, rawRoot = 0;
    size_t firstSet = 0, technique = 0, passes = 0, declaration = 0;
    size_t vertexShader = 0, pixelShader = 0, vertexProgram = 0, pixelProgram = 0;
    size_t arguments = 0, literal = 0, secondArguments = 0, secondSet = 0;
    uint32_t techniqueBlock = 0, declarationBlock = 0, vertexShaderBlock = 0;
    uint32_t pixelShaderBlock = 0, literalBlock = 0, block4Size = 0;
};

// No game data: two shared technique sets, an inserted root alias, and a rawfile
// after the graph. The zlib stored-block payload starts at file byte 19.
std::vector<unsigned char> TechsetFixture(TechsetFixtureOffsets* offsets = nullptr);

// Shared host/iOS self-test. Removes temporaryPath after all exits.
bool CheckTechsetFixture(const char* temporaryPath, std::string& error);
}
