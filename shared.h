#ifndef SHARED_H
#define SHARED_H

#include <jni.h>
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

#endif //SHARED_H
