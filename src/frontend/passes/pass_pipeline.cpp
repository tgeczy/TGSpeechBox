/*
TGSpeechBox — Pass pipeline registration and execution.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "pass_pipeline.h"

#include "allophones.h"
#include "syllable_marking.h"
#include "coarticulation.h"
#include "microprosody.h"
#include "nasalization.h"
#include "prosody.h"
#include "rate_compensation.h"
#include "liquid_dynamics.h"
#include "length_contrast.h"
#include "boundary_smoothing.h"
#include "trajectory_limit.h"
#include "cluster_timing.h"
#include "cluster_blend.h"
#include "special_coartic.h"
#include "prominence.h"

namespace nvsp_frontend {

namespace {

const PassDesc kPasses[] = {
    {"syllable_marking", PassStage::PreTiming, &passes::runSyllableMarking},
    {"nasalization", PassStage::PreTiming, &passes::runNasalization},
    {"allophones", PassStage::PreTiming, &passes::runAllophones},

    {"coarticulation", PassStage::PostTiming, &passes::runCoarticulation},
    {"special_coartic", PassStage::PostTiming, &passes::runSpecialCoarticulation},
    {"cluster_timing", PassStage::PostTiming, &passes::runClusterTiming},
    {"cluster_blend", PassStage::PostTiming, &passes::runClusterBlend},
    {"prominence", PassStage::PostTiming, &passes::runProminence},
    {"prosody", PassStage::PostTiming, &passes::runProsody},
    {"rate_compensation", PassStage::PostTiming, &passes::runRateCompensation},
    {"liquid_dynamics", PassStage::PostTiming, &passes::runLiquidDynamics},
    {"length_contrast", PassStage::PostTiming, &passes::runLengthContrast},

    {"boundary_smoothing", PassStage::PostTiming, &passes::runBoundarySmoothing},
    {"trajectory_limit", PassStage::PostTiming, &passes::runTrajectoryLimit},

    {"microprosody", PassStage::PostPitch, &passes::runMicroprosody},
};

}  // namespace

bool runPasses(
    PassContext& ctx,
    PassStage stage,
    std::vector<Token>& tokens,
    std::string& outError) {
  for (const auto& pass : kPasses) {
    if (pass.fn == nullptr) continue;
    if (pass.stage != stage) continue;

    std::string err;
    if (!pass.fn(ctx, tokens, err)) {
      outError = std::string("pass '") + pass.name + "' failed: " + err;
      return false;
    }
  }
  return true;
}

}  // namespace nvsp_frontend
