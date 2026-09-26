/*
TGSpeechBox — Arató (BraiLab) intonation pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/
// =============================================================================
// Arató-style intonation — the BraiLab sentence melodies, as the program did it
// =============================================================================
//
// Reference: Arató András, "A BraiLab beszélő számítógépcsalád" (kandidátusi
// értekezés, műszaki leírás, Budapest, 1992), §5.4 "Mikrointonáció,
// intonáció, ének".  The BraiLab PC talker is the work of Vaspöri Teréz and
// Arató András ("BraiLab PC (c) Vaspöri Teréz és Arató András munkája" is the
// program's own banner).  This pass exists to carry that work forward.
//
// What it does (Arató §5.4): the melody exists so a blind reader hears, from
// the intonation alone, which punctuation closes the unit, even when the unit
// is cut off early because the next line was already requested.  Wh-questions
// are recognised from the sentence's first letters (HO HÁ MI ME KI), the
// imperative takes the wh melody, yes/no questions are split by length, and
// the definite article gets its own treatment.
//
// Where the algorithm comes from: the 1991 TALKHUN program itself
// (TALKHUN0.COM), disassembled and traced in an emulator on 2026-09-14; a
// re-implementation of the reading reproduces the program's output byte for
// byte on 32 intonation units covering every branch.  Its pitch codes are
// not stored in the speech database; one routine writes them per unit,
// after the diads are concatenated, and the 2000 build (TALKHUN.COM) still
// carries the same routine with the same constants.  What is ported here:
//
//   - the unit's start pitch: the user's pitch P plus a per-type delta in
//     the chip's pitch-byte units (-7, +4, 0, +18 = 85/112/103/146 Hz at
//     the default P);
//   - the type table: "." is type 1 when the unit starts with the article
//     "a"/"az", else type 2; "?" is the wh-melody (type 5) when the first
//     two phoneme keys match Arató's pairs, else yes/no (type 6, or 7 for
//     exactly two syllables); "!" is type 5; "," and ";" are type 8; ":" is
//     flat;
//   - the two anchor vectors: vowel starts (the syllable count) and word
//     starts;
//   - the ramp primitive: a signed total delta in pitch-code units spread
//     backwards over a span of frames, Bresenham style (dense spans get
//     floor(rem/N) per frame, sparse spans +/-1 every N/|D| frames), never
//     overwriting a frame that already carries a code;
//   - the per-type contours with their 18 constants, the +12 (+19.5 Hz)
//     hump on the first frame of the second word after an article, and the
//     fixed-frame anchors (frame 11 for the wh fall, +4/+6/+7 frames);
//   - the chip's geometric pitch-increment table (Philips PCF8200), applied
//     cumulatively, one code per 12.8 ms frame.
//
// The program never reads its tempo setting: anchors are in frames, so at a
// faster rate the same codes land on shorter frames and the contour
// compresses in time with the same pitch deltas.  This port keeps that by
// sizing its virtual frame from the unit itself (duration over syllables
// times the program's frames per syllable), so the frame count, and with it
// every code, is the same at any rate and for our faster segments.  Where a
// peak or knee falls inside one long vowel, the vowel is split in two so the
// engine's per-token pitch line can carry it.
//
// One call is one intonation unit (the platforms split at punctuation and
// pass the closing mark as clauseType).  Every constant is an arato* pack
// setting; the defaults are the program's.

#include "pitch_arato.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace nvsp_frontend {

namespace {

// Philips PCF8200 pitch increment per standard frame, Hz, by 5-bit code
// 0..15; codes 17..31 are the negatives of 15..1, code 16 marks a noise
// frame.  (The chip's own table; see the datasheet ladder.)
const double kPiHz[16] = {0.0, 1.2, 2.4, 3.7, 4.9, 6.1, 7.3, 8.5,
                          9.8, 11.0, 13.4, 15.9, 19.5, 25.6, 34.2, 45.2};
const int kNoise = 16;

struct Unit {
  std::vector<int> tokOfFrame;   // token index owning each virtual frame
  std::vector<int> code;         // 0 = free, 16 = noise, else the placed code (1..15 or 17..31)
  std::vector<int> V;            // frame index of each vowel start (syllables)
  std::vector<int> W;            // frame index of each word start; W[0] = 0
  int n = 0;                     // frame count (buffer end)
};

int clampIdx(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// TALKHUN's ramp: spread a signed delta D (pitch-code units) over frames
// [start, end) from the end backwards.  All virtual frames are standard
// length, so the FD weighting collapses to 1.
void ramp(Unit& u, int start, int end, int D) {
  start = clampIdx(start, 0, u.n);
  end = clampIdx(end, 0, u.n);
  if (D == 0) return;
  int rem = std::abs(D);
  int cur = end;
  auto place = [&](int f, int q) {
    if (f < 0 || f >= u.n) return;
    if (u.code[static_cast<size_t>(f)] != 0) return;   // noise frames and the hump are skipped
    q = std::min(q, 15);
    rem -= std::min(q, 15);
    if (rem < 0) rem = 0;
    u.code[static_cast<size_t>(f)] = (D > 0) ? q : (32 - q);
  };
  while (cur > start && rem > 0) {
    const int N = cur - start;
    if (rem > N) {
      const int q = rem / N;
      place(cur - 1, q);
      cur -= 1;
    } else {
      const int s = std::max(1, N / rem);
      place(cur - 1, 1);
      cur -= s;
    }
  }
}

}  // namespace

void applyPitchArato(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {
  if (tokens.empty()) return;
  const auto& lang = pack.lang;
  const size_t nTok = tokens.size();

  // ---------------------------------------------------------------------------
  // Virtual frames.  The program's anchors are in its own 12.8 ms frames, and
  // its speech ran at roughly 26 of them per syllable; it never reads its
  // tempo, so the frame count per unit is what its constants were tuned for.
  // The frame here is therefore the unit's duration divided by (syllables x
  // aratoFramesPerSyllable): the count holds at every rate and for our faster
  // segments, and the offsets (+4, +6, frame 11) keep their meaning.
  // ---------------------------------------------------------------------------
  (void)speed;
  std::vector<double> tokStartMs(nTok, 0.0), tokEndMs(nTok, 0.0);
  double elapsed = 0.0;
  int vowelCount = 0;
  for (size_t i = 0; i < nTok; ++i) {
    tokStartMs[i] = elapsed;
    elapsed += tokens[i].durationMs;
    tokEndMs[i] = elapsed;
    if (!tokens[i].silence && tokens[i].def && tokenIsVowel(tokens[i])) ++vowelCount;
  }
  const double T = elapsed;
  if (T <= 0.0 || vowelCount == 0) {
    for (size_t i = 0; i < nTok; ++i) setPitchFields(tokens[i], basePitch, basePitch);
    return;
  }
  const double fps = (lang.aratoFramesPerSyllable > 1.0) ? lang.aratoFramesPerSyllable : 26.0;
  const double frameMs = std::max(0.25, T / (vowelCount * fps));

  Unit u;
  u.n = std::max(1, static_cast<int>(std::ceil(T / frameMs)));
  u.tokOfFrame.assign(static_cast<size_t>(u.n), -1);
  u.code.assign(static_cast<size_t>(u.n), 0);
  {
    size_t ti = 0;
    for (int f = 0; f < u.n; ++f) {
      const double mid = (f + 0.5) * frameMs;
      while (ti + 1 < nTok && tokEndMs[ti] <= mid) ++ti;
      u.tokOfFrame[static_cast<size_t>(f)] = static_cast<int>(ti);
      const Token& t = tokens[ti];
      // Noise frames (voiceless consonants) carry no pitch code, like the
      // program's PI=16 frames; voiced tokens and closure gaps are free.
      const bool noise = (!t.silence && t.def && !pitchTokenIsVoiced(t) && !tokenIsVowel(t));
      if (noise) u.code[static_cast<size_t>(f)] = kNoise;
    }
  }
  // Vowel vector and word vector, in frames.
  int firstSpoken = -1;
  for (size_t i = 0; i < nTok; ++i) {
    const Token& t = tokens[i];
    if (t.silence || !t.def) continue;
    const int f = clampIdx(static_cast<int>(tokStartMs[i] / frameMs), 0, u.n - 1);
    if (firstSpoken < 0) { firstSpoken = static_cast<int>(i); u.W.push_back(0); }
    else if (t.wordStart) u.W.push_back(f);
    if (tokenIsVowel(t)) u.V.push_back(f);
  }
  if (firstSpoken < 0 || u.V.empty()) {
    for (size_t i = 0; i < nTok; ++i) setPitchFields(tokens[i], basePitch, basePitch);
    return;
  }

  // ---------------------------------------------------------------------------
  // First word's keys (for the article and the wh-word tests).
  // ---------------------------------------------------------------------------
  std::u32string firstWord;
  std::u32string k0, k1;
  {
    int seen = 0;
    for (size_t i = static_cast<size_t>(firstSpoken); i < nTok; ++i) {
      const Token& t = tokens[i];
      if (t.silence || !t.def) continue;
      if (seen > 0 && t.wordStart) break;
      if (seen == 0) k0 = t.def->key; else if (seen == 1) k1 = t.def->key;
      if (seen > 0) firstWord += U' ';
      firstWord += t.def->key;
      ++seen;
    }
  }
  bool articleInitial = false;
  for (const auto& a : lang.aratoArticles) if (a == firstWord) { articleInitial = true; break; }
  bool whInitial = false;
  for (const auto& pr : lang.aratoWhPairs) if (pr.first == k0 && pr.second == k1) { whInitial = true; break; }

  // ---------------------------------------------------------------------------
  // Type (the program's table) and start pitch.
  // ---------------------------------------------------------------------------
  const int syll = static_cast<int>(u.V.size());
  const int words = static_cast<int>(u.W.size());
  int type = 2;
  if (clauseType == '?') type = whInitial ? 5 : (syll == 2 ? 7 : 6);
  else if (clauseType == '!') type = 5;
  else if (clauseType == ',' || clauseType == ';') type = 8;
  // The program spoke a colon (and an unpunctuated line end) flat at P, its
  // "type 3".  A screen reader ends half its labels with a colon, so the
  // default here is the comma melody; aratoColonFlat restores the original.
  else if (clauseType == ':') type = lang.aratoColonFlat ? 3 : 8;
  else type = articleInitial ? 1 : 2;

  double startDelta = 0.0;
  switch (type) {
    case 1: startDelta = lang.aratoStartArticle; break;
    case 2: startDelta = lang.aratoStartDecl; break;
    case 5: startDelta = lang.aratoStartWh; break;
    case 8: startDelta = (words >= 2) ? (articleInitial ? lang.aratoStartArticle : lang.aratoStartComma) : 0.0; break;
    default: startDelta = 0.0; break;
  }

  // ---------------------------------------------------------------------------
  // Contours (frames; V = vowel vector, W = word vector, n = buffer end).
  // ---------------------------------------------------------------------------
  const int n = u.n;
  const int hump = static_cast<int>(lang.aratoHumpCode);
  auto V = [&](int i) { return u.V[static_cast<size_t>(i < 0 ? syll + i : i)]; };
  auto W = [&](int i) { return u.W[static_cast<size_t>(i < 0 ? words + i : i)]; };
  auto setHump = [&]() {
    if (words >= 2 && hump > 0) {
      const int f = clampIdx(W(1), 0, n - 1);
      if (u.code[static_cast<size_t>(f)] == 0) u.code[static_cast<size_t>(f)] = std::min(hump, 15);
    }
  };
  auto declBody = [&](int from, bool longUnit) {
    // Fall to the last syllable (2-5 syll) or the last word (>=6 syll), then to the end.
    int mid = longUnit ? W(-1) : V(-1);
    if (mid <= from) mid = longUnit ? (syll >= 3 ? V(-3) : from + 3) : from + 3;
    ramp(u, from, mid, longUnit ? static_cast<int>(lang.aratoDeclLongFallToLastWord) : static_cast<int>(lang.aratoDeclFallToLastSyll));
    ramp(u, mid, n, longUnit ? static_cast<int>(lang.aratoDeclLongFallEnd) : static_cast<int>(lang.aratoDeclFallEnd));
  };

  switch (type) {
    case 1: {  // article-initial declarative
      if (syll >= 2) { setHump(); declBody(V(1) + 4, syll >= 6); }
      break;
    }
    case 2: {  // plain declarative
      if (syll == 1) ramp(u, V(0) + 4, n, static_cast<int>(lang.aratoDeclOneSyllFall));
      else declBody(V(0) + 4, syll >= 6);
      break;
    }
    case 5: {  // wh-question and exclamation: fall from a fixed frame
      const int f0 = static_cast<int>(lang.aratoWhStartFrame);
      if (syll == 1) ramp(u, f0, n, static_cast<int>(lang.aratoWhOneSyllFall));
      else if (syll == 2) ramp(u, f0, n, static_cast<int>(lang.aratoWhTwoSyllFall));
      else {
        const int mid = V(1) + 7;
        ramp(u, f0, mid, static_cast<int>(lang.aratoWhFall));
        ramp(u, mid, n, static_cast<int>(lang.aratoWhTail));
      }
      break;
    }
    case 6: {  // yes/no, one or three-plus syllables
      if (syll == 1) {
        const int a = V(0) + 6;
        ramp(u, a, a + 3, static_cast<int>(lang.aratoYnOneSyllRise1));
        ramp(u, a + 3, a + 6, static_cast<int>(lang.aratoYnOneSyllRise2));
        // no tail: the program leaves a one-syllable question at its peak
      } else {
        const int pen = V(-2) + 4;
        ramp(u, 0, pen, static_cast<int>(lang.aratoYnCreep));
        ramp(u, pen, pen + 4, static_cast<int>(lang.aratoYnRise));
        ramp(u, pen + 4, n, static_cast<int>(lang.aratoYnFall));
      }
      break;
    }
    case 7: {  // yes/no, exactly two syllables
      const int a = V(1) + 6;
      ramp(u, a, a + 2, static_cast<int>(lang.aratoYnTwoSyllRise));
      ramp(u, a + 2, n, static_cast<int>(lang.aratoYnTwoSyllFall));
      break;
    }
    case 8: {  // comma / semicolon: the unit always ends with a rise over the last word
      if (words == 1) {
        ramp(u, 0, n, static_cast<int>(lang.aratoCommaRise));
      } else {
        const int lastWord = W(-1);
        // Syllables before the last word (+1 if the last word starts with a vowel).
        int before = 0;
        for (int v : u.V) if (v < lastWord) ++before;
        {
          const int lastTok = u.tokOfFrame[static_cast<size_t>(clampIdx(lastWord, 0, n - 1))];
          if (lastTok >= 0 && tokenIsVowel(tokens[static_cast<size_t>(lastTok)])) ++before;
        }
        const int fall = static_cast<int>(lang.aratoCommaFall);
        if (articleInitial) {
          if (before <= 1) ramp(u, 6, n, static_cast<int>(lang.aratoCommaRise));
          else {
            setHump();
            int from = V(1) + 4; if (from >= lastWord) from = lastWord - 2;
            ramp(u, from, lastWord, fall);
          }
        } else {
          if (before == 0) ramp(u, 0, n, static_cast<int>(lang.aratoCommaRise));
          else {
            int from = V(0) + 4; if (from >= lastWord) from = lastWord - 2;
            ramp(u, from, lastWord, fall);
          }
        }
        ramp(u, lastWord, n, static_cast<int>(lang.aratoCommaRise));
      }
      break;
    }
    default: break;  // type 3 (colon / line end): flat at P
  }

  // ---------------------------------------------------------------------------
  // Integrate the codes cumulatively, as the chip does, and hand each token its
  // start and end pitch.  The inflection slider scales every increment and the
  // start delta from the program's own values (reached at aratoInflectionRef).
  // ---------------------------------------------------------------------------
  // The chip's steps are hertz, and TALKHUN's melody is what they make at its
  // own pitch (aratoReferencePitchHz, 103 Hz).  Added unscaled to another base
  // pitch they are a smaller interval above it and a larger one below, and a
  // low voice's falls hit the floor (#136); scaled by the ratio they keep the
  // program's intervals, and at its own pitch they are the program's values.
  const double ref = (lang.aratoInflectionRef > 0.0) ? lang.aratoInflectionRef : 0.5;
  const double pitchRatio = (lang.aratoReferencePitchHz > 0.0 && basePitch > 0.0)
                                ? basePitch / lang.aratoReferencePitchHz : 1.0;
  const double scale = inflection / ref * pitchRatio;
  const double byteHz = lang.aratoPitchByteHz;
  double pitch = basePitch + startDelta * byteHz * scale;
  const double lo = 40.0 * pitchRatio, hi = std::max(400.0, basePitch * 2.0);
  pitch = std::min(std::max(pitch, lo), hi);
  std::vector<double> atStart(static_cast<size_t>(u.n) + 1, pitch);
  for (int f = 0; f < u.n; ++f) {
    const int c = u.code[static_cast<size_t>(f)];
    double inc = 0.0;
    if (c >= 1 && c <= 15) inc = kPiHz[c];
    else if (c >= 17 && c <= 31) inc = -kPiHz[32 - c];
    pitch = std::min(std::max(pitch + inc * scale, lo), hi);
    atStart[static_cast<size_t>(f) + 1] = pitch;
  }
  // A token carries one start and one end pitch, and the emitter draws a
  // straight line between them.  The chip drew every frame, so a peak or a
  // knee inside a long vowel (the two-syllable question's rise-fall, the wh
  // hold-then-fall) would be flattened.  Where the integrated contour leaves
  // that straight line by more than a few hertz inside a vowel, split the
  // vowel at that frame: two halves, same sound, each with its own pitch.
  struct Split { size_t tok; double atMs; };
  std::vector<Split> splits;
  const double splitTolHz = 4.0;
  for (size_t i = 0; i < nTok; ++i) {
    const Token& t = tokens[i];
    if (t.silence || !t.def || !tokenIsVowel(t) || t.durationMs < 30.0) continue;
    // Never split a token that glides: the emitter renders a diphthong's whole
    // formant sweep from each token's own start to its end target, so two
    // halves would each play the full glide ("why" -> "why-i", #125).
    const bool glides = t.isDiphthongGlide ||
        t.hasEndCf1 || t.hasEndCf2 || t.hasEndCf3 ||
        t.hasEndPf1 || t.hasEndPf2 || t.hasEndPf3 ||
        (t.def->hasEndCf1 || t.def->hasEndCf2 || t.def->hasEndCf3);
    if (glides) continue;
    const int fa = clampIdx(static_cast<int>(tokStartMs[i] / frameMs), 0, u.n);
    const int fb = clampIdx(static_cast<int>(std::ceil(tokEndMs[i] / frameMs)), 0, u.n);
    if (fb - fa < 3) continue;
    const double p0 = atStart[static_cast<size_t>(fa)], p1 = atStart[static_cast<size_t>(fb)];
    double bestDev = 0.0; int bestF = -1;
    for (int f = fa + 1; f < fb; ++f) {
      const double lin = p0 + (p1 - p0) * (double)(f - fa) / (double)(fb - fa);
      const double dev = std::fabs(atStart[static_cast<size_t>(f)] - lin);
      if (dev > bestDev) { bestDev = dev; bestF = f; }
    }
    if (bestF < 0 || bestDev < splitTolHz) continue;
    const double atMs = bestF * frameMs;
    if (atMs - tokStartMs[i] < 12.0 || tokEndMs[i] - atMs < 12.0) continue;
    splits.push_back({i, atMs});
  }
  auto pitchAt = [&](double ms) {
    const int f = clampIdx(static_cast<int>(ms / frameMs), 0, u.n);
    return atStart[static_cast<size_t>(f)];
  };
  // Apply from the back so earlier indices stay valid.
  for (size_t k = splits.size(); k-- > 0;) {
    const Split& s = splits[k];
    Token& a = tokens[s.tok];
    Token b = a;                                  // second half: same sound, keeps the token's fade
    const double firstDur = s.atMs - tokStartMs[s.tok];
    b.durationMs = a.durationMs - firstDur;
    a.durationMs = firstDur;
    // Amplitude contour (DSP v9): the twins share ONE line.  The first
    // half ends where the line is at the cut, the second starts there and
    // finishes the fall — otherwise the whole fall would play twice.
    if (a.endVoiceAmplitudeScale >= 0.0 && a.durationMs + b.durationMs > 0.0) {
      const double r = a.endVoiceAmplitudeScale;
      const double frac = a.durationMs / (a.durationMs + b.durationMs);
      const double atCut = 1.0 + (r - 1.0) * frac;
      const int vaIdx = static_cast<int>(FieldId::voiceAmplitude);
      const std::uint64_t vaBit = 1ULL << vaIdx;
      const double va = (b.setMask & vaBit) ? b.field[vaIdx]
                        : (b.def && (b.def->setMask & vaBit)) ? b.def->field[vaIdx] : 0.0;
      if (atCut > 0.0 && va > 0.0) {
        a.endVoiceAmplitudeScale = atCut;
        b.field[vaIdx] = va * atCut;
        b.setMask |= vaBit;
        b.endVoiceAmplitudeScale = r / atCut;
      }
    }
    a.fadeMs = std::min(a.fadeMs, 3.0);           // a short crossfade into the twin
    b.wordStart = false;
    b.syllableStart = false;
    b.stress = 0;
    b.amplitudeOnsetMs = 0.0;                     // one onset per vowel, on the first half
    setPitchFields(a, pitchAt(tokStartMs[s.tok]), pitchAt(s.atMs));
    setPitchFields(b, pitchAt(s.atMs), pitchAt(tokEndMs[s.tok]));
    tokens.insert(tokens.begin() + static_cast<long>(s.tok) + 1, b);
    tokStartMs.insert(tokStartMs.begin() + static_cast<long>(s.tok) + 1, s.atMs);
    tokEndMs.insert(tokEndMs.begin() + static_cast<long>(s.tok), s.atMs);
  }
  for (size_t i = 0; i < tokens.size(); ++i) {
    setPitchFields(tokens[i], pitchAt(tokStartMs[i]), pitchAt(tokEndMs[i]));
  }
}

} // namespace nvsp_frontend
