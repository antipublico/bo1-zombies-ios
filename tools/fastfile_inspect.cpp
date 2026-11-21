#include <database/db_fastfile_format.h>
#include <zlib.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Inspect the disk envelope without interpreting serialized references as pointers.
static int Inspect(FILE* input)
{
    using namespace bo1::fastfile;
    unsigned char envelope[FileHeaderSize];
    FileHeader header{};
    const auto result = ParseFileHeader(envelope, fread(envelope, 1, sizeof(envelope), input), header);
    if (result != ParseResult::Ok)
    {
        fprintf(stderr, "Invalid fastfile header (result %d, version %" PRIu32 ").\n",
            static_cast<int>(result), header.version);
        return 1;
    }
    if (header.authenticated)
    {
        fprintf(stderr, "Authenticated fastfiles are unsupported by this loader.\n");
        return 1;
    }

    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
    {
        fprintf(stderr, "Could not initialize the inflater.\n");
        return 1;
    }

    unsigned char compressed[32768];
    unsigned char expanded[32768];
    unsigned char prefix[XFileHeaderSize + AssetListHeaderSize];
    size_t headerBytes = 0;
    uint64_t expandedBytes = 0;
    int status = Z_OK;
    while (status != Z_STREAM_END)
    {
        if (stream.avail_in == 0)
        {
            stream.avail_in = static_cast<uInt>(fread(compressed, 1, sizeof(compressed), input));
            stream.next_in = compressed;
            if (stream.avail_in == 0)
            {
                fprintf(stderr, "Compressed payload is truncated or unreadable.\n");
                inflateEnd(&stream);
                return 1;
            }
        }

        stream.next_out = expanded;
        stream.avail_out = sizeof(expanded);
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
        {
            fprintf(stderr, "Invalid compressed payload (zlib %d).\n", status);
            inflateEnd(&stream);
            return 1;
        }

        const size_t produced = sizeof(expanded) - stream.avail_out;
        size_t toCopy = sizeof(prefix) - headerBytes;
        if (toCopy > produced)
            toCopy = produced;
        memcpy(prefix + headerBytes, expanded, toCopy);
        headerBytes += toCopy;
        expandedBytes += produced;
    }
    inflateEnd(&stream);

    XFileHeader file{};
    if (ParseXFileHeader(prefix, headerBytes, file) != ParseResult::Ok)
    {
        fprintf(stderr, "XFile header is truncated.\n");
        return 1;
    }

    if (expandedBytes != XFileHeaderSize + static_cast<uint64_t>(file.size))
    {
        fprintf(stderr, "Declared payload size does not match the decompressed stream.\n");
        return 1;
    }

    AssetListHeader assets{};
    if (ParseAssetListHeader(prefix + XFileHeaderSize, headerBytes - XFileHeaderSize, assets) != ParseResult::Ok)
    {
        fprintf(stderr, "Invalid or truncated asset list header.\n");
        return 1;
    }
    const uint64_t stringsBytes = assets.scriptStringsTag ? uint64_t(assets.scriptStringCount) * 4 : 0;
    const uint64_t assetsBytes = assets.assetsTag ? uint64_t(assets.assetCount) * 8 : 0;
    const uint64_t minimumBytes = stringsBytes + assetsBytes;
    if (minimumBytes > file.size - AssetListHeaderSize || minimumBytes > file.blockSize[4])
    {
        fprintf(stderr, "Asset table counts exceed the package or stream block.\n");
        return 1;
    }

    printf("Version %" PRIu32 "\n", header.version);
    printf("Declared payload bytes %" PRIu32 "\n", file.size);
    printf("External bytes %" PRIu32 "\n", file.externalSize);
    printf("Inflated bytes %" PRIu64 "\n", expandedBytes);
    printf("Script strings %" PRIu32 " (table %s)\n", assets.scriptStringCount, assets.scriptStringsTag ? "present" : "absent");
    printf("Assets %" PRIu32 " (table %s)\n", assets.assetCount, assets.assetsTag ? "present" : "absent");
    for (size_t index = 0; index < 7; ++index)
        printf("Block %zu bytes %" PRIu32 "\n", index, file.blockSize[index]);
    printf("Envelope and compressed stream verified. Asset loading remains pending.\n");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: bo1_fastfile_inspect <zone.ff>\n");
        return 2;
    }
    FILE* input = fopen(argv[1], "rb");
    if (input == nullptr)
    {
        perror(argv[1]);
        return 1;
    }
    const int result = Inspect(input);
    fclose(input);
    return result;
}
