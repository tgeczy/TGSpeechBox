#include "allophones.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

// ── Existing helpers (kept for compatibility with other code) ──────────────

static inline bool tokIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool tokIsConsonant(const Token& t) {
  return t.def && !t.silence && ((t.def->flags & kIsVowel) == 0);
}

static inline bool tokIsStopLike(const Token& t) {
  if (!t.def || t.silence) return false;
  const uint32_t f = t.def->flags;
  return ((f & kIsStop) != 0) || ((f & kIsAfricate) != 0);
}

static inline bool tokIsVoiced(const Token& t) {
  return t.def && ((t.def->flags & kIsVoiced) != 0);
}

static inline bool tokIsSilence(const Token& t) {
  return t.silence || !t.def;
}

static inline double clampDouble(double v, double lo, double hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline double clamp01(double v) {
  return clampDouble(v, 0.0, 1.0);
}

static inline double safeSpeed(double s) {
  return (s < 0.05) ? 0.05 : s;
}

static inline double getField(const Token& tok, FieldId fid) {
  const int idx = static_cast<int>(fid);
  const uint64_t bit = 1ULL << idx;
  if (tok.setMask & bit) return tok.field[idx];
  if (tok.def && (tok.def->setMask & bit)) return tok.def->field[idx];
  return 0.0;
}

static inline bool hasField(const Token& tok, FieldId fid) {
  const int idx = static_cast<int>(fid);
  const uint64_t bit = 1ULL << idx;
  return (tok.setMask & bit) || (tok.def && (tok.def->setMask & bit));
}

static inline void setField(Token& tok, FieldId fid, double v) {
  const int idx = static_cast<int>(fid);
  tok.field[idx] = v;
  tok.setMask |= (1ULL << idx);
}

static void clampFadeToDuration(Token& t) {
  if (t.durationMs < 0.0) t.durationMs = 0.0;
  if (t.fadeMs < 0.0) t.fadeMs = 0.0;
  if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
}

static const PhonemeDef* findPhoneme(const PackSet& pack, const std::u32string& key) {
  auto it = pack.phonemes.find(key);
  if (it == pack.phonemes.end()) return nullptr;
  return &it->second;
}

static bool containsString(const std::vector<std::string>& v, const char* s) {
  for (const auto& x : v) {
    if (x == s) return true;
  }
  return false;
}

static const Token* prevNonSilence(const std::vector<Token>& tokens, int i) {
  for (int j = i - 1; j >= 0; --j) {
    if (!tokIsSilence(tokens[j])) return &tokens[j];
  }
  return nullptr;
}

static const Token* nextNonSilence(const std::vector<Token>& tokens, int i) {
  const int n = static_cast<int>(tokens.size());
  for (int j = i + 1; j < n; ++j) {
    if (!tokIsSilence(tokens[j])) return &tokens[j];
  }
  return nullptr;
}

static bool isWordFinalIndex(const std::vector<Token>& tokens, int i) {
  const int n = static_cast<int>(tokens.size());
  for (int j = i + 1; j < n; ++j) {
    const Token& t = tokens[j];
    if (tokIsSilence(t)) continue;
    return t.wordStart;
  }
  return true; // end of stream
}

// ── New helpers for the rule engine ───────────────────────────────────────

// Skip silence, preStopGap, postStopAspiration, clusterGap, voicedClosure.
// Returns the nearest "real phoneme" neighbor.
static const Token* prevPhoneme(const std::vector<Token>& tokens, int i) {
  for (int j = i - 1; j >= 0; --j) {
    const Token& t = tokens[static_cast<size_t>(j)];
    if (tokIsSilence(t)) continue;
    if (t.preStopGap || t.postStopAspiration || t.clusterGap || t.voicedClosure) continue;
    return &t;
  }
  return nullptr;
}

static const Token* nextPhoneme(const std::vector<Token>& tokens, int i) {
  const int n = static_cast<int>(tokens.size());
  for (int j = i + 1; j < n; ++j) {
    const Token& t = tokens[static_cast<size_t>(j)];
    if (tokIsSilence(t)) continue;
    if (t.preStopGap || t.postStopAspiration || t.clusterGap || t.voicedClosure) continue;
    return &t;
  }
  return nullptr;
}

// Like prevPhoneme but returns the index instead of a pointer.
static int prevPhonemeIndex(const std::vector<Token>& tokens, int i) {
  for (int j = i - 1; j >= 0; --j) {
    const Token& t = tokens[static_cast<size_t>(j)];
    if (tokIsSilence(t)) continue;
    if (t.preStopGap || t.postStopAspiration || t.clusterGap || t.voicedClosure) continue;
    return j;
  }
  return -1;
}

// Like isWordFinalIndex but skips closure/aspiration tokens.
static bool isWordFinalPhoneme(const std::vector<Token>& tokens, int i) {
  const Token* next = nextPhoneme(tokens, i);
  if (!next) return true; // end of stream
  return next->wordStart;
}

static uint32_t flagFromString(const std::string& s) {
  if (s == "stop") return kIsStop;
  if (s == "vowel") return kIsVowel;
  if (s == "nasal") return kIsNasal;
  if (s == "liquid") return kIsLiquid;
  if (s == "semivowel") return kIsSemivowel;
  if (s == "affricate") return kIsAfricate;
  if (s == "tap") return kIsTap;
  if (s == "trill") return kIsTrill;
  if (s == "voiced") return kIsVoiced;
  return 0;
}

static int fieldIdFromString(const std::string& s) {
  if (s == "voicePitch") return static_cast<int>(FieldId::voicePitch);
  if (s == "aspirationAmplitude") return static_cast<int>(FieldId::aspirationAmplitude);
  if (s == "fricationAmplitude") return static_cast<int>(FieldId::fricationAmplitude);
  if (s == "voiceAmplitude") return static_cast<int>(FieldId::voiceAmplitude);
  if (s == "glottalOpenQuotient") return static_cast<int>(FieldId::glottalOpenQuotient);
  if (s == "cf1") return static_cast<int>(FieldId::cf1);
  if (s == "cf2") return static_cast<int>(FieldId::cf2);
  if (s == "cf3") return static_cast<int>(FieldId::cf3);
  if (s == "cf4") return static_cast<int>(FieldId::cf4);
  if (s == "pf1") return static_cast<int>(FieldId::pf1);
  if (s == "pf2") return static_cast<int>(FieldId::pf2);
  if (s == "pf3") return static_cast<int>(FieldId::pf3);
  if (s == "cb1") return static_cast<int>(FieldId::cb1);
  if (s == "cb2") return static_cast<int>(FieldId::cb2);
  if (s == "cb3") return static_cast<int>(FieldId::cb3);
  if (s == "cfN0") return static_cast<int>(FieldId::cfN0);
  if (s == "cfNP") return static_cast<int>(FieldId::cfNP);
  if (s == "caNP") return static_cast<int>(FieldId::caNP);
  if (s == "cbN0") return static_cast<int>(FieldId::cbN0);
  if (s == "cbNP") return static_cast<int>(FieldId::cbNP);
  if (s == "cf5") return static_cast<int>(FieldId::cf5);
  if (s == "cf6") return static_cast<int>(FieldId::cf6);
  if (s == "cb4") return static_cast<int>(FieldId::cb4);
  if (s == "cb5") return static_cast<int>(FieldId::cb5);
  if (s == "cb6") return static_cast<int>(FieldId::cb6);
  if (s == "pf4") return static_cast<int>(FieldId::pf4);
  if (s == "pf5") return static_cast<int>(FieldId::pf5);
  if (s == "pf6") return static_cast<int>(FieldId::pf6);
  if (s == "pb1") return static_cast<int>(FieldId::pb1);
  if (s == "pb2") return static_cast<int>(FieldId::pb2);
  if (s == "pb3") return static_cast<int>(FieldId::pb3);
  if (s == "pb4") return static_cast<int>(FieldId::pb4);
  if (s == "pb5") return static_cast<int>(FieldId::pb5);
  if (s == "pb6") return static_cast<int>(FieldId::pb6);
  if (s == "pa1") return static_cast<int>(FieldId::pa1);
  if (s == "pa2") return static_cast<int>(FieldId::pa2);
  if (s == "pa3") return static_cast<int>(FieldId::pa3);
  if (s == "pa4") return static_cast<int>(FieldId::pa4);
  if (s == "pa5") return static_cast<int>(FieldId::pa5);
  if (s == "pa6") return static_cast<int>(FieldId::pa6);
  if (s == "parallelBypass") return static_cast<int>(FieldId::parallelBypass);
  if (s == "preFormantGain") return static_cast<int>(FieldId::preFormantGain);
  if (s == "outputGain") return static_cast<int>(FieldId::outputGain);
  if (s == "endVoicePitch") return static_cast<int>(FieldId::endVoicePitch);
  if (s == "voiceTurbulenceAmplitude") return static_cast<int>(FieldId::voiceTurbulenceAmplitude);
  if (s == "vibratoSpeed") return static_cast<int>(FieldId::vibratoSpeed);
  if (s == "vibratoPitchOffset") return static_cast<int>(FieldId::vibratoPitchOffset);
  return -1; // not found
}

// ── Match conditions ──────────────────────────────────────────────────────

static bool ruleMatches(
    const AllophoneRule& rule,
    const std::vector<Token>& tokens,
    int i,
    const Token& t
) {
  // 1) Token type filter
  if (rule.tokenType == "aspiration") {
    if (!t.postStopAspiration) return false;
  } else if (rule.tokenType == "closure") {
    if (!t.preStopGap && !t.clusterGap) return false;
  } else {
    // "phoneme" — skip non-phoneme tokens
    if (t.silence || !t.def) return false;
    if (t.preStopGap || t.postStopAspiration || t.clusterGap) return false;
  }

  // 2) Phoneme key match (if list is non-empty, token must match one)
  if (!rule.phonemes.empty()) {
    if (!t.def) return false;
    bool found = false;
    for (const auto& ph : rule.phonemes) {
      if (t.def->key == ph) { found = true; break; }
    }
    if (!found) return false;
  }

  // 3) Flag match (all listed flags must be present)
  if (!rule.flags.empty()) {
    if (!t.def) return false;
    for (const auto& f : rule.flags) {
      uint32_t bit = flagFromString(f);
      if (bit == 0) continue;
      if (!(t.def->flags & bit)) return false;
    }
  }

  // 4) notFlags (exclude if ANY listed flag is present)
  if (!rule.notFlags.empty() && t.def) {
    for (const auto& f : rule.notFlags) {
      uint32_t bit = flagFromString(f);
      if (bit != 0 && (t.def->flags & bit)) return false;
    }
  }

  // 5) Position — use phoneme-aware neighbors (skip closure/aspiration)
  const Token* prev = prevPhoneme(tokens, i);
  const Token* next = nextPhoneme(tokens, i);
  const bool prevIsVowel = (prev && tokIsVowel(*prev));
  const bool nextIsVowel = (next && tokIsVowel(*next));

  if (rule.position == "word-initial") {
    // For aspiration tokens, check the parent stop's wordStart
    if (rule.tokenType == "aspiration") {
      const Token* parentStop = prevPhoneme(tokens, i);
      if (!parentStop || !parentStop->wordStart) return false;
    } else {
      if (!t.wordStart) return false;
    }
  } else if (rule.position == "word-final") {
    if (!isWordFinalPhoneme(tokens, i)) return false;
  } else if (rule.position == "intervocalic") {
    if (!prevIsVowel || !nextIsVowel) return false;
    if (t.wordStart) return false;  // word-initial is not intervocalic
  } else if (rule.position == "pre-vocalic") {
    if (!nextIsVowel) return false;
  } else if (rule.position == "post-vocalic") {
    if (!prevIsVowel) return false;
  } else if (rule.position == "syllabic") {
    if (prevIsVowel || nextIsVowel) return false;
  }
  // "any" = no position filter

  // 6) Stress
  if (rule.stress == "stressed") {
    int s = (rule.tokenType == "aspiration" && prev) ? prev->stress : t.stress;
    if (s <= 0) return false;
  } else if (rule.stress == "unstressed") {
    int s = (rule.tokenType == "aspiration" && prev) ? prev->stress : t.stress;
    if (s > 0) return false;
  } else if (rule.stress == "next-unstressed") {
    // A consonant carrying syllable stress should never match.
    // eSpeak puts stress marks on syllable-initial consonants,
    // so /d/ in "dˌiː" has stress>0 even though the vowel is the nucleus.
    if (t.stress > 0) return false;
    const Token* nv = nextPhoneme(tokens, i);
    if (!nv || !tokIsVowel(*nv)) return false;
    if (nv->stress > 0) return false;
  } else if (rule.stress == "prev-stressed") {
    if (!prev || prev->stress <= 0) return false;
  }
  // "any" = no stress filter

  // 7) Neighbor key filters
  if (!rule.after.empty()) {
    const Token* checkPrev = prev;
    // For aspiration tokens, "after" means the phoneme before the
    // PARENT STOP, not before the aspiration itself.  prevPhoneme()
    // returns the parent stop; we need one more hop backward.
    if (rule.tokenType == "aspiration" && prev) {
      int parentIdx = prevPhonemeIndex(tokens, i);
      if (parentIdx >= 0) {
        checkPrev = prevPhoneme(tokens, parentIdx);
      } else {
        checkPrev = nullptr;
      }
    }
    if (!checkPrev || !checkPrev->def) return false;
    bool found = false;
    for (const auto& ph : rule.after) {
      if (checkPrev->def->key == ph) { found = true; break; }
    }
    if (!found) return false;
  }
  if (!rule.before.empty()) {
    if (!next || !next->def) return false;
    bool found = false;
    for (const auto& ph : rule.before) {
      if (next->def->key == ph) { found = true; break; }
    }
    if (!found) return false;
  }

  // 8) Neighbor flag filters
  //    For aspiration tokens, use the same "look past parent stop" logic.
  const Token* afterFlagsPrev = prev;
  if (rule.tokenType == "aspiration" && prev) {
    int parentIdx = prevPhonemeIndex(tokens, i);
    if (parentIdx >= 0) {
      afterFlagsPrev = prevPhoneme(tokens, parentIdx);
    } else {
      afterFlagsPrev = nullptr;
    }
  }
  if (!rule.afterFlags.empty()) {
    if (!afterFlagsPrev || !afterFlagsPrev->def) return false;
    for (const auto& f : rule.afterFlags) {
      uint32_t bit = flagFromString(f);
      if (bit != 0 && !(afterFlagsPrev->def->flags & bit)) return false;
    }
  }
  if (!rule.notAfterFlags.empty()) {
    if (afterFlagsPrev && afterFlagsPrev->def) {
      for (const auto& f : rule.notAfterFlags) {
        uint32_t bit = flagFromString(f);
        if (bit != 0 && (afterFlagsPrev->def->flags & bit)) return false;
      }
    }
  }
  if (!rule.beforeFlags.empty()) {
    if (!next || !next->def) return false;
    for (const auto& f : rule.beforeFlags) {
      uint32_t bit = flagFromString(f);
      if (bit != 0 && !(next->def->flags & bit)) return false;
    }
  }
  if (!rule.notBeforeFlags.empty()) {
    if (next && next->def) {
      for (const auto& f : rule.notBeforeFlags) {
        uint32_t bit = flagFromString(f);
        if (bit != 0 && (next->def->flags & bit)) return false;
      }
    }
  }

  return true;
}

// ── Action: Replace ───────────────────────────────────────────────────────

static void applyReplace(
    Token& t,
    const AllophoneRule& rule,
    const PackSet& pack,
    double speed
) {
  const PhonemeDef* newDef = findPhoneme(pack, rule.replaceTo);
  if (!newDef) return;

  // Swap the phoneme definition
  t.def = newDef;

  // Copy fields from new def where the token doesn't already have overrides
  for (int k = 0; k < kFrameFieldCount; ++k) {
    const uint64_t bit = 1ULL << k;
    if (!(t.setMask & bit) && (newDef->setMask & bit)) {
      t.field[k] = newDef->field[k];
      t.setMask |= bit;
    }
  }

  // Set duration if specified
  if (rule.replaceDurationMs > 0.0) {
    double sp = (speed < 0.05) ? 0.05 : speed;
    t.durationMs = rule.replaceDurationMs / sp;
    t.fadeMs = std::min(t.durationMs, 3.0 / sp);
    clampFadeToDuration(t);
  }
}

// ── Action: Scale ─────────────────────────────────────────────────────────

static void applyScale(Token& t, const AllophoneRule& rule) {
  if (rule.durationScale != 1.0) {
    t.durationMs *= rule.durationScale;
  }
  if (rule.fadeScale != 1.0) {
    t.fadeMs *= rule.fadeScale;
  }
  clampFadeToDuration(t);

  for (const auto& [fieldName, scale] : rule.fieldScales) {
    int fid = fieldIdFromString(fieldName);
    if (fid < 0) continue;
    FieldId id = static_cast<FieldId>(fid);
    if (hasField(t, id)) {
      setField(t, id, getField(t, id) * scale);
    }
  }
}

// ── Action: Shift ─────────────────────────────────────────────────────────

static void applyShift(Token& t, const AllophoneRule& rule) {
  for (const auto& se : rule.fieldShifts) {
    int fid = fieldIdFromString(se.field);
    if (fid < 0) continue;
    FieldId id = static_cast<FieldId>(fid);
    if (!hasField(t, id)) continue;

    double cur = getField(t, id);
    if (se.targetHz > 0.0) {
      // Blend toward target
      double blend = std::max(0.0, std::min(1.0, se.blend));
      setField(t, id, cur + (se.targetHz - cur) * blend);
    } else {
      // Absolute delta
      setField(t, id, cur + se.deltaHz);
    }
  }
}

// ── Action: Insert token ──────────────────────────────────────────────────

static Token makeInsertToken(
    const AllophoneRule& rule,
    const PackSet& pack,
    double speed
) {
  Token g;
  const PhonemeDef* def = findPhoneme(pack, rule.insertPhoneme);
  if (!def) { g.silence = true; return g; }

  g.def = def;
  g.silence = false;
  g.wordStart = false;
  g.syllableStart = false;
  g.stress = 0;
  g.lengthened = 0;
  g.tiedTo = false;
  g.tiedFrom = false;

  g.setMask = def->setMask;
  for (int k = 0; k < kFrameFieldCount; ++k) {
    g.field[k] = def->field[k];
  }

  double sp = (speed < 0.05) ? 0.05 : speed;
  g.durationMs = std::max(2.0, rule.insertDurationMs) / sp;
  g.fadeMs = std::min(g.durationMs, std::max(1.0, rule.insertFadeMs) / sp);
  clampFadeToDuration(g);
  return g;
}

} // namespace

// ── Main rule engine ──────────────────────────────────────────────────────

bool runAllophones(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError
) {
  (void)outError;
  const auto& lp = ctx.pack.lang;
  if (!lp.allophoneRulesEnabled || lp.allophoneRules.empty()) return true;

  const double sp = safeSpeed(ctx.speed);

  // We need the output-vector pattern because replace can remove
  // neighboring closure/aspiration tokens, and insert adds tokens.
  std::vector<Token> out;
  out.reserve(tokens.size() + tokens.size() / 8);

  // Track which tokens to skip (removed by replace rules).
  std::vector<bool> skip(tokens.size(), false);

  // First pass: mark tokens for removal by replace rules.
  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    Token& t = tokens[static_cast<size_t>(i)];
    for (const auto& rule : lp.allophoneRules) {
      if (rule.action != "replace") continue;
      if (!ruleMatches(rule, tokens, i, t)) continue;

      // Mark preceding closure for removal or scaling
      if (rule.replaceRemovesClosure) {
        for (int j = i - 1; j >= 0; --j) {
          Token& cj = tokens[static_cast<size_t>(j)];
          if (cj.preStopGap || cj.clusterGap) {
            if (rule.replaceClosureScale > 0.0) {
              // Shorten closure instead of removing — allows resonator drain
              cj.durationMs *= rule.replaceClosureScale;
              cj.fadeMs *= rule.replaceClosureScale;
              clampFadeToDuration(cj);
            } else {
              skip[static_cast<size_t>(j)] = true;
            }
            break;
          }
          if (!cj.silence) break;
        }
      }
      // Mark following aspiration for removal or scaling
      if (rule.replaceRemovesAspiration) {
        // Always inject breathiness on the main phoneme when scale > 0,
        // regardless of whether an aspiration token exists after it.
        // Word-final stops often have no aspiration token (end of utterance).
        if (rule.replaceAspirationScale > 0.0) {
          Token& mainTok = tokens[static_cast<size_t>(i)];
          mainTok.hasTokenBreathiness = true;
          mainTok.tokenBreathiness = clamp01(rule.replaceAspirationScale);
        }
        // Now search for and handle the aspiration token if present
        for (int j = i + 1; j < static_cast<int>(tokens.size()); ++j) {
          Token& aj = tokens[static_cast<size_t>(j)];
          if (aj.postStopAspiration) {
            if (rule.replaceAspirationScale > 0.0) {
              // Scale aspiration duration instead of removing
              aj.durationMs *= rule.replaceAspirationScale;
              aj.fadeMs *= rule.replaceAspirationScale;
              clampFadeToDuration(aj);
            } else {
              skip[static_cast<size_t>(j)] = true;
            }
            break;
          }
          if (!aj.silence && !aj.postStopAspiration) break;
        }
      }
      break; // first replace match wins
    }
  }

  // Main output loop
  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    if (skip[static_cast<size_t>(i)]) continue;

    Token t = tokens[static_cast<size_t>(i)]; // copy — we'll modify it
    bool replaced = false;
    bool needInsertAfter = false;
    const AllophoneRule* insertAfterRule = nullptr;

    for (const auto& rule : lp.allophoneRules) {
      // Use original tokens for matching context (not modified 't'),
      // since neighbor lookups need original positions.
      if (!ruleMatches(rule, tokens, i, tokens[static_cast<size_t>(i)])) continue;

      if (rule.action == "replace" && !replaced) {
        applyReplace(t, rule, ctx.pack, sp);
        replaced = true;
        // Don't break — allow scale/shift rules to stack on top
      } else if (rule.action == "scale") {
        applyScale(t, rule);
      } else if (rule.action == "shift") {
        applyShift(t, rule);
      } else if (rule.action == "insert-before" && !replaced) {
        // Check insert contexts if specified
        bool contextOk = rule.insertContexts.empty();
        if (!contextOk) {
          const Token* prevP = prevPhoneme(tokens, i);
          bool prevPIsVowel = (prevP && tokIsVowel(*prevP));
          bool wf = isWordFinalPhoneme(tokens, i);
          for (const auto& ctx_str : rule.insertContexts) {
            if (ctx_str == "V_#" && prevPIsVowel && wf) contextOk = true;
            if (ctx_str == "#_#" && wf) contextOk = true;
          }
        }
        if (contextOk) {
          // Avoid double-insert
          if (!out.empty() && out.back().def &&
              out.back().def->key == rule.insertPhoneme) {
            // already inserted
          } else {
            Token ins = makeInsertToken(rule, ctx.pack, sp);
            if (!ins.silence) out.push_back(ins);
          }
        }
        replaced = true; // prevent further insert/replace
      } else if (rule.action == "insert-after" && !replaced) {
        needInsertAfter = true;
        insertAfterRule = &rule;
        replaced = true; // prevent further insert/replace
      }
    }

    out.push_back(t);

    // Handle insert-after
    if (needInsertAfter && insertAfterRule) {
      Token ins = makeInsertToken(*insertAfterRule, ctx.pack, sp);
      if (!ins.silence) out.push_back(ins);
    }
  }

  tokens.swap(out);
  return true;
}

} // namespace nvsp_frontend::passes
