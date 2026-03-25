#pragma once

/// @file basic_string.h
/// @brief Type-erased base class for fl::StrN<N>.
/// All string logic lives here, compiled once. StrN<N> is a thin wrapper
/// that provides the inline buffer storage and delegates everything here.

#include "fl/stl/int.h"
#include "fl/stl/cstring.h"
#include "fl/stl/detail/string_holder.h"
#include "fl/stl/cctype.h"
#include "fl/stl/charconv.h"
#include "fl/stl/not_null.h"
#include "fl/stl/compiler_control.h"
#include "fl/stl/shared_ptr.h"
#include "fl/stl/type_traits.h"
#include "fl/stl/variant.h"
#include "fl/stl/iterator.h"
#include "fl/stl/bit_cast.h"
#include "fl/stl/cstddef.h"
#include "fl/stl/move.h"
#include "fl/math/math.h"

namespace fl {

// Forward declaration
class string_view;

// Define shared_ptr type for StringHolder
using StringHolderPtr = fl::shared_ptr<fl::StringHolder>;
using NotNullStringHolderPtr = fl::not_null<StringHolderPtr>;

/// Type-erased string base class. Holds all string logic.
/// StrN<N> inherits this and provides the inline buffer.
/// This class cannot be constructed directly — only through StrN<N>.
class basic_string {
  public:
    // ======= NESTED TYPES =======

    // Non-owning pointer to constant null-terminated string data.
    struct ConstLiteral {
        const char* data;
        constexpr ConstLiteral() : data(nullptr) {}
        constexpr explicit ConstLiteral(const char* str) : data(str) {}
    };

    // Non-owning pointer + length to constant string data.
    struct ConstView {
        const char* data;
        fl::size length;
        constexpr ConstView() : data(nullptr), length(0) {}
        constexpr ConstView(const char* str, fl::size len) : data(str), length(len) {}
    };

    class iterator {
    private:
        char* ptr;
    public:
        typedef char value_type;
        typedef char& reference;
        typedef char* pointer;
        typedef fl::ptrdiff_t difference_type;
        typedef fl::random_access_iterator_tag iterator_category;

        iterator() : ptr(nullptr) {}
        explicit iterator(char* p) : ptr(p) {}

        reference operator*() const { return *ptr; }
        pointer operator->() const { return ptr; }
        iterator& operator++() { ++ptr; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++ptr; return tmp; }
        iterator& operator--() { --ptr; return *this; }
        iterator operator--(int) { iterator tmp = *this; --ptr; return tmp; }

        iterator operator+(difference_type n) const { return iterator(ptr + n); }
        iterator operator-(difference_type n) const { return iterator(ptr - n); }
        iterator& operator+=(difference_type n) { ptr += n; return *this; }
        iterator& operator-=(difference_type n) { ptr -= n; return *this; }
        difference_type operator-(const iterator& other) const { return ptr - other.ptr; }
        reference operator[](difference_type n) const { return ptr[n]; }

        bool operator==(const iterator& other) const { return ptr == other.ptr; }
        bool operator!=(const iterator& other) const { return ptr != other.ptr; }
        bool operator<(const iterator& other) const { return ptr < other.ptr; }
        bool operator>(const iterator& other) const { return ptr > other.ptr; }
        bool operator<=(const iterator& other) const { return ptr <= other.ptr; }
        bool operator>=(const iterator& other) const { return ptr >= other.ptr; }

        operator char*() const { return ptr; }
    };

    class const_iterator {
    private:
        const char* ptr;
    public:
        typedef char value_type;
        typedef const char& reference;
        typedef const char* pointer;
        typedef fl::ptrdiff_t difference_type;
        typedef fl::random_access_iterator_tag iterator_category;

        const_iterator() : ptr(nullptr) {}
        explicit const_iterator(const char* p) : ptr(p) {}
        const_iterator(const iterator& it) : ptr(it.operator char*()) {}

        reference operator*() const { return *ptr; }
        pointer operator->() const { return ptr; }
        const_iterator& operator++() { ++ptr; return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++ptr; return tmp; }
        const_iterator& operator--() { --ptr; return *this; }
        const_iterator operator--(int) { const_iterator tmp = *this; --ptr; return tmp; }

        const_iterator operator+(difference_type n) const { return const_iterator(ptr + n); }
        const_iterator operator-(difference_type n) const { return const_iterator(ptr - n); }
        const_iterator& operator+=(difference_type n) { ptr += n; return *this; }
        const_iterator& operator-=(difference_type n) { ptr -= n; return *this; }
        difference_type operator-(const const_iterator& other) const { return ptr - other.ptr; }
        reference operator[](difference_type n) const { return ptr[n]; }

        bool operator==(const const_iterator& other) const { return ptr == other.ptr; }
        bool operator!=(const const_iterator& other) const { return ptr != other.ptr; }
        bool operator<(const const_iterator& other) const { return ptr < other.ptr; }
        bool operator>(const const_iterator& other) const { return ptr > other.ptr; }
        bool operator<=(const const_iterator& other) const { return ptr <= other.ptr; }
        bool operator>=(const const_iterator& other) const { return ptr >= other.ptr; }

        operator const char*() const { return ptr; }
    };

    typedef fl::reverse_iterator<iterator> reverse_iterator;
    typedef fl::reverse_iterator<const_iterator> const_reverse_iterator;

    // ======= CONSTANTS =======
    static constexpr fl::size npos = static_cast<fl::size>(-1);

    // ======= STORAGE TYPE QUERIES =======
    bool is_literal() const { return mStorage.is<ConstLiteral>(); }
    bool is_view() const { return mStorage.is<ConstView>(); }
    bool is_owning() const {
        return isInline() || mStorage.is<NotNullStringHolderPtr>();
    }
    bool is_referencing() const { return is_literal() || is_view(); }

    // ======= ACCESSORS =======
    fl::size size() const { return mLength; }
    fl::size length() const { return mLength; }
    bool empty() const { return mLength == 0; }
    const char* c_str() const;
    const char* data() const { return c_str(); }
    char* c_str_mutable();
    fl::size capacity() const;

    // ======= ELEMENT ACCESS =======
    char operator[](fl::size index) const;
    char& operator[](fl::size index);
    char& at(fl::size pos);
    const char& at(fl::size pos) const;
    char front() const;
    char back() const;
    char charAt(fl::size index) const;

    // ======= ITERATORS =======
    iterator begin() { return iterator(c_str_mutable()); }
    iterator end() { return iterator(c_str_mutable() + mLength); }
    const_iterator begin() const { return const_iterator(c_str()); }
    const_iterator end() const { return const_iterator(c_str() + mLength); }
    const_iterator cbegin() const { return const_iterator(c_str()); }
    const_iterator cend() const { return const_iterator(c_str() + mLength); }
    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator crend() const { return const_reverse_iterator(begin()); }

    // ======= COMPARISON OPERATORS =======
    bool operator==(const basic_string& other) const;
    bool operator!=(const basic_string& other) const;
    bool operator<(const basic_string& other) const;
    bool operator>(const basic_string& other) const;
    bool operator<=(const basic_string& other) const;
    bool operator>=(const basic_string& other) const;

    bool operator==(const char* other) const;
    bool operator!=(const char* other) const;

    // ======= WRITE OPERATIONS =======
    fl::size write(const fl::u8* data, fl::size n);
    fl::size write(const char* str, fl::size n);
    fl::size write(char c);
    fl::size write(fl::u8 c);
    fl::size write(const fl::u16& n);
    fl::size write(const fl::u32& val);
    fl::size write(const u64& val);
    fl::size write(const i64& val);
    fl::size write(const fl::i32& val);
    fl::size write(const fl::i8 val);

    // Generic write for multi-byte integer types
    template <typename T>
    typename fl::enable_if<fl::is_multi_byte_integer<T>::value, fl::size>::type
    write(const T& val) {
        using target_t = typename int_cast_detail::cast_target<T>::type;
        char buf[64] = {0};
        int len;
        if (fl::is_signed<target_t>::value) {
            len = fl::itoa(static_cast<fl::i32>(val), buf, 10);
        } else {
            len = fl::utoa32(static_cast<fl::u32>(val), buf, 10);
        }
        return write(buf, len);
    }

    // ======= COPY OPERATIONS =======
    void copy(const char* str);
    void copy(const char* str, fl::size len);
    void copy(const basic_string& other);
    fl::size copy(char* dest, fl::size count, fl::size pos = 0) const;

    // ======= ASSIGN OPERATIONS =======
    void assign(const char* str, fl::size len);
    basic_string& assign(const basic_string& str);
    basic_string& assign(const basic_string& str, fl::size pos, fl::size count = npos);
    basic_string& assign(fl::size count, char c);
    basic_string& assign(basic_string&& str) noexcept;

    // Assign from iterator range
    template <typename InputIt>
    basic_string& assign(InputIt first, InputIt last) {
        clear();
        fl::size len = 0;
        for (auto it = first; it != last; ++it) {
            ++len;
        }
        if (len == 0) {
            return *this;
        }
        mLength = len;
        if (len + 1 <= mInlineCapacity) {
            if (!isInline()) {
                mStorage.reset();
            }
            fl::size i = 0;
            for (auto it = first; it != last; ++it, ++i) {
                inlineBufferPtr()[i] = *it;
            }
            inlineBufferPtr()[len] = '\0';
        } else {
            mStorage = NotNullStringHolderPtr(fl::make_shared<StringHolder>(len));
            NotNullStringHolderPtr& ptr = heapData();
            fl::size i = 0;
            for (auto it = first; it != last; ++it, ++i) {
                ptr->data()[i] = *it;
            }
            ptr->data()[len] = '\0';
        }
        return *this;
    }

    // ======= FIND OPERATIONS =======
    fl::size find(const char& value) const;
    fl::size find(const char* substr) const;
    fl::size find(const basic_string& other) const;
    fl::size find(const char& value, fl::size start_pos) const;
    fl::size find(const char* substr, fl::size start_pos) const;
    fl::size find(const basic_string& other, fl::size start_pos) const;

    fl::size rfind(char c, fl::size pos = npos) const;
    fl::size rfind(const char* s, fl::size pos, fl::size count) const;
    fl::size rfind(const char* s, fl::size pos = npos) const;
    fl::size rfind(const basic_string& str, fl::size pos = npos) const;

    fl::size find_first_of(char c, fl::size pos = 0) const;
    fl::size find_first_of(const char* s, fl::size pos, fl::size count) const;
    fl::size find_first_of(const char* s, fl::size pos = 0) const;
    fl::size find_first_of(const basic_string& str, fl::size pos = 0) const;

    fl::size find_last_of(char c, fl::size pos = npos) const;
    fl::size find_last_of(const char* s, fl::size pos, fl::size count) const;
    fl::size find_last_of(const char* s, fl::size pos = npos) const;
    fl::size find_last_of(const basic_string& str, fl::size pos = npos) const;

    fl::size find_first_not_of(char c, fl::size pos = 0) const;
    fl::size find_first_not_of(const char* s, fl::size pos, fl::size count) const;
    fl::size find_first_not_of(const char* s, fl::size pos = 0) const;
    fl::size find_first_not_of(const basic_string& str, fl::size pos = 0) const;

    fl::size find_last_not_of(char c, fl::size pos = npos) const;
    fl::size find_last_not_of(const char* s, fl::size pos, fl::size count) const;
    fl::size find_last_not_of(const char* s, fl::size pos = npos) const;
    fl::size find_last_not_of(const basic_string& str, fl::size pos = npos) const;

    // ======= CONTAINS / STARTS_WITH / ENDS_WITH =======
    bool contains(const char* substr) const;
    bool contains(char c) const;
    bool contains(const basic_string& other) const;

    bool starts_with(const char* prefix) const;
    bool starts_with(char c) const;
    bool starts_with(const basic_string& prefix) const;

    bool ends_with(const char* suffix) const;
    bool ends_with(char c) const;
    bool ends_with(const basic_string& suffix) const;

    // ======= STACK OPERATIONS =======
    void push_back(char c);
    void push_ascii(char c);
    void pop_back();

    // ======= APPEND OPERATIONS =======
    basic_string& append(const char* str);
    basic_string& append(const char* str, fl::size len);
    basic_string& append(char c);
    basic_string& append(const i8& val);
    basic_string& append(const u8& val);
    basic_string& append(const bool& val);
    basic_string& append(const i16& val);
    basic_string& append(const u16& val);
    basic_string& append(const i32& val);
    basic_string& append(const u32& val);
    basic_string& append(const i64& val);
    basic_string& append(const u64& val);
    basic_string& append(const float& val);
    basic_string& append(const float& val, int precision);
    basic_string& append(const double& val);
    basic_string& append(const basic_string& str);

    // SFINAE catch-all for integer types not covered by explicit overloads
    // (e.g. unsigned long on Windows, which is distinct from both u32 and u64).
    // Casts to the appropriate fl:: type via cast_target.
    template<typename T>
    typename fl::enable_if<fl::is_multi_byte_integer<T>::value
        && !fl::is_same<T, i8>::value  && !fl::is_same<T, u8>::value
        && !fl::is_same<T, i16>::value && !fl::is_same<T, u16>::value
        && !fl::is_same<T, i32>::value && !fl::is_same<T, u32>::value
        && !fl::is_same<T, i64>::value && !fl::is_same<T, u64>::value,
        basic_string&>::type
    append(T val) {
        using target_t = typename int_cast_detail::cast_target<T>::type;
        return append(static_cast<target_t>(val));
    }

    // ======= HEX/OCT APPEND =======
    basic_string& appendHex(i32 val);
    basic_string& appendHex(u32 val);
    basic_string& appendHex(i64 val);
    basic_string& appendHex(u64 val);
    basic_string& appendHex(i16 val);
    basic_string& appendHex(u16 val);
    basic_string& appendHex(i8 val);
    basic_string& appendHex(u8 val);
    basic_string& appendOct(i32 val);
    basic_string& appendOct(u32 val);
    basic_string& appendOct(i64 val);
    basic_string& appendOct(u64 val);
    basic_string& appendOct(i16 val);
    basic_string& appendOct(u16 val);
    basic_string& appendOct(i8 val);
    basic_string& appendOct(u8 val);

    // ======= INSERT =======
    basic_string& insert(fl::size pos, fl::size count, char ch);
    basic_string& insert(fl::size pos, const char* s);
    basic_string& insert(fl::size pos, const char* s, fl::size count);
    basic_string& insert(fl::size pos, const basic_string& str);
    basic_string& insert(fl::size pos, const basic_string& str, fl::size pos2, fl::size count = npos);

    // ======= ERASE =======
    basic_string& erase(fl::size pos = 0, fl::size count = npos);

    // Iterator-based erase (SFINAE for pointer types)
    template<typename T>
    typename fl::enable_if<fl::is_pointer<T>::value && fl::is_same<typename fl::remove_cv<typename fl::remove_pointer<T>::type>::type, char>::value, char*>::type
    erase(T it_pos) {
        if (!it_pos) return end();
        const char* str_begin = c_str();
        const char* str_end = str_begin + mLength;
        if (it_pos < str_begin || it_pos >= str_end) return end();
        fl::size pos = it_pos - str_begin;
        erase(pos, 1);
        return begin() + pos;
    }

    template<typename T>
    typename fl::enable_if<fl::is_pointer<T>::value && fl::is_same<typename fl::remove_cv<typename fl::remove_pointer<T>::type>::type, char>::value, char*>::type
    erase(T first, T last) {
        if (!first || !last || first >= last) return end();
        const char* str_begin = c_str();
        const char* str_end = str_begin + mLength;
        if (first < str_begin) first = begin();
        if (last > str_end) last = end();
        if (first >= str_end) return end();
        fl::size pos = first - str_begin;
        fl::size count = last - first;
        erase(pos, count);
        return begin() + pos;
    }

    // ======= REPLACE =======
    basic_string& replace(fl::size pos, fl::size count, const basic_string& str);
    basic_string& replace(fl::size pos, fl::size count, const basic_string& str,
                     fl::size pos2, fl::size count2 = npos);
    basic_string& replace(fl::size pos, fl::size count, const char* s, fl::size count2);
    basic_string& replace(fl::size pos, fl::size count, const char* s);
    basic_string& replace(fl::size pos, fl::size count, fl::size count2, char ch);

    // ======= COMPARE =======
    int compare(const basic_string& str) const;
    int compare(fl::size pos1, fl::size count1, const basic_string& str) const;
    int compare(fl::size pos1, fl::size count1, const basic_string& str,
                fl::size pos2, fl::size count2 = npos) const;
    int compare(const char* s) const;
    int compare(fl::size pos1, fl::size count1, const char* s) const;
    int compare(fl::size pos1, fl::size count1, const char* s, fl::size count2) const;

    // ======= MEMORY MANAGEMENT =======
    void reserve(fl::size newCapacity);
    void clear(bool freeMemory = false);
    void shrink_to_fit();
    fl::size max_size() const { return (npos / 2) - 1; }
    void resize(fl::size count);
    void resize(fl::size count, char ch);

    // ======= OTHER =======
    float toFloat() const;

    // ======= DESTRUCTOR =======
    ~basic_string() {}

  protected:
    // ======= CONSTRUCTION (only callable by StrN<N>) =======
    basic_string(char* inlineBuffer, fl::size inlineCapacity)
        : mInlineOffset(static_cast<fl::size>(
              inlineBuffer -
              static_cast<char*>(static_cast<void*>(this))))
        , mInlineCapacity(inlineCapacity)
        , mLength(0)
        , mStorage() // empty variant = inline mode
    {}

    // Deleted copy/move — StrN<N> handles these
    basic_string(const basic_string&) = delete;
    basic_string(basic_string&&) = delete;
    basic_string& operator=(const basic_string&) = delete;
    basic_string& operator=(basic_string&&) = delete;

    // ======= CONTENT POPULATION (for StrN<N> constructors) =======
    void moveFrom(basic_string&& other) noexcept;
    void moveAssign(basic_string&& other) noexcept;
    void swapWith(basic_string& other);

    // Factory helpers
    void setLiteral(const char* literal);
    void setView(const char* data, fl::size len);
    void setSharedHolder(const fl::shared_ptr<StringHolder>& holder);

    // ======= DATA MEMBERS =======
    // Store offset from `this` to the inline buffer, not a raw pointer.
    // This survives trivial relocation (bitwise copy by containers) because
    // the offset is relative to `this` which updates with the object.
    fl::size mInlineOffset;
    fl::size mInlineCapacity;
    fl::size mLength;
    fl::variant<NotNullStringHolderPtr, ConstLiteral, ConstView> mStorage;

    // ======= HELPER METHODS =======
    // Compute inline buffer pointer from offset (survives trivial relocation)
    char* inlineBufferPtr() {
        return static_cast<char*>(static_cast<void*>(this)) + mInlineOffset;
    }
    const char* inlineBufferPtr() const {
        return static_cast<const char*>(static_cast<const void*>(this)) + mInlineOffset;
    }

    bool isInline() const { return mStorage.empty(); }
    bool hasHeapData() const { return mStorage.is<NotNullStringHolderPtr>(); }
    bool hasConstLiteral() const { return mStorage.is<ConstLiteral>(); }
    bool hasConstView() const { return mStorage.is<ConstView>(); }
    bool isNonOwning() const { return hasConstLiteral() || hasConstView(); }

    const char* constData() const;
    void materialize();

    NotNullStringHolderPtr& heapData();
    const NotNullStringHolderPtr& heapData() const;
};

} // namespace fl
