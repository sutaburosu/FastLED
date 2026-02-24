#pragma once

// IWYU pragma: private

/// @file init_channel_driver.h
/// @brief Stub platform channel driver initialization
///
/// This registers the stub channel driver so the legacy FastLED.addLeds<>() API
/// can route through the channel driver infrastructure for testing.

#include "fl/channels/manager.h"
#include "fl/channels/channel.h"
#include "fl/stl/shared_ptr.h"
#include "platforms/stub/clockless_channel_engine_stub.h"

namespace fl {
namespace platforms {

/// @brief Initialize channel drivers for stub platform
///
/// Registers ClocklessChannelEngineStub with the ChannelManager so that
/// the FastLED.add() API (Channel objects) drives stub GPIO simulation and
/// fires SimEdgeObserver callbacks that NativeRxDevice uses for validation.
inline void initChannelDrivers() {
    fl::ChannelManager& manager = fl::ChannelManager::instance();

    // Register the stub clockless driver with priority 1.
    // Priority 1 > 0 (the no-op StubChannelDriver registered elsewhere),
    // so this driver is preferred for clockless channels on the stub platform.
    static fl::stub::ClocklessChannelEngineStub stubEngine;  // ok static in header
    // Cast to base so make_shared_no_tracking creates shared_ptr<IChannelDriver>
    fl::IChannelDriver& driverRef = stubEngine;
    auto sharedDriver = fl::make_shared_no_tracking(driverRef);
    manager.addDriver(1, sharedDriver);
}

}  // namespace platforms
}  // namespace fl
