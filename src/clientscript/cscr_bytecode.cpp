#include "cscr_bytecode.h"
#include <platform/bo1_bytecode_io.h>

std::uint16_t Scr_ReadUnsignedShort(const char** pos)
{
    return bo1::bytecode::Read<std::uint16_t>(pos);
}

std::uintptr_t Scr_ReadUnsigned(const char** pos)
{
    return bo1::bytecode::Read<std::uint32_t>(pos);
}

float Scr_ReadFloat(const char** pos)
{
    return bo1::bytecode::Read<float>(pos);
}

int Scr_ReadInt(const char** pos)
{
    static_assert(sizeof(int) == 4, "Script integer operands are four bytes");
    return bo1::bytecode::Read<std::int32_t>(pos);
}
