// Adapted from ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e,
// src/platform/android/narrow32_android.cpp.
// https://github.com/ReeceKetley/BlackOpsARM

#include <universal/narrow32.h>

#if BO1_NATIVE_64BIT
#include <cstdio>
#include <cstdlib>
#include <mutex>

[[noreturn]] void Bo1_NarrowFail(const void* pointer)
{
    std::fprintf(stderr, "Bo1_NarrowFail: cannot represent pointer %p in a four-byte slot\n", pointer);
    std::abort();
}

#if BO1_NARROW_HANDLES
std::atomic<uintptr_t> g_bo1NarrowBase[256]{};
std::atomic<uint64_t> g_bo1NarrowKey[2048]{};
std::atomic<uint8_t> g_bo1NarrowIdx[2048]{};
static unsigned int regionCount;
static std::mutex registrationMutex;

unsigned int Bo1_NarrowEncodeSlow(const void* pointer)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    const uint64_t chunk = static_cast<uint64_t>(address >> 24) + 1;
    std::lock_guard<std::mutex> lock(registrationMutex);
    unsigned int bucket = static_cast<unsigned int>((chunk * 0x9E3779B97F4A7C15ull) >> 53);
    for (unsigned int probe = 0; probe < 2048; ++probe, bucket = (bucket + 1) & 2047)
    {
        const uint64_t key = g_bo1NarrowKey[bucket].load(std::memory_order_acquire);
        if (key == chunk)
        {
            const unsigned int region = g_bo1NarrowIdx[bucket].load(std::memory_order_relaxed);
            return (region << 24) | static_cast<unsigned int>(address & 0xFFFFFF);
        }
        if (key == 0)
        {
            if (regionCount >= 255)
                Bo1_NarrowFail(pointer);

            const unsigned int region = ++regionCount;
            g_bo1NarrowBase[region].store(static_cast<uintptr_t>(chunk - 1) << 24, std::memory_order_release);
            g_bo1NarrowIdx[bucket].store(static_cast<uint8_t>(region), std::memory_order_relaxed);
            // A reader that acquires this key can also observe its base and index.
            g_bo1NarrowKey[bucket].store(chunk, std::memory_order_release);
            return (region << 24) | static_cast<unsigned int>(address & 0xFFFFFF);
        }
    }
    Bo1_NarrowFail(pointer);
}
#endif
#endif
