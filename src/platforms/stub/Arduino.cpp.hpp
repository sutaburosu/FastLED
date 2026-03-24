// IWYU pragma: private

// ok no namespace fl

#include "platforms/wasm/is_wasm.h"
#if (defined(FASTLED_USE_STUB_ARDUINO) || defined(FL_IS_WASM)) && !defined(FASTLED_NO_ARDUINO_STUBS)
// STUB platform implementation - excluded for WASM builds which provide their own Arduino.cpp
// Also excluded when FASTLED_NO_ARDUINO_STUBS is defined (for compatibility with ArduinoFake, etc.)

#include "platforms/stub/Arduino.h"  // ok include

#include "fl/stl/map.h"
#include "fl/stl/stdio.h"
#include "fl/math/math.h"
#include "fl/stl/cstdlib.h"

// Arduino map() function - in global namespace (NOT in fl::)
// fl::map refers to the map container (red-black tree)
// Delegates to fl::map_range() for consistent behavior and overflow protection
long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return fl::map_range<long, long>(x, in_min, in_max, out_min, out_max);
}

// Random number generation functions
long random(long min, long max) {
    if (min == max) {
        return min;
    }
    if (min > max) {
        // Swap if inverted (Arduino behavior)
        long tmp = min;
        min = max;
        max = tmp;
    }
    // Arduino random() is exclusive of max
    long range = max - min;
    if (range <= 0) {
        return min;
    }
    return min + (rand() % range);
}

long random(long max) {
    return random(0, max);
}

// Analog value storage for test injection
// Key: pin number, Value: analog value (or -1 for unset/random)
static fl::map<int, int> g_analog_values;

int analogRead(int pin) {
    // Check if a test value has been set for this pin
    auto it = g_analog_values.find(pin);
    if (it != g_analog_values.end() && it->second >= 0) {
        return it->second;
    }
    // Default: return random value (0-1023 for 10-bit ADC emulation)
    return random(0, 1024);  // Arduino random is exclusive of max
}

// Test helper functions for analog value injection
void setAnalogValue(int pin, int value) {
    g_analog_values[pin] = value;
}

int getAnalogValue(int pin) {
    auto it = g_analog_values.find(pin);
    if (it != g_analog_values.end()) {
        return it->second;
    }
    return -1;  // Not set
}

void clearAnalogValues() {
    g_analog_values.clear();
}

// Arduino hardware initialization (stub: does nothing)
void init() {
    // On real Arduino platforms, init() sets up timers, interrupts, and other hardware.
    // On stub platform, there's no hardware to initialize, so this is a no-op.
}

// Digital I/O functions
void digitalWrite(int, int) {}
void analogWrite(int, int) {}
void analogReference(int) {}
int digitalRead(int) { return LOW; }
void pinMode(int, int) {}

// SerialEmulation member functions
void SerialEmulation::begin(int) {}

// Two-argument floating point print: print(float/double, digits)
template <typename T>
typename fl::enable_if<fl::is_floating_point<T>::value>::type
SerialEmulation::print(T val, int digits) {
    digits = digits < 0 ? 0 : (digits > 9 ? 9 : digits);
    double d = static_cast<double>(val);
    switch(digits) {
        case 0: fl::printf("%.0f", d); break;
        case 1: fl::printf("%.1f", d); break;
        case 2: fl::printf("%.2f", d); break;
        case 3: fl::printf("%.3f", d); break;
        case 4: fl::printf("%.4f", d); break;
        case 5: fl::printf("%.5f", d); break;
        case 6: fl::printf("%.6f", d); break;
        case 7: fl::printf("%.7f", d); break;
        case 8: fl::printf("%.8f", d); break;
        case 9: fl::printf("%.9f", d); break;
    }
}

// Two-argument integer print: print(integer, base)
// Works for all signed/unsigned integer types; char/u8 print as numbers
template <typename T>
typename fl::enable_if<fl::is_integral<T>::value &&
                       !fl::is_same<typename fl::remove_cv<T>::type, bool>::value>::type
SerialEmulation::print(T val, int base) {
    // Use long long to handle all integer sizes uniformly
    if (fl::is_signed<T>::value) {
        long long v = static_cast<long long>(val);
        if (base == 16) fl::printf("%llx", v);
        else if (base == 8) fl::printf("%llo", v);
        else if (base == 2) {
            int bits = static_cast<int>(sizeof(T) * 8);
            for (int i = bits - 1; i >= 0; i--) {
                fl::printf("%d", (int)((v >> i) & 1));
            }
        }
        else fl::printf("%lld", v);
    } else {
        unsigned long long v = static_cast<unsigned long long>(val);
        if (base == 16) fl::printf("%llx", v);
        else if (base == 8) fl::printf("%llo", v);
        else if (base == 2) {
            int bits = static_cast<int>(sizeof(T) * 8);
            for (int i = bits - 1; i >= 0; i--) {
                fl::printf("%d", (int)((v >> i) & 1));
            }
        }
        else fl::printf("%llu", v);
    }
}

// println variants just delegate to print + newline
template <typename T>
typename fl::enable_if<fl::is_floating_point<T>::value>::type
SerialEmulation::println(T val, int digits) {
    print(val, digits);
    fl::printf("\n");
}

template <typename T>
typename fl::enable_if<fl::is_integral<T>::value &&
                       !fl::is_same<typename fl::remove_cv<T>::type, bool>::value>::type
SerialEmulation::println(T val, int base) {
    print(val, base);
    fl::printf("\n");
}

void SerialEmulation::println() {
    fl::printf("\n");
}

int SerialEmulation::available() {
    return 0;
}

int SerialEmulation::read() {
    return 0;
}

fl::string SerialEmulation::readStringUntil(char terminator) {
    (void)terminator;
    // Stub implementation: returns empty string since there's no actual serial input
    // In a real implementation, this would read from stdin until the terminator is found
    // For testing purposes, you could set an environment variable or use stdin redirection
    return fl::string();
}

void SerialEmulation::write(fl::u8) {}

void SerialEmulation::write(const char *s) {
    fl::printf("%s", s);
}

void SerialEmulation::write(const fl::u8 *s, size_t n) {
    fl::write_bytes(s, n);
}

void SerialEmulation::write(const char *s, size_t n) {
    fl::write_bytes(reinterpret_cast<const fl::u8 *>(s), n); // ok reinterpret cast
}

void SerialEmulation::flush() {}

void SerialEmulation::end() {}

fl::u8 SerialEmulation::peek() {
    return 0;
}

// Serial instances
SerialEmulation Serial;
SerialEmulation Serial1;
SerialEmulation Serial2;
SerialEmulation Serial3;

#endif
