#pragma once

#include <cstdint>

// These are the VM's scalar readers. Their operands keep the PC bytecode
// widths even when uintptr_t and native pointers are eight bytes wide.
#if defined(_MSC_VER)
#define BO1_SCRIPT_CALL __cdecl
#else
#define BO1_SCRIPT_CALL
#endif

std::uint16_t BO1_SCRIPT_CALL Scr_ReadUnsignedShort(const char** pos);
std::uintptr_t Scr_ReadUnsigned(const char** pos);
float Scr_ReadFloat(const char** pos);
int Scr_ReadInt(const char** pos);

#undef BO1_SCRIPT_CALL
