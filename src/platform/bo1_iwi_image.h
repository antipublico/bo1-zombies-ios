#pragma once

#include <platform/bo1_bytecode_io.h>
#include <platform/bo1_texture_bc.h>
#include <array>
#include <cstddef>
#include <cstdint>

// The Win32 GfxImageFileHeader (r_material.h) is a 48-byte packed disk record.
// r_image_load_obj.cpp expects the IWi signature, version 13, and its
// fileSizeForPicmip[0] to equal the full file size. IWI format 12 is DXT3.
// The original Image_LoadDxtc reads mip levels smallest-to-largest, so the
// full-resolution 2D BC2 block array occupies the final bytes of the file.
namespace bo1::texture
{
inline constexpr std::size_t IwiHeaderBytes = 48;
inline constexpr std::uint8_t IwiVersion = 13;
inline constexpr std::uint8_t IwiFormatDXT1 = 11;
inline constexpr std::uint8_t IwiFormatDXT3 = 12;
inline constexpr std::uint8_t IwiFormatDXT5 = 13;

enum class IwiParseResult
{
    Ok,
    Truncated,
    NotIwi,
    UnsupportedVersion,
    InvalidDimensions,
    InvalidFileSize
};

struct IwiHeader
{
    std::uint8_t version = 0;
    std::uint8_t format = 0;
    std::uint8_t flags = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 0;
    float gamma = 0.0f;
    std::array<std::uint32_t, 8> fileSizeForPicmip{};
    std::uint64_t fileBytes = 0;
};

inline const char* IwiFormatName(std::uint8_t format) noexcept
{
    switch (format)
    {
    case IwiFormatDXT1: return "DXT1 / BC1";
    case IwiFormatDXT3: return "DXT3 / BC2";
    case IwiFormatDXT5: return "DXT5 / BC3";
    case 1: return "ARGB32";
    case 2: return "XRGB32";
    case 3: return "A8L8";
    case 4: return "L8";
    case 5: return "A8";
    default: return "other/unsupported IWI image format";
    }
}

inline IwiParseResult ParseIwiHeader(const unsigned char* bytes,
    std::size_t available, std::uint64_t fullFileSize, IwiHeader& output) noexcept
{
    output = {};
    if (!bytes || available < IwiHeaderBytes || fullFileSize < IwiHeaderBytes)
        return IwiParseResult::Truncated;
    if (bytes[0] != 'I' || bytes[1] != 'W' || bytes[2] != 'i')
        return IwiParseResult::NotIwi;
    if (bytes[3] != IwiVersion) return IwiParseResult::UnsupportedVersion;

    IwiHeader parsed{};
    parsed.version = bytes[3];
    parsed.format = bytes[4];
    parsed.flags = bytes[5];
    const auto w = bo1::bytecode::Load<std::int16_t>(bytes + 6);
    const auto h = bo1::bytecode::Load<std::int16_t>(bytes + 8);
    const auto d = bo1::bytecode::Load<std::int16_t>(bytes + 10);
    if (w <= 0 || h <= 0 || d <= 0)
        return IwiParseResult::InvalidDimensions;
    parsed.width = static_cast<std::uint16_t>(w);
    parsed.height = static_cast<std::uint16_t>(h);
    parsed.depth = static_cast<std::uint16_t>(d);
    parsed.gamma = bo1::bytecode::Load<float>(bytes + 12);
    for (std::size_t i = 0; i < parsed.fileSizeForPicmip.size(); ++i)
    {
        const auto size = bo1::bytecode::Load<std::int32_t>(bytes + 16 + i * 4);
        if (size < 0 || (size && (size < IwiHeaderBytes
            || std::uint64_t(size) > fullFileSize)))
            return IwiParseResult::InvalidFileSize;
        parsed.fileSizeForPicmip[i] = static_cast<std::uint32_t>(size);
    }
    if (std::uint64_t(parsed.fileSizeForPicmip[0]) != fullFileSize)
        return IwiParseResult::InvalidFileSize;
    parsed.fileBytes = fullFileSize;
    output = parsed;
    return IwiParseResult::Ok;
}

// The first/highest-resolution mip is stored LAST for ordinary 2D images.
// Cubemap/volume flags and volume depth are intentionally unsupported here.
// No floating-point or native pointer interpretation is involved.
inline bool IwiBC2TopMipRange(const IwiHeader& header,
    std::uint64_t& offset, std::size_t& compressedBytes) noexcept
{
    offset = 0;
    compressedBytes = 0;
    if (header.version != IwiVersion || header.format != IwiFormatDXT3
        || header.depth != 1 || (header.flags & 0x0cu))
        return false;
    std::size_t rgbaBytes = 0, expected = 0;
    if (!BC2ByteSizes(header.width, header.height, expected, rgbaBytes)
        || header.fileBytes < IwiHeaderBytes + std::uint64_t(expected))
        return false;
    // If the first picmip boundary is present, it should separate this
    // largest mip's blocks from the smaller levels (no blind extra bytes).
    const auto smallerMipsBoundary = header.fileSizeForPicmip[1];
    if (smallerMipsBoundary && header.fileBytes - smallerMipsBoundary != expected)
        return false;
    offset = header.fileBytes - expected;
    compressedBytes = expected;
    return true;
}
}
