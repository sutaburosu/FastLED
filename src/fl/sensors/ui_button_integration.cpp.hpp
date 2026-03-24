// ok no header
/// @file ui_button_integration.cpp.hpp
/// @brief UIButton/UIDropdown <-> Button integration via IButtonInput interface.
/// Compiled in fl.sensors+ to keep concrete Button out of fl.cpp link chain.

#include "fl/ui.h"
#include "fl/sensors/button.h"

namespace fl {

void UIButton::addRealButton(fl::shared_ptr<Button> button) {
    // Button inherits IButtonInput; shared_ptr implicit upcast captures the
    // concrete deleter so fl.cpp never needs Button's destructor.
    mButtonInput = button;
}

void UIDropdown::addNextButton(int pin) {
    mNextButton = fl::make_shared<Button>(pin);
}

} // namespace fl
