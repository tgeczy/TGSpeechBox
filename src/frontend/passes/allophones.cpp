#include "allophones.h"

namespace nvsp_frontend::passes {

bool runAllophones(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError) {
  (void)tokens;
  (void)outError;

  // Placeholder.
  // The main reason this file exists right now is to give us a clean
  // spot to add systematic positional allophones later (aspiration,
  // /l/ darkness, glottal reinforcement, assimilation, etc.) without
  // growing ipa_engine.cpp into a monster.
  if (!ctx.pack.lang.positionalAllophonesEnabled) {
    return true;
  }

  // Nothing implemented yet.
  return true;
}

}  // namespace nvsp_frontend::passes
