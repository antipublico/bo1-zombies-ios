#pragma once
// Internal streaming interface shared by metadata inspection and reconstruction.
#include "fastfile_inspection.h"
#include <zlib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace bo1::fastfile::detail
{
// Follow Load_XAssetListCustom, Load_TempStringArray and Load_XAssetArrayCustom.
// DB_AllocStreamPos aligns the block cursor, without adding bytes to the file.
class Reader
{
public:
    Reader(FILE* file, const InspectionLimits& limits, std::string& error)
        : input(file), limits(limits), error(error)
    {
        initialized = inflateInit(&stream) == Z_OK;
        if (!initialized)
            Fail(InspectionResult::OutOfMemory, "Could not initialize the inflater.");
    }

    ~Reader() { if (initialized) inflateEnd(&stream); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool Fail(InspectionResult code, const char* message)
    {
        if (result == InspectionResult::Ok)
        {
            result = code;
            error = message;
        }
        return false;
    }

    bool Read(void* destination, size_t count)
    {
        auto* bytes = static_cast<unsigned char*>(destination);
        while (count)
        {
            if (position == available && !Fill())
                return false;
            const size_t take = std::min(count, available - position);
            std::memcpy(bytes, expanded + position, take);
            bytes += take;
            position += take;
            consumed += take;
            count -= take;
        }
        return true;
    }

    bool Finish()
    {
        while (!ended && result == InspectionResult::Ok)
            Fill();
        if (result != InspectionResult::Ok)
            return false;
        size_t padding = 0;
        for (;;)
        {
            while (stream.avail_in)
            {
                if (*stream.next_in++ != 0 || padding == limits.maxZeroPadding)
                    return Fail(InspectionResult::InvalidFormat, "Unexpected trailing data after compressed stream.");
                --stream.avail_in;
                ++padding;
            }
            if (!ReadInput())
                return result == InspectionResult::Ok;
        }
    }

    InspectionResult result = InspectionResult::Ok;
    uint64_t produced = 0;
    uint64_t consumed = 0;

private:
    bool ReadInput()
    {
        const size_t bytes = std::fread(compressed, 1, sizeof(compressed), input);
        if (std::ferror(input))
            return Fail(InspectionResult::IoError, "Compressed payload is truncated or unreadable.");
        if (inputBytes > limits.maxFileBytes || bytes > limits.maxFileBytes - inputBytes)
            return Fail(InspectionResult::LimitExceeded, "Fastfile exceeds the input byte limit.");
        inputBytes += bytes;
        stream.next_in = compressed;
        stream.avail_in = static_cast<uInt>(bytes);
        return bytes != 0;
    }

    bool Fill()
    {
        position = available = 0;
        if (!initialized || ended || result != InspectionResult::Ok)
            return false;
        while (!available && !ended)
        {
            if (!stream.avail_in && !ReadInput())
                return Fail(InspectionResult::CorruptStream, "Compressed payload is truncated or unreadable.");
            stream.next_out = expanded;
            stream.avail_out = sizeof(expanded);
            const int status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END)
                return Fail(InspectionResult::CorruptStream, "Invalid compressed payload.");
            available = sizeof(expanded) - stream.avail_out;
            if (produced > limits.maxExpandedBytes || available > limits.maxExpandedBytes - produced)
                return Fail(InspectionResult::LimitExceeded, "Fastfile exceeds the decompressed byte limit.");
            produced += available;
            ended = status == Z_STREAM_END;
        }
        return available != 0;
    }

    FILE* input;
    const InspectionLimits& limits;
    std::string& error;
    z_stream stream{};
    unsigned char compressed[32768]{};
    unsigned char expanded[32768]{};
    uint64_t inputBytes = FileHeaderSize;
    size_t position = 0;
    size_t available = 0;
    bool initialized = false;
    bool ended = false;
};

struct MetadataState
{
    std::vector<std::pair<uint32_t, uint32_t>> strings;
    uint64_t directoryStart = 0;
    uint64_t block4Cursor = 0;
};
using AfterMetadata = bool (*)(Reader&, Inspection&, const MetadataState&, void*);
InspectionResult ReadFile(const char* path, Inspection& output, std::string& error,
    const InspectionLimits& limits, AfterMetadata callback = nullptr, void* context = nullptr);
}
