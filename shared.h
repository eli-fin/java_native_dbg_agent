#ifndef SHARED_H
#define SHARED_H

#define _CRT_SECURE_NO_WARNINGS

#include <inttypes.h>
#include <string.h>
#include <exception>
#include <string>
#include <sstream>
#include <locale>
#include <codecvt>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define getpid() GetCurrentProcessId()
#define wcsdup(x) _wcsdup(x)
#else
#include <sys/types.h>
#include <unistd.h>
#endif // _WIN32


#include <jni.h>
#include <jvmti.h>

// define export
#ifdef _WIN32
#ifdef CX_JAVA_NATIVE_DBG_AGENT_EXPORTS
#define API_EXPORT __declspec(dllexport)
#else
#define API_EXPORT
#endif // CX_JAVA_NATIVE_DBG_AGENT_EXPORTS
#else
#define API_EXPORT
#endif // _WIN32

#define PRINT_PREFIX "[i] cx native agent: "

#endif //SHARED_H
