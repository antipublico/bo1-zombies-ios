#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bo1::fastfile
{
    inline constexpr size_t FileHeaderSize = 12;
    inline constexpr size_t XFileHeaderSize = 36;
    inline constexpr size_t AssetListHeaderSize = 16;

    enum class ParseResult
    {
        Ok,
        Truncated,
        InvalidMagic,
        UnsupportedVersion,
        InvalidCount
    };

    struct FileHeader
    {
        uint32_t version;
        bool authenticated;
    };

    struct XFileHeader
    {
        uint32_t size;
        uint32_t externalSize;
        uint32_t blockSize[7];
    };

    // Serialized tags describe array presence. They are never native pointers.
    struct AssetListHeader
    {
        uint32_t scriptStringCount;
        uint32_t scriptStringsTag;
        uint32_t assetCount;
        uint32_t assetsTag;
    };

    // UnsupportedVersion still returns the parsed header for diagnostics.
    // Other errors leave the caller's output unchanged.
    ParseResult ParseFileHeader(const void* data, size_t size, FileHeader& output) noexcept;
    ParseResult ParseXFileHeader(const void* data, size_t size, XFileHeader& output) noexcept;
    ParseResult ParseAssetListHeader(const void* data, size_t size, AssetListHeader& output) noexcept;
}
