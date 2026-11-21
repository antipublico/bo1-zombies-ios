#pragma once

#include <string>
#include <vector>

namespace bo1::tests
{
// Complete synthetic FastFile with a single uncompressed DEFLATE block.
// The inflated payload starts at file byte 19 and occupies 221 bytes.
// It contains no original game data and needs no deflate encoder to construct.
std::vector<unsigned char> NativeAssetFixture();

// Load and verify the actual native asset loader, including retained ownership
// after copies/moves. Creates temporaryPath and removes it on every exit.
bool CheckNativeAssetFixture(const char* temporaryPath, std::string& error);
}
