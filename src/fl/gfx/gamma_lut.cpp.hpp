// ok no header
/// @file gamma_lut.cpp.hpp
/// @brief Explicit instantiation of gamma LUT templates.
/// Compiled once in fl.gfx+ to avoid per-TU _GLOBAL__sub_I_ blocks (~6KB each).

#include "fl/gfx/gamma_lut.h"

namespace fl {

// Explicit instantiation definitions — these are the ONLY copies of the init code.
template struct ProgmemLUT<GammaEval<gamma<u8x24>(2.2f)>, 256>;
template struct ProgmemLUT<GammaEval<gamma<u8x24>(2.8f)>, 256>;
template struct ProgmemLUT16<GammaEval16<gamma<u8x24>(2.2f)>, 256>;
template struct ProgmemLUT16<GammaEval16<gamma<u8x24>(2.8f)>, 256>;

} // namespace fl
