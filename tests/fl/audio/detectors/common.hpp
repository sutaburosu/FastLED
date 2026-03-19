/// @file tests/fl/audio/detectors/common.hpp
/// Shared includes, using-declarations, and timing utilities for all detector
/// test .hpp files.  Each detector test includes this header so that it is
/// self-contained and independent of include order in detectors.cpp.

#pragma once

// Test framework
#include "test.h"
#include "tests/fl/audio/test_helpers.h"

// Audio core
#include "fl/audio/audio.h"
#include "fl/audio/audio_context.h"
#include "fl/audio/fft/fft.h"

// Audio detectors
#include "fl/audio/detectors/backbeat.h"
#include "fl/audio/detectors/beat.h"
#include "fl/audio/detectors/downbeat.h"
#include "fl/audio/detectors/energy_analyzer.h"
#include "fl/audio/detectors/frequency_bands.h"
#include "fl/audio/detectors/percussion.h"
#include "fl/audio/detectors/tempo_analyzer.h"
#include "fl/audio/detectors/vibe.h"
#include "fl/audio/detectors/vocal.h"

// Codec
#include "fl/codec/vorbis.h"

// STL
#include "fl/stl/allocator.h"
#include "fl/stl/chrono.h"
#include "fl/stl/cstdio.h"
#include "fl/stl/cstring.h"
#include "fl/stl/detail/file_handle.h"
#include "fl/stl/detail/file_io.h"
#include "fl/stl/function.h"
#include "fl/stl/int.h"
#include "fl/stl/math.h"
#include "fl/stl/move.h"
#include "fl/stl/new.h"
#include "fl/stl/scope_exit.h"
#include "fl/stl/shared_ptr.h"
#include "fl/stl/span.h"
#include "fl/stl/strstream.h"
#include "fl/stl/vector.h"

using namespace fl;

static double g_section_start = 0;
static double g_overall_start = 0;

static void timing_start(const char* label) {
    double now = (double)fl::millis();
    if (g_overall_start == 0) g_overall_start = now;
    g_section_start = now;
    fl::sstream ss;
    ss << "[TIMING] >>> " << label << " starting ("
       << (now - g_overall_start) / 1000.0 << "s elapsed)";
    fl::println(ss.str().c_str());
}

static void timing_end(const char* label) {
    double now = (double)fl::millis();
    fl::sstream ss;
    ss << "[TIMING] <<< " << label << " done: "
       << (now - g_section_start) << " ms ("
       << (now - g_overall_start) / 1000.0 << "s elapsed)";
    fl::println(ss.str().c_str());
}
