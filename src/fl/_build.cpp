/// @file _build.cpp
/// @brief Master unity build for fl/ library
/// Compiles entire fl/ namespace into single object file
/// This is the ONLY .cpp file in src/fl/** that should be compiled

// Include placement new early to prevent __cxa_guard_* signature conflicts
// on Teensy 3.x. The platforms/new.h dispatch header handles all platforms.
// See CLAUDE.md "Function-local statics and Teensy 3.x" for details.
#include "platforms/new.h"

// IWYU pragma: begin_keep
#include "fl/system/arduino.h"  // Trampoline that includes Arduino.h + cleans up macros
// IWYU pragma: end_keep

// All fl/ implementations (current directory + all subdirectories hierarchically)
#include "fl/_build.cpp.hpp"
