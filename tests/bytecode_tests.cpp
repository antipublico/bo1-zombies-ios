#include <clientscript/cscr_bytecode.h>
#include <platform/bo1_bytecode_io.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
unsigned int checks = 0;
unsigned int failures = 0;

void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

template <typename T>
void CheckStore(T value, const unsigned char* expected)
{
    // Every offset exercises packed data; the surrounding sentinels detect
    // accidental native-width writes or writes outside the operand.
    for (unsigned int offset = 1; offset <= 7; ++offset)
    {
        alignas(8) unsigned char bytes[16];
        std::memset(bytes, 0xA5, sizeof(bytes));
        bo1::bytecode::Store<T>(bytes + offset, value);
        Check(std::memcmp(bytes + offset, expected, sizeof(T)) == 0, "canonical little-endian encoding");
        bool guards = true;
        for (unsigned int i = 0; i < sizeof(bytes); ++i)
            if (i < offset || i >= offset + sizeof(T))
                guards &= bytes[i] == 0xA5;
        Check(guards, "store only changes the operand bytes");
    }
}

void CheckMixedStream()
{
    // Expected bytes come from the BO1 scalar encoding, independently of Store.
    // A leading opcode byte deliberately leaves the first operand unaligned.
    const unsigned char encoded[] = {
        0x08, 0xFE, 0xFF, 0xFF, 0xFF, // integer -2
        0x06, 0x01, 0x80,             // unsigned short 32769
        0x09, 0x00, 0x00, 0x20, 0xC0, // float -2.5
        0x7D, 0xEF, 0xCD, 0xAB, 0x89, // unsigned switch value
        0x01                          // next opcode
    };
    for (unsigned int offset = 0; offset < 8; ++offset)
    {
        alignas(8) unsigned char bytes[40] = {};
        std::memcpy(bytes + offset, encoded, sizeof(encoded));
        const char* start = reinterpret_cast<const char*>(bytes + offset);
        const char* cursor = start;
        Check(*cursor++ == 0x08, "integer opcode");
        Check(Scr_ReadInt(&cursor) == -2, "signed integer operand");
        Check(cursor == start + 5, "integer cursor advances four bytes");
        Check(*cursor++ == 0x06, "unsigned short opcode");
        Check(Scr_ReadUnsignedShort(&cursor) == 32769, "unsigned short preserves high bit");
        Check(cursor == start + 8, "short cursor advances two bytes");
        Check(*cursor++ == 0x09, "float opcode");
        Check(Scr_ReadFloat(&cursor) == -2.5f, "negative float operand");
        Check(cursor == start + 13, "float cursor advances four bytes");
        Check(*cursor++ == 0x7D, "switch opcode");
        Check(Scr_ReadUnsigned(&cursor) == std::uintptr_t{0x89ABCDEFu}, "unsigned operand zero-extends on ARM64");
        Check(cursor == start + 18, "unsigned cursor advances four bytes on every ABI");
        Check(*cursor == 0x01, "next opcode survives mixed operands");
    }
}
}

int main()
{
    static_assert(!bo1::bytecode::Scalar<std::uintptr_t> || sizeof(std::uintptr_t) == 4);
    static_assert(!bo1::bytecode::Scalar<std::uint64_t>);
    static_assert(!bo1::bytecode::Scalar<bool>);
    static_assert(!bo1::bytecode::Scalar<void*>);

    CheckMixedStream();
    const unsigned char u16[] = {0xAB, 0xCD};
    const unsigned char u32[] = {0xEF, 0xCD, 0xAB, 0x89};
    const unsigned char negative[] = {0xFE, 0xFF, 0xFF, 0xFF};
    const unsigned char minimum[] = {0x00, 0x00, 0x00, 0x80};
    const unsigned char floatValue[] = {0x00, 0x00, 0x20, 0xC0};
    const unsigned char negativeZero[] = {0x00, 0x00, 0x00, 0x80};
    CheckStore<std::uint16_t>(0xCDAB, u16);
    CheckStore<std::uint32_t>(0x89ABCDEF, u32);
    CheckStore<std::int32_t>(-2, negative);
    CheckStore<std::int32_t>(std::numeric_limits<std::int32_t>::min(), minimum);
    CheckStore<float>(-2.5f, floatValue);
    CheckStore<float>(-0.0f, negativeZero);

    for (unsigned int offset = 1; offset < 8; ++offset)
    {
        alignas(8) unsigned char bytes[16] = {};
        std::memcpy(bytes + offset, minimum, 4);
        Check(bo1::bytecode::Load<std::int32_t>(bytes + offset) == std::numeric_limits<std::int32_t>::min(), "minimum signed operand");
        Check(std::signbit(bo1::bytecode::Load<float>(bytes + offset)), "negative zero sign bit");
        const unsigned char infinity[] = {0x00, 0x00, 0x80, 0x7F};
        std::memcpy(bytes + offset, infinity, 4);
        Check(std::isinf(bo1::bytecode::Load<float>(bytes + offset)), "infinite float payload");
        const unsigned char nan[] = {0x45, 0x23, 0xC1, 0x7F};
        std::memcpy(bytes + offset, nan, 4);
        float value = bo1::bytecode::Load<float>(bytes + offset);
        Check(std::isnan(value), "NaN operand");
        bo1::bytecode::Store<float>(bytes + offset, value);
        Check(std::memcmp(bytes + offset, nan, 4) == 0, "NaN payload preserved");
    }
    std::printf("%u bytecode checks, %u failures\n", checks, failures);
    return failures != 0;
}
