#pragma once

///////////////////////////////////////////////////////////////////////////////
// FastLED C Standard Library Compatibility Header
//
// This header provides a compatibility layer for third-party code that
// expects standard C library functions like malloc, free, exit, etc.
///////////////////////////////////////////////////////////////////////////////

#include "fl/stl/stdint.h"
#include "fl/stl/cstddef.h"

namespace fl {

// ============================================================================
// fl::aligned_alloc / fl::aligned_free — Portable over-aligned allocation
// ============================================================================
// Mirrors std::aligned_alloc from <cstdlib> (C++17 / C11).
// Falls back to plain malloc on platforms where over-alignment is
// unsupported or unnecessary (AVR, ESP8266).
// Defined in cstdlib.cpp.hpp.

void *aligned_alloc(fl::size_t alignment, fl::size_t size);

// Portable free for memory obtained from fl::aligned_alloc.
// On POSIX, std::free suffices; on Windows, _aligned_free is required.
void aligned_free(void *ptr);

// Convert string to long integer
// Similar to standard strtol but without locale support
long strtol(const char* str, char** endptr, int base);

// Convert string to unsigned long integer
unsigned long strtoul(const char* str, char** endptr, int base);

// Convert string to integer
int atoi(const char* str);

// Convert string to long
long atol(const char* str);

// Convert string to double
double strtod(const char* str, char** endptr);

// C-style comparison function type for qsort
typedef int (*qsort_compare_fn)(const void*, const void*);

// qsort - Quick sort function compatible with C stdlib qsort
// Sorts an array of elements using the provided comparison function
void qsort(void* base, size_t nmemb, size_t size, qsort_compare_fn compar);

// Pseudo-random number generator (mirrors ::rand()).
// Returns u32 — values are always non-negative [0, RAND_MAX].
// Fixed-width avoids AVR's 16-bit int truncation.
u32 rand();

// Get the value of an environment variable
// Only functional on FASTLED_TESTING (stub platform), returns nullptr otherwise
// This avoids std:: namespace dependencies on embedded platforms
const char* getenv(const char* name);

} // namespace fl
