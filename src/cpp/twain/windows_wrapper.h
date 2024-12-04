#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PASCAL __stdcall
#define WINAPI __stdcall
#define CALLBACK __stdcall
typedef HANDLE TW_HANDLE;
typedef LPVOID TW_MEMREF;
typedef UINT_PTR TW_UINTPTR;
#else
// Non-Windows definitions if needed
#endif