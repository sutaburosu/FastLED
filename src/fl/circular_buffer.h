#pragma once

#include "fl/int.h"
#include "fl/stl/move.h"    // for fl::move
#include "fl/stl/vector.h"  // for fl::vector_inlined

namespace fl {

// Core circular buffer logic operating on caller-owned storage.
// Uses a sentinel slot: the storage must have (capacity + 1) elements.
// Both static and dynamic CircularBuffer compose this.
template <typename T>
class CircularBufferCore {
  public:
    CircularBufferCore() : mData(nullptr), mAllocSize(0), mHead(0), mTail(0) {}

    CircularBufferCore(T* data, fl::size alloc_size)
        : mData(data), mAllocSize(alloc_size), mHead(0), mTail(0) {}

    void assign(T* data, fl::size alloc_size) {
        mData = data;
        mAllocSize = alloc_size;
        mHead = 0;
        mTail = 0;
    }

    bool push_back(const T &value) {
        if (full()) {
            mTail = increment(mTail);
        }
        mData[mHead] = value;
        mHead = increment(mHead);
        return true;
    }

    bool pop_front(T *dst = nullptr) {
        if (empty()) {
            return false;
        }
        if (dst) {
            *dst = fl::move(mData[mTail]);
        }
        mData[mTail] = T();
        mTail = increment(mTail);
        return true;
    }

    bool push_front(const T &value) {
        if (full()) {
            mHead = decrement(mHead);
        }
        mTail = decrement(mTail);
        mData[mTail] = value;
        return true;
    }

    bool pop_back(T *dst = nullptr) {
        if (empty()) {
            return false;
        }
        mHead = decrement(mHead);
        if (dst) {
            *dst = fl::move(mData[mHead]);
        }
        mData[mHead] = T();
        return true;
    }

    T& front() { return mData[mTail]; }
    const T& front() const { return mData[mTail]; }

    T& back() { return mData[decrement(mHead)]; }
    const T& back() const { return mData[decrement(mHead)]; }

    T& operator[](fl::size index) {
        return mData[(mTail + index) % mAllocSize];
    }
    const T& operator[](fl::size index) const {
        return mData[(mTail + index) % mAllocSize];
    }

    fl::size size() const {
        if (mAllocSize == 0) return 0;
        return (mHead + mAllocSize - mTail) % mAllocSize;
    }
    fl::size capacity() const { return mAllocSize > 0 ? mAllocSize - 1 : 0; }
    bool empty() const { return mAllocSize == 0 || mHead == mTail; }
    bool full() const { return mAllocSize > 0 && increment(mHead) == mTail; }

    void clear() {
        while (!empty()) {
            pop_front();
        }
    }

    // Expose head/tail for move operations.
    fl::size head() const { return mHead; }
    fl::size tail() const { return mTail; }
    void setHead(fl::size h) { mHead = h; }
    void setTail(fl::size t) { mTail = t; }

  private:
    fl::size increment(fl::size index) const { return (index + 1) % mAllocSize; }
    fl::size decrement(fl::size index) const {
        return (index + mAllocSize - 1) % mAllocSize;
    }

    T* mData;
    fl::size mAllocSize; // capacity + 1 (includes sentinel slot)
    fl::size mHead;
    fl::size mTail;
};

// Unified circular buffer: N > 0 for inline storage, N == 0 for dynamic.
// Uses vector_inlined internally — inline when N > 0, heap when N == 0.
template <typename T, fl::size N = 0>
class CircularBuffer {
  public:
    // Default constructor — pre-sizes to N+1 (useful when N > 0).
    CircularBuffer() {
        mStorage.resize(N + 1);
        mCore.assign(mStorage.data(), N + 1);
    }

    // Capacity constructor — for dynamic (N==0) or overriding static size.
    explicit CircularBuffer(fl::size capacity) {
        mStorage.resize(capacity + 1);
        mCore.assign(mStorage.data(), capacity + 1);
    }

    CircularBuffer(const CircularBuffer& other)
        : mStorage(other.mStorage),
          mCore(mStorage.data(), mStorage.size()) {
        mCore.setHead(other.mCore.head());
        mCore.setTail(other.mCore.tail());
    }

    CircularBuffer(CircularBuffer&& other)
        : mStorage(fl::move(other.mStorage)),
          mCore(mStorage.data(), mStorage.size()) {
        mCore.setHead(other.mCore.head());
        mCore.setTail(other.mCore.tail());
        other.mCore.assign(nullptr, 0);
    }

    CircularBuffer& operator=(const CircularBuffer& other) {
        if (this != &other) {
            mStorage = other.mStorage;
            mCore.assign(mStorage.data(), mStorage.size());
            mCore.setHead(other.mCore.head());
            mCore.setTail(other.mCore.tail());
        }
        return *this;
    }

    CircularBuffer& operator=(CircularBuffer&& other) {
        if (this != &other) {
            mStorage = fl::move(other.mStorage);
            mCore.assign(mStorage.data(), mStorage.size());
            mCore.setHead(other.mCore.head());
            mCore.setTail(other.mCore.tail());
            other.mCore.assign(nullptr, 0);
        }
        return *this;
    }

    void push(const T &value) { mCore.push_back(value); }
    bool push_back(const T &value) { return mCore.push_back(value); }
    bool push_front(const T &value) { return mCore.push_front(value); }

    bool pop(T &value) { return mCore.pop_front(&value); }
    bool pop_front(T *dst = nullptr) { return mCore.pop_front(dst); }
    bool pop_back(T *dst = nullptr) { return mCore.pop_back(dst); }

    T& front() { return mCore.front(); }
    const T& front() const { return mCore.front(); }

    T& back() { return mCore.back(); }
    const T& back() const { return mCore.back(); }

    T& operator[](fl::size index) { return mCore[index]; }
    const T& operator[](fl::size index) const { return mCore[index]; }

    fl::size size() const { return mCore.size(); }
    fl::size capacity() const { return mCore.capacity(); }
    bool empty() const { return mCore.empty(); }
    bool full() const { return mCore.full(); }
    void clear() { mCore.clear(); }

    void resize(fl::size new_capacity) {
        // Save existing elements
        fl::size count = mCore.size();
        fl::size to_save = (count < new_capacity) ? count : new_capacity;
        // Use a temporary storage to save elements
        vector_inlined<T, (N > 0 ? N + 1 : 1)> saved;
        saved.resize(to_save);
        for (fl::size i = 0; i < to_save; ++i) {
            saved[i] = mCore[i];
        }
        // Resize storage
        mStorage.resize(new_capacity + 1);
        mCore.assign(mStorage.data(), new_capacity + 1);
        // Re-insert saved elements
        for (fl::size i = 0; i < to_save; ++i) {
            mCore.push_back(saved[i]);
        }
    }

  private:
    vector_inlined<T, (N > 0 ? N + 1 : 1)> mStorage;
    CircularBufferCore<T> mCore;
};

// Deprecated aliases for backward compatibility
template <typename T, fl::size N>
using StaticCircularBuffer = CircularBuffer<T, N>;

template <typename T>
using DynamicCircularBuffer = CircularBuffer<T, 0>;

} // namespace fl
