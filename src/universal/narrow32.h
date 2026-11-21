#pragma once

// Adapted from ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e, src/universal/narrow32.h.
// https://github.com/ReeceKetley/BlackOpsARM

#include <cstdint>
#include <platform/bo1_native_width.h>

#if BO1_NATIVE_64BIT
#include <atomic>
#endif

// Four-byte slots hold region handles on a 64-bit host. Bits 31..24 select
// a registered 16 MB region, and bits 23..0 hold the offset within that region.
// Zero is null. Published region entries are immutable for the process lifetime.
// These handles are runtime values and must be reconstructed when loading saves.
#ifndef BO1_NARROW_HANDLES
#define BO1_NARROW_HANDLES 1
#endif

#if BO1_NATIVE_64BIT

[[noreturn]] void Bo1_NarrowFail(const void* pointer);

#if BO1_NARROW_HANDLES
extern std::atomic<uintptr_t> g_bo1NarrowBase[256];
extern std::atomic<uint64_t> g_bo1NarrowKey[2048];
extern std::atomic<uint8_t> g_bo1NarrowIdx[2048];

unsigned int Bo1_NarrowEncodeSlow(const void* pointer);

inline unsigned int Bo1_NarrowEncode(const void* pointer)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (address == 0)
        return 0;

    const uint64_t chunk = static_cast<uint64_t>(address >> 24) + 1;
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
            break;
    }
    return Bo1_NarrowEncodeSlow(pointer);
}

inline void* Bo1_NarrowDecode(unsigned int value)
{
    if (value == 0)
        return nullptr;

    const uintptr_t region = g_bo1NarrowBase[value >> 24].load(std::memory_order_acquire);
    return reinterpret_cast<void*>(region + (value & 0xFFFFFF));
}
#else
inline unsigned int Bo1_NarrowEncode(const void* pointer)
{
    if (reinterpret_cast<uintptr_t>(pointer) >> 32)
        Bo1_NarrowFail(pointer);
    return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(pointer));
}

inline void* Bo1_NarrowDecode(unsigned int value)
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(value));
}
#endif

template <class T>
struct Narrow32
{
    unsigned int v;

    Narrow32& operator=(T* pointer)
    {
        v = Bo1_NarrowEncode(pointer);
        return *this;
    }

    operator T*() const { return static_cast<T*>(Bo1_NarrowDecode(v)); }

    template <class U>
    explicit operator U*() const { return (U*)static_cast<T*>(Bo1_NarrowDecode(v)); }

    T* operator->() const { return static_cast<T*>(Bo1_NarrowDecode(v)); }
    unsigned int bits() const { return v; }
    void setBits(unsigned int value) { v = value; }
};

#define BO1_NARROW(T) Narrow32<T>
template <class T> inline unsigned int Bo1_NarrowBits(const Narrow32<T>& pointer) { return pointer.bits(); }
template <class T> inline void Bo1_NarrowSetBits(Narrow32<T>& pointer, unsigned int value) { pointer.setBits(value); }
#define BO1_NARROW_ENCODE(pointer) Bo1_NarrowEncode(pointer)
#define BO1_NARROW_DECODE(T, value) (static_cast<T*>(Bo1_NarrowDecode(value)))
#else
#define BO1_NARROW(T) T*
template <class T> inline unsigned int Bo1_NarrowBits(T* const& pointer)
{
    return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(pointer));
}
template <class T> inline void Bo1_NarrowSetBits(T*& pointer, unsigned int value)
{
    pointer = reinterpret_cast<T*>(static_cast<uintptr_t>(value));
}
#define BO1_NARROW_ENCODE(pointer) (static_cast<unsigned int>(reinterpret_cast<uintptr_t>(pointer)))
#define BO1_NARROW_DECODE(T, value) (reinterpret_cast<T*>(static_cast<uintptr_t>(value)))
#endif
