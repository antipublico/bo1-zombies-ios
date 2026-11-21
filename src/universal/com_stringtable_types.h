#pragma once

#include <cstddef>
#include <cstdint>

// Native runtime types shared by the engine and portable asset reconstruction.
// Serialized FastFiles use four-byte tags in place of the native pointers.
struct StringTableCell
{
    const char* string;
    int hash;
};

struct StringTable
{
    const char* name;
    int columnCount;
    int rowCount;
    StringTableCell* values;
    std::int16_t* cellIndex;
};

static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "Supported native pointer widths");
static_assert(sizeof(int) == 4, "StringTable counts and hashes retain four bytes");
static_assert(offsetof(StringTableCell, string) == 0);
static_assert(offsetof(StringTableCell, hash) == sizeof(void*));
static_assert(sizeof(StringTableCell) == (sizeof(void*) == 8 ? 16 : 8));
static_assert(offsetof(StringTable, name) == 0);
static_assert(offsetof(StringTable, columnCount) == sizeof(void*));
static_assert(offsetof(StringTable, rowCount) == sizeof(void*) + 4);
static_assert(offsetof(StringTable, values) == (sizeof(void*) == 8 ? 16 : 12));
static_assert(offsetof(StringTable, cellIndex) == (sizeof(void*) == 8 ? 24 : 16));
static_assert(sizeof(StringTable) == (sizeof(void*) == 8 ? 32 : 20));
