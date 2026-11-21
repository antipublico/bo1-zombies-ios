#pragma once

#include <cstddef>

// Runtime value/name pair for ASSET_TYPE_LOCALIZE_ENTRY (23). The serialized
// header has two four-byte tags; each becomes a native string pointer here.
struct LocalizeEntry
{
    const char* value;
    const char* name;
};

static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "Supported native pointer widths");
static_assert(offsetof(LocalizeEntry, value) == 0);
static_assert(offsetof(LocalizeEntry, name) == sizeof(void*));
static_assert(sizeof(LocalizeEntry) == (sizeof(void*) == 8 ? 16 : 8));
