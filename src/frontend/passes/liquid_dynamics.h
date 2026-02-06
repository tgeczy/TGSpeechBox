#ifndef TGSB_FRONTEND_PASSES_LIQUID_DYNAMICS_H
#define TGSB_FRONTEND_PASSES_LIQUID_DYNAMICS_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Adds internal movement to liquids/glides by splitting tokens and applying formant targets.
bool runLiquidDynamics(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
);

} // namespace nvsp_frontend::passes

#endif // TGSB_FRONTEND_PASSES_LIQUID_DYNAMICS_H
