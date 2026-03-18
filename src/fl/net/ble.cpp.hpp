// IWYU pragma: private
// src/fl/net/ble.cpp.hpp
//
// Stub implementations for platforms without BLE support.
// On ESP32 with NimBLE, the real implementation is compiled via the
// platform build chain (platforms/esp/32/drivers/ble/_build.cpp.hpp).
// Both paths are guarded by #pragma once in ble_esp32.cpp.hpp.

#pragma once

#include "fl/net/ble.h"

#if !FL_BLE_AVAILABLE

namespace fl {
namespace net {
namespace ble {

TransportState* createTransport(const char*) {
    return nullptr;
}

void destroyTransport(TransportState*) {
}

StatusInfo queryStatus(const TransportState*) {
    return StatusInfo{};
}

fl::pair<fl::function<fl::optional<fl::json>()>, fl::function<void(const fl::json&)>>
getTransportCallbacks(TransportState*) {
    return {
        fl::function<fl::optional<fl::json>()>([]() -> fl::optional<fl::json> { return fl::optional<fl::json>(); }),
        fl::function<void(const fl::json&)>([](const fl::json&) {})
    };
}

} // namespace ble
} // namespace net
} // namespace fl

#endif // !FL_BLE_AVAILABLE
