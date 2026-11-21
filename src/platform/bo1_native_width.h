#pragma once

#include <stdint.h>

// Adapted from ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e, src/platform/bo1_native_width.h.
// The portable component follows the compiler's ABI on every platform.
#if UINTPTR_MAX == UINT64_MAX
#define BO1_NATIVE_64BIT 1
#else
#define BO1_NATIVE_64BIT 0
#endif
