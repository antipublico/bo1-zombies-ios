#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// Adapted from ReeceKetley/BlackOpsARM commit
// 2c1edb8720464113eb76d4d0914a6bf959e7136e, src/platform/bo1_bytecode_io.h.
// https://github.com/ReeceKetley/BlackOpsARM
// The engine's instruction stream contains packed little-endian scalars.
// It does not contain aligned C++ objects or native-width integer operands.
namespace bo1::bytecode
{
template <typename T>
inline constexpr bool Scalar =
    (std::is_integral_v<T> || std::is_same_v<T, float>) &&
    !std::is_same_v<T, bool> &&
    (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Script floats require the IEEE 754 binary32 representation");

template <typename T>
T Load(const void* bytes)
{
    static_assert(Scalar<T>, "Use a one-, two- or four-byte scalar, never a native pointer");
    using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t,
                 std::conditional_t<sizeof(T) == 2, std::uint16_t, std::uint32_t>>;
    const auto* source = static_cast<const unsigned char*>(bytes);
    Bits bits = 0;
    for (unsigned int i = 0; i < sizeof(T); ++i)
        bits |= static_cast<Bits>(static_cast<std::uint32_t>(source[i]) << (8 * i));
    T value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

template <typename T>
void Store(void* bytes, T value)
{
    static_assert(Scalar<T>, "Use a one-, two- or four-byte scalar, never a native pointer");
    using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t,
                 std::conditional_t<sizeof(T) == 2, std::uint16_t, std::uint32_t>>;
    Bits bits;
    std::memcpy(&bits, &value, sizeof(value));
    auto* destination = static_cast<unsigned char*>(bytes);
    for (unsigned int i = 0; i < sizeof(T); ++i)
        destination[i] = static_cast<unsigned char>(bits >> (8 * i));
}

template <typename T>
T Read(const char** cursor)
{
    const T value = Load<T>(*cursor);
    *cursor += sizeof(T);
    return value;
}
}
