#include "db_fastfile_format.h"

namespace bo1::fastfile
{
    namespace
    {
        uint32_t ReadLittleEndian32(const uint8_t* bytes) noexcept
        {
            return static_cast<uint32_t>(bytes[0])
                | (static_cast<uint32_t>(bytes[1]) << 8)
                | (static_cast<uint32_t>(bytes[2]) << 16)
                | (static_cast<uint32_t>(bytes[3]) << 24);
        }
    }

    ParseResult ParseFileHeader(const void* data, size_t size, FileHeader& output) noexcept
    {
        if (data == nullptr || size < FileHeaderSize)
            return ParseResult::Truncated;

        const auto* bytes = static_cast<const uint8_t*>(data);
        if (bytes[0] != 'I' || bytes[1] != 'W' || bytes[2] != 'f' || bytes[3] != 'f'
            || (bytes[4] != 'u' && bytes[4] != '0')
            || bytes[5] != '1' || bytes[6] != '0' || bytes[7] != '0')
        {
            return ParseResult::InvalidMagic;
        }

        output.version = ReadLittleEndian32(bytes + 8);
        output.authenticated = bytes[4] == '0';
        if (output.version != 473)
            return ParseResult::UnsupportedVersion;

        return ParseResult::Ok;
    }

    ParseResult ParseXFileHeader(const void* data, size_t size, XFileHeader& output) noexcept
    {
        if (data == nullptr || size < XFileHeaderSize)
            return ParseResult::Truncated;

        const auto* bytes = static_cast<const uint8_t*>(data);
        XFileHeader header{};
        header.size = ReadLittleEndian32(bytes);
        header.externalSize = ReadLittleEndian32(bytes + 4);
        for (size_t index = 0; index < 7; ++index)
            header.blockSize[index] = ReadLittleEndian32(bytes + 8 + index * 4);

        output = header;
        return ParseResult::Ok;
    }

    ParseResult ParseAssetListHeader(const void* data, size_t size, AssetListHeader& output) noexcept
    {
        if (data == nullptr || size < AssetListHeaderSize)
            return ParseResult::Truncated;

        const auto* bytes = static_cast<const uint8_t*>(data);
        AssetListHeader header{};
        header.scriptStringCount = ReadLittleEndian32(bytes);
        header.scriptStringsTag = ReadLittleEndian32(bytes + 4);
        header.assetCount = ReadLittleEndian32(bytes + 8);
        header.assetsTag = ReadLittleEndian32(bytes + 12);
        if (header.scriptStringCount > INT32_MAX || header.assetCount > INT32_MAX)
            return ParseResult::InvalidCount;

        output = header;
        return ParseResult::Ok;
    }
}
