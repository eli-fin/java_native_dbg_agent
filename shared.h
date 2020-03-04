#ifndef SHARED_H
#define SHARED_H

#define _CRT_SECURE_NO_WARNINGS

#include <inttypes.h>
#include <string.h>
#include <mutex>
#include <string>
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
#pragma comment(lib, "jvm")

// define export
#ifdef _WIN32
#ifdef JAVANATIVEAGENTTEST_EXPORTS
#define API_EXPORT __declspec(dllexport)
#else
#define API_EXPORT
#endif // JAVANATIVEAGENTTEST_EXPORTS
#else
#define API_EXPORT
#endif // _WIN32

#define PRINT_PREFIX "[i] cx native agent: "

#endif //SHARED_H
