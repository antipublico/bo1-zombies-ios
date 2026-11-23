#include <platform/bo1_texture_bc.h>
#include <platform/bo1_bytecode_io.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
using Block = std::array<std::uint8_t, 16>;
Block Solid(std::uint16_t color, std::uint8_t alpha)
{
    Block b{};
    for (std::size_t i = 0; i < 8; ++i) b[i] = std::uint8_t((alpha << 4) | alpha);
    bo1::bytecode::Store<std::uint16_t>(b.data() + 8, color);
    bo1::bytecode::Store<std::uint16_t>(b.data() + 10, color);
    return b;
}
bool Pixel(const std::vector<std::uint8_t>& rgba, std::size_t width,
    std::size_t x, std::size_t y,
    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    const auto i = (y * width + x) * 4;
    return i + 3 < rgba.size() && rgba[i] == r && rgba[i + 1] == g
        && rgba[i + 2] == b && rgba[i + 3] == a;
}
}

int main()
{
    using namespace bo1::texture;
    Check(DXT3 == 861165636u && std::strcmp(FormatName(DXT3), "DXT3 / BC2") == 0,
        "real fastfile decimal format 861165636 is DXT3 / BC2");
    Check(std::strcmp(FormatName(DXT1), "DXT1 / BC1") == 0
        && std::strcmp(FormatName(DXT5), "DXT5 / BC3") == 0,
        "recognize other known Direct3D FourCC formats without decoding them");
    Check(std::strcmp(FormatName(0u), "unrecognized/other Direct3D format") == 0,
        "unknown format remains unknown");

    std::size_t compressed = 0, decoded = 0;
    Check(BC2ByteSizes(512, 512, compressed, decoded)
        && compressed == 262144 && decoded == 1048576,
        "512x512 BC2 requires 256 KiB blocks and 1 MiB RGBA");
    Check(BC2ByteSizes(5, 5, compressed, decoded)
        && compressed == 64 && decoded == 100,
        "partial edge blocks still consume full 4x4 BC2 blocks");
    Check(!BC2ByteSizes(0, 8, compressed, decoded)
        && !BC2ByteSizes(16384, 16384, compressed, decoded),
        "reject empty dimensions and oversized RGBA allocation");

    const auto red = Solid(0xf800, 15);
    std::vector<std::uint8_t> rgba;
    Check(DecodeBC2RGBA(red.data(), red.size(), 4, 4, rgba)
        && rgba.size() == 64, "opaque red block");
    Check(Pixel(rgba, 4, 0, 0, 255, 0, 0, 255)
        && Pixel(rgba, 4, 3, 3, 255, 0, 0, 255),
        "solid red RGBA output in every corner");

    auto blend = red;
    bo1::bytecode::Store<std::uint16_t>(blend.data() + 10, 0x07e0);
    bo1::bytecode::Store<std::uint32_t>(blend.data() + 12, 0xe4u);
    blend[0] = 0x80u; // pixel 0 alpha 0; pixel 1 alpha 8.
    blend[1] = 0x1fu; // pixel 2 alpha 15; pixel 3 alpha 1.
    Check(DecodeBC2RGBA(blend.data(), blend.size(), 4, 4, rgba),
        "decode mixed palette and independent four-bit alphas");
    Check(Pixel(rgba, 4, 0, 0, 255, 0, 0, 0),
        "palette index 0 and transparent four-bit alpha");
    Check(Pixel(rgba, 4, 1, 0, 0, 255, 0, 136),
        "palette index 1 and half alpha");
    Check(Pixel(rgba, 4, 2, 170, 85, 0, 255),
        "palette index 2 uses two-thirds first color");
    Check(Pixel(rgba, 4, 3, 85, 170, 0, 17),
        "palette index 3 uses two-thirds second color");

    auto reverse = blend;
    bo1::bytecode::Store<std::uint16_t>(reverse.data() + 8, 0x07e0);
    bo1::bytecode::Store<std::uint16_t>(reverse.data() + 10, 0xf800);
    Check(DecodeBC2RGBA(reverse.data(), reverse.size(), 4, 4, rgba)
        && Pixel(rgba, 4, 2, 85, 170, 0, 255)
        && Pixel(rgba, 4, 3, 170, 85, 0, 17),
        "BC2 uses four-color mode even when endpoint 0 < endpoint 1");

    const Block green = Solid(0x07e0, 8);
    const Block blue = Solid(0x001f, 4);
    const Block white = Solid(0xffff, 0);
    std::vector<std::uint8_t> tiled;
    for (const auto& tile : {red, green, blue, white})
        tiled.insert(tiled.end(), tile.begin(), tile.end());
    Check(DecodeBC2RGBA(tiled.data(), tiled.size(), 5, 5, rgba)
        && rgba.size() == 100, "clip BC2 blocks to 5x5 image dimensions");
    Check(Pixel(rgba, 5, 0, 0, 255, 0, 0, 255)
        && Pixel(rgba, 5, 4, 0, 0, 255, 0, 136)
        && Pixel(rgba, 5, 0, 4, 0, 0, 255, 68)
        && Pixel(rgba, 5, 4, 4, 255, 255, 255, 0),
        "2x2 BC2 block grid is traversed in row-major order");

    const std::vector<std::uint8_t> previous{1, 2, 3, 4};
    rgba = previous;
    Check(!DecodeBC2RGBA(nullptr, 16, 4, 4, rgba) && rgba == previous,
        "null compressed block data does not change output");
    Check(!DecodeBC2RGBA(red.data(), 15, 4, 4, rgba) && rgba == previous,
        "truncated compressed payload is rejected atomically");
    Check(!DecodeBC2RGBA(red.data(), 17, 4, 4, rgba) && rgba == previous,
        "surplus data is rejected atomically");
    Check(!DecodeBC2RGBA(red.data(), 16, 0, 4, rgba) && rgba == previous,
        "zero width is rejected atomically");

    std::printf("%u BC2 / DXT3 texture checks, %u failures\n", checks, failures);
    return failures != 0;
}
