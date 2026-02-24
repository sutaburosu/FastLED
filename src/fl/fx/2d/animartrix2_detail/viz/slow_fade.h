#pragma once

// Slow_Fade visualizer class
// Extracted from animartrix2_detail.hpp

#include "fl/fx/2d/animartrix2_detail/fp_state.h"
#include "fl/fx/2d/animartrix2_detail/viz/viz_base.h"

namespace fl {

class Slow_Fade : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Slow_Fade.
class Slow_Fade_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
