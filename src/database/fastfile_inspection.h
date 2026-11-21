#pragma once

#include "db_fastfile_format.h"
#include <string>
#include <vector>

namespace bo1::fastfile
{
enum class InspectionResult { Ok, IoError, InvalidFormat, UnsupportedReference, LimitExceeded, CorruptStream, OutOfMemory };

struct InspectionLimits
{
    uint64_t maxFileBytes = 512ull * 1024 * 1024;
    uint64_t maxExpandedBytes = 512ull * 1024 * 1024;
    // Bounds retained object sizes and string lengths, not process RSS or
    // allocator overhead. The inflater uses two additional 32 KiB buffers.
    size_t maxMetadataBytes = 8 * 1024 * 1024;
    size_t maxZeroPadding = 31;
};

struct ScriptString
{
    bool present = false;
    uint32_t serializedTag = 0;
    std::string value;
};

struct AssetEntry
{
    uint32_t type = 0;
    uint32_t serializedTag = 0;
};

struct Inspection
{
    FileHeader header{};
    XFileHeader file{};
    AssetListHeader list{};
    uint64_t expandedBytes = 0;
    uint64_t metadataBytes = 0;
    std::vector<ScriptString> scriptStrings;
    std::vector<AssetEntry> assets;
};

// Reads the complete zlib stream but retains only strings and the asset
// directory. No serialized tag becomes a native pointer. Offset strings are
// supported only when they identify an already decoded string in block 4.
// On failure output is empty and error contains a diagnostic.
InspectionResult InspectFile(const char* path, Inspection& output, std::string& error,
    const InspectionLimits& limits = {});
const char* AssetTypeName(uint32_t type) noexcept;
}
