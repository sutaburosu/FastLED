#pragma once

#include "fl/circular_buffer.h"
#include "fl/stl/span.h"

namespace fl {
namespace detail {

template <typename T, fl::size N = 0>
class GaussianFilterImpl {
  public:
    GaussianFilterImpl() : mLastValue(T(0)) {}
    explicit GaussianFilterImpl(fl::size capacity)
        : mBuf(capacity), mLastValue(T(0)) {}

    T update(T input) {
        mBuf.push_back(input);
        return recompute();
    }

    T update(fl::span<const T> values) {
        if (values.size() == 0) return mLastValue;
        for (fl::size i = 0; i < values.size(); ++i) {
            mBuf.push_back(values[i]);
        }
        return recompute();
    }

    T value() const { return mLastValue; }
    void reset() { mBuf.clear(); mLastValue = T(0); }
    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = CircularBuffer<T, N>(new_capacity);
        mLastValue = T(0);
    }

  private:
    T recompute() {
        fl::size n = mBuf.size();
        if (n == 0) { mLastValue = T(0); return mLastValue; }

        T weighted_sum = T(0);
        T weight_total = T(0);
        fl::size binom = 1;
        for (fl::size i = 0; i < n; ++i) {
            T w = T(static_cast<float>(binom));
            weighted_sum = weighted_sum + mBuf[i] * w;
            weight_total = weight_total + w;
            if (i + 1 < n) {
                binom = binom * (n - 1 - i) / (i + 1);
            }
        }
        mLastValue = weighted_sum / weight_total;
        return mLastValue;
    }

    CircularBuffer<T, N> mBuf;
    T mLastValue;
};

} // namespace detail
} // namespace fl
