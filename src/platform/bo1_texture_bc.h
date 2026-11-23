#pragma once

#include <platform/bo1_bytecode_io.h>
#include <cstddef>
#include <cstdint>
#include <vector>

// Standalone, CPU-only decoder for Direct3D DXT3/BC2 texture blocks.
// No Win32 types, GPU APIs, platform byte order assumptions or game data.
// This converts known compressed bytes to RGBA8; it does not find externally
// streamed texture data or upload textures to an iOS GPU.
namespace bo1::texture
{
inline constexpr std::uint32_t DXT1 = 0x31545844u; // "DXT1" little-endian FourCC
inline constexpr std::uint32_t DXT3 = 0x33545844u; // "DXT3" = BC2
inline constexpr std::uint32_t DXT5 = 0x35545844u; // "DXT5" = BC3
inline constexpr std::uint64_t MaxRGBABytes = 64ull * 1024 * 1024;

inline const char* FormatName(std::uint32_t fourcc) noexcept
{
    switch (fourcc)
    {
    case DXT1: return "DXT1 / BC1";
    case DXT3: return "DXT3 / BC2";
    case DXT5: return "DXT5 / BC3";
    default: return "unrecognized/other Direct3D format";
    }
}

inline bool BC2ByteSizes(std::uint32_t width, std::uint32_t height,
    std::size_t& compressed, std::size_t& rgba) noexcept
{
    compressed = rgba = 0;
    if (!width || !height) return false;
    const std::uint64_t out = std::uint64_t(width) * height * 4;
    const std::uint64_t blocksX = (std::uint64_t(width) + 3) / 4;
    const std::uint64_t blocksY = (std::uint64_t(height) + 3) / 4;
    const std::uint64_t src = blocksX * blocksY * 16;
    // Cap RGBA output to keep hostile file metadata from exhausting memory.
    if (out > MaxRGBABytes || src > MaxRGBABytes) return false;
    rgba = static_cast<std::size_t>(out);
    compressed = static_cast<std::size_t>(src);
    return true;
}

inline bool DecodeBC2RGBA(const std::uint8_t* blocks, std::size_t byteCount,
    std::uint32_t width, std::uint32_t height,
    std::vector<std::uint8_t>& destination)
{
    std::size_t expected = 0, outBytes = 0;
    if (!BC2ByteSizes(width, height, expected, outBytes)
        || !blocks || byteCount != expected)
        return false;

    std::vector<std::uint8_t> result(outBytes);
    const std::size_t blocksX = (std::size_t(width) + 3) / 4;
    const std::size_t blocksY = (std::size_t(height) + 3) / 4;
    for (std::size_t by = 0; by < blocksY; ++by)
        for (std::size_t bx = 0; bx < blocksX; ++bx)
        {
            const auto* block = blocks + (by * blocksX + bx) * 16;
            const auto c0 = bo1::bytecode::Load<std::uint16_t>(block + 8);
            const auto c1 = bo1::bytecode::Load<std::uint16_t>(block + 10);
            std::uint8_t colors[4][3]{};
            auto unpack = [](std::uint16_t c, std::uint8_t rgb[3])
            {
                const std::uint8_t r = static_cast<std::uint8_t>((c >> 11) & 31);
                const std::uint8_t g = static_cast<std::uint8_t>((c >> 5) & 63);
                const std::uint8_t b = static_cast<std::uint8_t>(c & 31);
                rgb[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
                rgb[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
                rgb[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
            };
            unpack(c0, colors[0]);
            unpack(c1, colors[1]);
            // BC2 always uses FOUR colors, including when c0 <= c1.
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                colors[2][channel] = static_cast<std::uint8_t>(
                    (2u * colors[0][channel] + colors[1][channel]) / 3);
                colors[3][channel] = static_cast<std::uint8_t>(
                    (colors[0][channel] + 2u * colors[1][channel]) / 3);
            }
            const auto indices = bo1::bytecode::Load<std::uint32_t>(block + 12);
            for (std::size_t y = 0; y < 4; ++y)
                for (std::size_t x = 0; x < 4; ++x)
                {
                    const std::size_t destX = bx * 4 + x;
                    const std::size_t destY = by * 4 + y;
                    if (destX >= width || destY >= height) continue;
                    const std::size_t pixel = y * 4 + x;
                    const unsigned int paletteIndex = (indices >> (2 * pixel)) & 3u;
                    const unsigned int alphaNibble =
                        (block[pixel / 2] >> (4 * (pixel % 2))) & 0xfu;
                    const std::size_t dest = (destY * width + destX) * 4;
                    result[dest] = colors[paletteIndex][0];
                    result[dest + 1] = colors[paletteIndex][1];
                    result[dest + 2] = colors[paletteIndex][2];
                    result[dest + 3] = static_cast<std::uint8_t>(alphaNibble * 17);
                }
        }

    destination.swap(result); // No partial decoded output on validation failure.
    return true;
}
}
