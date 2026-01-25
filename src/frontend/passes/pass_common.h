#ifndef NVSP_FRONTEND_PASSES_PASS_COMMON_H
#define NVSP_FRONTEND_PASSES_PASS_COMMON_H

#include <string>
#include <unordered_map>
#include <vector>

#include "../pack.h"
#include "../ipa_engine.h"

namespace nvsp_frontend {

// When a pass runs in the frontend pipeline.
enum class PassStage {
  // After parse/transforms/default voice defaults, before calculateTimes.
  PreTiming = 0,

  // After calculateTimes, before calculatePitches.
  PostTiming = 1,

  // After calculatePitches (and tone contours if used).
  PostPitch = 2,
};

// Context passed through all passes.
struct PassContext {
  const PackSet& pack;
  double speed = 1.0;
  double basePitch = 100.0;
  double inflection = 0.6;
  char clauseType = '.';

  // Passes can stash intermediate values here for later passes.
  std::unordered_map<std::string, double> scratchpad;

  PassContext(const PackSet& p, double s, double bp, double inf, char ct)
      : pack(p), speed(s), basePitch(bp), inflection(inf), clauseType(ct) {}
};

// Each pass modifies tokens in place and returns success.
// On failure, outError is set.
using PassFn = bool (*)(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

struct PassDesc {
  const char* name = "";
  PassStage stage = PassStage::PreTiming;
  PassFn fn = nullptr;
};

}  // namespace nvsp_frontend

#endif  // NVSP_FRONTEND_PASSES_PASS_COMMON_H
