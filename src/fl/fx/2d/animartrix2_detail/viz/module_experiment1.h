#pragma once

// Module_Experiment1 visualizer class
// Extracted from animartrix2_detail.hpp

#include "fl/fx/2d/animartrix2_detail/fp_state.h"
#include "fl/fx/2d/animartrix2_detail/viz/viz_base.h"

namespace fl {

class Module_Experiment1 : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Module_Experiment1.
class Module_Experiment1_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
