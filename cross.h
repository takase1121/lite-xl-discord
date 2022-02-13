#ifndef CROSS_H
#define CROSS_H

#include "cosmopolitan.h"
typedef int64_t HANDLE;
typedef unsigned int DWORD;

#ifndef __stdcall
#define __stdcall __attribute__((stdcall))
#endif

#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define INVALID_HANDLE_VALUE -1
#define OPEN_EXISTING 3
#define INFINITE 0xffffffff

#define STD_INPUT_HANDLE ((DWORD) - 10)
#define STD_OUTPUT_HANDLE ((DWORD) - 11)

#define ENABLE_ECHO_INPUT 0x0004
#define ENABLE_LINE_INPUT 0x0002

#endif
