#pragma once

#include "fl/scoped_array.h"
#include "fl/int.h"
#include "fl/stl/move.h"  // for fl::move

namespace fl {

// Core circular buffer logic operating on caller-owned storage.
// Uses a sentinel slot: the storage must have (capacity + 1) elements.
// Both StaticCircularBuffer and DynamicCircularBuffer compose this.
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

// Static version with compile-time capacity
template <typename T, fl::size N>
class StaticCircularBuffer {
  public:
    StaticCircularBuffer() : mCore(mBuffer, N + 1) {}

    StaticCircularBuffer(const StaticCircularBuffer& other) = default;
    StaticCircularBuffer(StaticCircularBuffer&& other) : mCore(mBuffer, N + 1) {
        for (fl::size i = 0; i < (N + 1); ++i) {
            mBuffer[i] = fl::move(other.mBuffer[i]);
        }
        mCore.setHead(other.mCore.head());
        mCore.setTail(other.mCore.tail());
        other.clear();
    }

    StaticCircularBuffer& operator=(const StaticCircularBuffer& other) {
        if (this != &other) {
            for (fl::size i = 0; i < (N + 1); ++i) {
                mBuffer[i] = other.mBuffer[i];
            }
            mCore.setHead(other.mCore.head());
            mCore.setTail(other.mCore.tail());
        }
        return *this;
    }

    StaticCircularBuffer& operator=(StaticCircularBuffer&& other) {
        if (this != &other) {
            for (fl::size i = 0; i < (N + 1); ++i) {
                mBuffer[i] = fl::move(other.mBuffer[i]);
            }
            mCore.setHead(other.mCore.head());
            mCore.setTail(other.mCore.tail());
            other.clear();
        }
        return *this;
    }

    void push(const T &value) { mCore.push_back(value); }
    bool push_back(const T &value) { return mCore.push_back(value); }

    bool pop(T &value) { return mCore.pop_front(&value); }
    bool pop_front(T *dst = nullptr) { return mCore.pop_front(dst); }

    T& front() { return mCore.front(); }
    const T& front() const { return mCore.front(); }

    T& back() { return mCore.back(); }
    const T& back() const { return mCore.back(); }

    T& operator[](fl::size index) { return mCore[index]; }
    const T& operator[](fl::size index) const { return mCore[index]; }

    fl::size size() const { return mCore.size(); }
    constexpr fl::size capacity() const { return N; }
    bool empty() const { return mCore.empty(); }
    bool full() const { return mCore.full(); }
    void clear() { mCore.clear(); }

  private:
    T mBuffer[N + 1]; // Extra space for distinguishing full/empty
    CircularBufferCore<T> mCore;
};

// Dynamic version with runtime capacity
template <typename T> class DynamicCircularBuffer {
  public:
    DynamicCircularBuffer(fl::size capacity)
        : mBuffer(new T[capacity + 1]),
          mCore(mBuffer.get(), capacity + 1) {}

    DynamicCircularBuffer(const DynamicCircularBuffer &) = delete;
    DynamicCircularBuffer &operator=(const DynamicCircularBuffer &) = delete;

    DynamicCircularBuffer(DynamicCircularBuffer&& other) noexcept
        : mBuffer(fl::move(other.mBuffer)),
          mCore(mBuffer.get(), other.mCore.capacity() + 1) {
        mCore.setHead(other.mCore.head());
        mCore.setTail(other.mCore.tail());
        other.mCore.assign(nullptr, 0);
    }

    DynamicCircularBuffer& operator=(DynamicCircularBuffer&& other) noexcept {
        if (this != &other) {
            mBuffer = fl::move(other.mBuffer);
            fl::size alloc = other.mCore.capacity() + 1;
            mCore.assign(mBuffer.get(), alloc);
            mCore.setHead(other.mCore.head());
            mCore.setTail(other.mCore.tail());
            other.mCore.assign(nullptr, 0);
        }
        return *this;
    }

    bool push_back(const T &value) { return mCore.push_back(value); }
    bool pop_front(T *dst = nullptr) { return mCore.pop_front(dst); }
    bool push_front(const T &value) { return mCore.push_front(value); }
    bool pop_back(T *dst = nullptr) { return mCore.pop_back(dst); }

    T &front() { return mCore.front(); }
    const T &front() const { return mCore.front(); }

    T &back() { return mCore.back(); }
    const T &back() const { return mCore.back(); }

    T &operator[](fl::size index) { return mCore[index]; }
    const T &operator[](fl::size index) const { return mCore[index]; }

    fl::size size() const { return mCore.size(); }
    fl::size capacity() const { return mCore.capacity(); }
    bool empty() const { return mCore.empty(); }
    bool full() const { return mCore.full(); }
    void clear() { mCore.clear(); }

  private:
    fl::scoped_array<T> mBuffer;
    CircularBufferCore<T> mCore;
};

// For backward compatibility, keep the old name for the dynamic version
template <typename T>
using CircularBuffer = DynamicCircularBuffer<T>;

} // namespace fl
