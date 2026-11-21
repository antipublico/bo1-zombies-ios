#include "database/db_fastfile_format.h"

#include <stdio.h>
#include <string.h>

namespace
{
    using namespace bo1::fastfile;

    int failures = 0;
    int checks = 0;

    void Check(bool passed, const char* description)
    {
        ++checks;
        if (!passed)
        {
            fprintf(stderr, "FAIL: %s\n", description);
            ++failures;
        }
    }

    bool Equal(const FileHeader& left, const FileHeader& right)
    {
        return left.version == right.version && left.authenticated == right.authenticated;
    }

    bool Equal(const XFileHeader& left, const XFileHeader& right)
    {
        if (left.size != right.size || left.externalSize != right.externalSize)
            return false;
        for (size_t index = 0; index < 7; ++index)
        {
            if (left.blockSize[index] != right.blockSize[index])
                return false;
        }
        return true;
    }

    void TestFileHeader()
    {
        const uint8_t valid[] = {'I', 'W', 'f', 'f', 'u', '1', '0', '0', 0xd9, 0x01, 0x00, 0x00};
        static_assert(sizeof(valid) == FileHeaderSize);
        const FileHeader sentinel{0xabcdef01u, true};
        FileHeader header = sentinel;

        Check(ParseFileHeader(valid, sizeof(valid), header) == ParseResult::Ok,
            "unsigned file header accepted");
        Check(header.version == 473 && !header.authenticated,
            "unsigned header fields decoded");

        uint8_t secure[FileHeaderSize];
        memcpy(secure, valid, sizeof(secure));
        secure[4] = '0';
        Check(ParseFileHeader(secure, sizeof(secure), header) == ParseResult::Ok,
            "authenticated file header accepted");
        Check(header.version == 473 && header.authenticated,
            "authenticated header fields decoded");

        for (size_t length = 0; length < FileHeaderSize; ++length)
        {
            header = sentinel;
            Check(ParseFileHeader(valid, length, header) == ParseResult::Truncated,
                "every partial file header is truncated");
            Check(Equal(header, sentinel), "truncated file header preserves output");
        }

        const size_t nullLengths[] = {0, FileHeaderSize};
        for (size_t length : nullLengths)
        {
            header = sentinel;
            Check(ParseFileHeader(nullptr, length, header) == ParseResult::Truncated,
                "null file header is truncated");
            Check(Equal(header, sentinel), "null file header preserves output");
        }

        for (size_t index = 0; index < 8; ++index)
        {
            uint8_t invalid[FileHeaderSize];
            memcpy(invalid, valid, sizeof(invalid));
            invalid[index] = '#';
            header = sentinel;
            Check(ParseFileHeader(invalid, sizeof(invalid), header) == ParseResult::InvalidMagic,
                "every magic byte is validated");
            Check(Equal(header, sentinel), "invalid magic preserves output");
        }

        const uint8_t versionBytes[][4] = {
            {0xd8, 0x01, 0x00, 0x00},
            {0xda, 0x01, 0x00, 0x00},
            {0x78, 0x56, 0x34, 0x12}
        };
        const uint32_t versions[] = {472, 474, 0x12345678u};
        for (size_t index = 0; index < 3; ++index)
        {
            uint8_t unsupported[FileHeaderSize];
            memcpy(unsupported, valid, sizeof(unsupported));
            memcpy(unsupported + 8, versionBytes[index], 4);
            header = sentinel;
            Check(ParseFileHeader(unsupported, sizeof(unsupported), header) == ParseResult::UnsupportedVersion,
                "unsupported versions rejected");
            Check(header.version == versions[index] && !header.authenticated,
                "unsupported version still returns little-endian header fields");
        }

        alignas(uint32_t) uint8_t unaligned[FileHeaderSize + 1]{};
        memcpy(unaligned + 1, valid, sizeof(valid));
        Check(ParseFileHeader(unaligned + 1, FileHeaderSize, header) == ParseResult::Ok,
            "unaligned file header accepted");
        Check(header.version == 473 && !header.authenticated,
            "unaligned file header fields decoded");

        uint8_t withTrailingBytes[FileHeaderSize + 4]{};
        memcpy(withTrailingBytes, valid, sizeof(valid));
        Check(ParseFileHeader(withTrailingBytes, sizeof(withTrailingBytes), header) == ParseResult::Ok,
            "file header can be read from a larger buffer");
    }

    void TestXFileHeader()
    {
        const uint8_t valid[] = {
            0x04, 0x03, 0x02, 0x01,
            0x01, 0x00, 0x00, 0x80,
            0x00, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0x7f,
            0x00, 0x00, 0x00, 0x80,
            0x78, 0x56, 0x34, 0x12,
            0x96, 0x5a, 0xc3, 0xa5,
            0x01, 0x00, 0x00, 0x00
        };
        static_assert(sizeof(valid) == XFileHeaderSize);
        const XFileHeader expected{
            0x01020304u, 0x80000001u,
            {0, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0x12345678u, 0xa5c35a96u, 1}
        };
        const XFileHeader sentinel{91, 92, {93, 94, 95, 96, 97, 98, 99}};
        XFileHeader header = sentinel;

        Check(ParseXFileHeader(valid, sizeof(valid), header) == ParseResult::Ok,
            "XFile header accepted");
        Check(Equal(header, expected), "all XFile fields decoded as unsigned little-endian values");

        for (size_t length = 0; length < XFileHeaderSize; ++length)
        {
            header = sentinel;
            Check(ParseXFileHeader(valid, length, header) == ParseResult::Truncated,
                "every partial XFile header is truncated");
            Check(Equal(header, sentinel), "truncated XFile header preserves output");
        }

        const size_t nullLengths[] = {0, XFileHeaderSize};
        for (size_t length : nullLengths)
        {
            header = sentinel;
            Check(ParseXFileHeader(nullptr, length, header) == ParseResult::Truncated,
                "null XFile header is truncated");
            Check(Equal(header, sentinel), "null XFile header preserves output");
        }

        alignas(uint32_t) uint8_t unaligned[XFileHeaderSize + 1]{};
        memcpy(unaligned + 1, valid, sizeof(valid));
        Check(ParseXFileHeader(unaligned + 1, XFileHeaderSize, header) == ParseResult::Ok,
            "unaligned XFile header accepted");
        Check(Equal(header, expected), "unaligned XFile fields decoded");

        uint8_t withTrailingBytes[XFileHeaderSize + 4]{};
        memcpy(withTrailingBytes, valid, sizeof(valid));
        Check(ParseXFileHeader(withTrailingBytes, sizeof(withTrailingBytes), header) == ParseResult::Ok,
            "XFile header can be read from a larger buffer");
        Check(Equal(header, expected), "trailing bytes do not change XFile fields");
    }
    void TestAssetListHeader()
    {
        const uint8_t valid[] = {
            0x15, 0x03, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
            0x8c, 0x03, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
        };
        AssetListHeader header{};
        Check(ParseAssetListHeader(valid, sizeof(valid), header) == ParseResult::Ok,
            "asset list header accepted");
        Check(header.scriptStringCount == 789 && header.assetCount == 908,
            "asset counts decoded in little-endian order");
        Check(header.scriptStringsTag == UINT32_MAX && header.assetsTag == UINT32_MAX,
            "serialized sentinel tags preserved");
        for (size_t length = 0; length < AssetListHeaderSize; ++length)
        {
            Check(ParseAssetListHeader(valid, length, header) == ParseResult::Truncated,
                "partial asset header rejected");
            Check(header.assetCount == 908, "truncated asset header preserves output");
        }
        Check(ParseAssetListHeader(nullptr, AssetListHeaderSize, header) == ParseResult::Truncated,
            "null asset header rejected");
        alignas(uint32_t) uint8_t unaligned[AssetListHeaderSize + 1]{};
        memcpy(unaligned + 1, valid, sizeof(valid));
        Check(ParseAssetListHeader(unaligned + 1, AssetListHeaderSize, header) == ParseResult::Ok,
            "unaligned asset header accepted");
        uint8_t negative[AssetListHeaderSize];
        memcpy(negative, valid, sizeof(valid));
        negative[3] = 0x80;
        Check(ParseAssetListHeader(negative, sizeof(negative), header) == ParseResult::InvalidCount,
            "negative script string count rejected");
        Check(header.scriptStringCount == 789, "invalid count preserves output");
        memcpy(negative, valid, sizeof(valid));
        negative[11] = 0x80;
        Check(ParseAssetListHeader(negative, sizeof(negative), header) == ParseResult::InvalidCount,
            "negative asset count rejected");
    }
}

int main()
{
    TestFileHeader();
    TestXFileHeader();
    TestAssetListHeader();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
