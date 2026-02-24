#pragma once

#include "fl/compiler_control.h"
#include "fl/stl/type_traits.h"

// Platform macros for min/max/abs must be undefined so we can define
// proper template functions in the fl namespace.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef abs
#undef abs
#endif

namespace fl {

template <typename T> constexpr inline T abs(T value) {
    return value < 0 ? -value : value;
}

FL_DISABLE_WARNING_PUSH
FL_DISABLE_WARNING(sign-compare)
FL_DISABLE_WARNING_FLOAT_CONVERSION
FL_DISABLE_WARNING_SIGN_CONVERSION
FL_DISABLE_WARNING_IMPLICIT_INT_CONVERSION
FL_DISABLE_WARNING_FLOAT_CONVERSION


// Template functions for FL_MIN and FL_MAX to avoid statement repetition
// Returns the most promotable type between the two arguments
template <typename T, typename U> constexpr inline common_type_t<T, U> min(T a, U b) {
    return (a < b) ? a : b;
}

template <typename T, typename U> constexpr inline common_type_t<T, U> max(T a, U b) {
    return (a > b) ? a : b;
}

FL_DISABLE_WARNING_POP
} // namespace fl

#ifndef FL_MAX
#define FL_MAX(a, b) fl::max(a, b)
#endif

#ifndef FL_MIN
#define FL_MIN(a, b) fl::min(a, b)
#endif

#ifndef FL_ABS
#define FL_ABS(x) fl::abs(x)
#endif

#ifndef FL_EPSILON_F
// smallest possible float
#define FL_EPSILON_F 1.19209290e-07F
#endif

#ifndef FL_EPSILON_D
// smallest possible double
#define FL_EPSILON_D 2.2204460492503131e-16
#endif

#ifndef FL_ALMOST_EQUAL
#define FL_ALMOST_EQUAL(a, b, small) (FL_ABS((a) - (b)) < small)
#endif

#ifndef FL_ALMOST_EQUAL_FLOAT
#define FL_ALMOST_EQUAL_FLOAT(a, b) (FL_ABS((a) - (b)) < FL_EPSILON_F)
#endif

#ifndef FL_ALMOST_EQUAL_EPSILON
#define FL_ALMOST_EQUAL_EPSILON(a, b, epsilon) (FL_ABS((a) - (b)) < (epsilon))
#endif

#ifndef FL_ALMOST_EQUAL_DOUBLE
#define FL_ALMOST_EQUAL_DOUBLE(a, b) FL_ALMOST_EQUAL_EPSILON(a, b, FL_EPSILON_F)
#endif

#ifndef FL_INFINITY_DOUBLE
#define FL_INFINITY_DOUBLE (1.0 / 0.0)
#endif

#ifndef FL_INFINITY_FLOAT
#define FL_INFINITY_FLOAT (1.0f / 0.0f)
#endif

#ifndef FL_FLT_MAX
#define FL_FLT_MAX 3.402823466e+38F
#endif

#ifndef FL_PI
#define FL_PI 3.1415926535897932384626433832795
#endif

#ifndef FL_E
#define FL_E 2.71828182845904523536
#endif

#ifndef FL_M_PI
#define FL_M_PI FL_PI
#endif
