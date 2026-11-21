#pragma once

#include <cstddef>

// Native runtime layout. FastFile headers retain 32-bit pointer tags and must
// be decoded into this structure rather than copied over a native instance.
// Layout checks follow ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e, src/database/db_rawfile_types.h.
struct RawFile
{
    const char* name;
    int len;
    const char* buffer;
};

static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "Supported native pointer widths");
static_assert(sizeof(int) == 4, "RawFile length retains four bytes");
static_assert(offsetof(RawFile, name) == 0);
static_assert(offsetof(RawFile, len) == sizeof(void*));
static_assert(offsetof(RawFile, buffer) == (sizeof(void*) == 8 ? 16 : 8));
static_assert(sizeof(RawFile) == (sizeof(void*) == 8 ? 24 : 12));
