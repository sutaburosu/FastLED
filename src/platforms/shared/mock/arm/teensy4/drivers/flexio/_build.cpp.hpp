// IWYU pragma: private

/// @file _build.hpp
/// @brief Unity build header for mock/arm/teensy4/drivers/flexio/ directory
///
/// On non-Teensy platforms, we compile:
/// 1. The channel engine (platform-independent business logic)
/// 2. The mock peripheral factory (provides IFlexIOPeripheral::create())

#include "platforms/arm/teensy/is_teensy.h"

#if !defined(FL_IS_TEENSY_4X)

#include "platforms/arm/teensy/teensy4_common/drivers/flexio/channel_engine_flexio.cpp.hpp"
#include "platforms/shared/mock/arm/teensy4/drivers/flexio/flexio_peripheral_mock.cpp.hpp"

#endif // !FL_IS_TEENSY_4X
