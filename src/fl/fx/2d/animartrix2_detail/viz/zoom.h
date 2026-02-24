#pragma once

// Zoom visualizer class
// Extracted from animartrix2_detail.hpp

#include "fl/fx/2d/animartrix2_detail/fp_state.h"
#include "fl/fx/2d/animartrix2_detail/viz/viz_base.h"

namespace fl {

class Zoom : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Zoom.
class Zoom_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
