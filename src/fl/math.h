#pragma once

// Generic math functions dispatching between float and fixed_point types.
// Float/double: forwards to fl::expf/fl::sinf/etc. from stl/math.h.
// Fixed-point:  forwards to T::pow/T::sin/fl::expfp/etc. from fixed_point.h.

#include "fl/fixed_point.h"
#include "fl/stl/math.h"
#include "fl/stl/type_traits.h"

namespace fl {

// --- exp(x) ---
template <typename T>
inline typename enable_if<is_fixed_point<T>::value, T>::type
exp(T x) { return fl::expfp(x); }

// --- pow(base, exponent) ---
template <typename T>
inline typename enable_if<is_fixed_point<T>::value, T>::type
pow(T base, T exponent) { return fl::powfp(base, exponent); }

// --- sin(x) ---
template <typename T>
inline typename enable_if<is_fixed_point<T>::value, T>::type
sin(T x) { return T::sin(x); }

// --- cos(x) ---
template <typename T>
inline typename enable_if<is_fixed_point<T>::value, T>::type
cos(T x) { return T::cos(x); }

// --- sincos(angle, &out_sin, &out_cos) ---
template <typename T>
inline typename enable_if<is_floating_point<T>::value>::type
sincos(T angle, T& out_sin, T& out_cos) {
    out_sin = static_cast<T>(fl::sinf(static_cast<float>(angle)));
    out_cos = static_cast<T>(fl::cosf(static_cast<float>(angle)));
}

template <typename T>
inline typename enable_if<is_fixed_point<T>::value>::type
sincos(T angle, T& out_sin, T& out_cos) {
    T::sincos(angle, out_sin, out_cos);
}

// --- sqrt(x) ---
template <typename T>
inline typename enable_if<is_fixed_point<T>::value, T>::type
sqrt(T x) { return T::sqrt(x); }

} // namespace fl
