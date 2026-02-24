#pragma once

// RGB_Blobs4 visualizer class
// Extracted from animartrix2_detail.hpp

#include "fl/fx/2d/animartrix2_detail/fp_state.h"
#include "fl/fx/2d/animartrix2_detail/viz/viz_base.h"

namespace fl {

class RGB_Blobs4 : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of RGB_Blobs4.
class RGB_Blobs4_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
