#pragma once

// FL_NOEXCEPT: expands to noexcept on ESP32 (reduces .eh_frame bloat),
// noop on all other platforms. Safe in C translation units.

#include "platforms/is_platform.h" // IWYU pragma: keep

#ifndef FL_NOEXCEPT
#if defined(FL_IS_ESP32) && defined(__cplusplus)
#define FL_NOEXCEPT noexcept
#else
#define FL_NOEXCEPT
#endif
#endif
