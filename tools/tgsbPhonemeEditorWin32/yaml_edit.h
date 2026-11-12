/*
TGSpeechBox — YAML round-trip editor interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "yaml_min.h"

namespace tgsb_editor {

using Node = nvsp_frontend::yaml_min::Node;

struct ReplacementWhen {
  bool atWordStart = false;
  bool atWordEnd = false;
  std::string beforeClass; // name from normalization.classes
  std::string afterClass;
  std::string notBeforeClass; // negative condition: match only if next char NOT in class
  std::string notAfterClass;  // negative condition: match only if prev char NOT in class
  // Cross-word conditions: the first sound of the next word, the last of
  // the previous one.  The mapping dialog has no controls for them; they
  // are carried so that changing a mapping does not drop them (#214).
  std::string nextWordStartsClass;
  std::string nextWordStartsNotClass;
  std::string prevWordEndsClass;
  std::string prevWordEndsNotClass;

  bool isEmpty() const {
    return (!atWordStart) && (!atWordEnd) && beforeClass.empty() && afterClass.empty()
        && notBeforeClass.empty() && notAfterClass.empty()
        && nextWordStartsClass.empty() && nextWordStartsNotClass.empty()
        && prevWordEndsClass.empty() && prevWordEndsNotClass.empty();
  }
};

struct ReplacementRule {
  std::string from;
  std::string to;
  // When the file's `to` is a list of candidates (the frontend takes the
  // first one the phoneme table has), `to` is the first and these the rest.
  std::vector<std::string> toMore;
  ReplacementWhen when;
};

// A (from, optional-to) pair used by child packs to suppress inherited rules.
struct SkipRule {
  std::string from;
  std::string to; // empty means "skip all rules with this 'from'"
};

// Allophone rule entry for editor round-trip.
// Vectors of IPA keys are stored as UTF-8 strings (not u32string).
struct AllophoneRuleEntry {
  std::string name;
  // Match conditions
  std::vector<std::string> phonemes;   // IPA keys
  std::vector<std::string> flags;      // e.g. "stop","voiced"
  std::vector<std::string> notFlags;
  std::string tokenType = "phoneme";   // "phoneme"/"aspiration"/"closure"
  std::string position = "any";        // "any"/"word-initial"/"word-final"/"intervocalic"/etc.
  std::string place = "any";            // "any"/"labial"/"alveolar"/"palatal"/"velar"
  std::string stress = "any";          // "any"/"stressed"/"unstressed"/"next-unstressed"/"prev-stressed"
  std::vector<std::string> after;      // neighbor IPA filter
  std::vector<std::string> before;
  std::vector<std::string> afterFlags;    // neighbor flag filters
  std::vector<std::string> notAfterFlags;
  std::vector<std::string> beforeFlags;
  std::vector<std::string> notBeforeFlags;
  bool beforeSamePhoneme = false;
  bool afterSamePhoneme  = false;
  // Action
  std::string action;                  // "replace"/"scale"/"shift"/"insert-before"/"insert-after"
  // Replace params
  std::string replaceTo;
  double replaceDurationMs = 0.0;
  bool replaceRemovesClosure = false;
  bool replaceRemovesAspiration = false;
  double replaceClosureScale = 0.0;
  double replaceAspirationScale = 0.0;
  // Scale params
  double durationScale = 1.0;
  double fadeScale = 1.0;
  std::vector<std::pair<std::string, double>> fieldScales;
  // Shift params
  struct ShiftEntry {
    std::string field;
    double deltaHz = 0.0;
    double targetHz = 0.0;
    double blend = 1.0;
  };
  std::vector<ShiftEntry> fieldShifts;
  // Insert params
  std::string insertPhoneme;
  double insertDurationMs = 18.0;
  double insertFadeMs = 3.0;
  std::vector<std::string> insertContexts;
};

// Special coarticulation rule entry for editor round-trip.
struct SpecialCoarticRuleEntry {
  std::string name;
  std::vector<std::string> triggers;    // IPA keys
  std::string vowelFilter = "all";      // "all"/"front"/"back"/specific IPA key
  std::string formant = "f2";           // "f2" or "f3"
  double deltaHz = 0.0;
  std::string side = "both";            // "left"/"right"/"both"
  bool cumulative = false;
  double unstressedScale = 1.0;
  double phraseFinalStressedScale = 1.0;
};

class PhonemesYaml {
public:
  bool load(const std::string& path, std::string& outError);
  bool save(std::string& outError) const;

  const std::string& path() const { return m_path; }
  bool isLoaded() const { return !m_path.empty(); }

  std::vector<std::string> phonemeKeysSorted() const;

  Node* getPhonemeNode(const std::string& key);
  const Node* getPhonemeNode(const std::string& key) const;

  bool clonePhoneme(const std::string& fromKey, const std::string& newKey, std::string& outError);

  // Carry an edit of one phoneme (original -> working) over to another, a
  // language's own copy of it.  Returns false if targetKey is not there.
  bool applyEditToPhoneme(const std::string& targetKey, const Node& original, const Node& working);

private:
  Node m_root;
  std::string m_path;
};

class LanguageYaml {
public:
  bool load(const std::string& path, std::string& outError);
  bool save(std::string& outError) const;

  const std::string& path() const { return m_path; }
  bool isLoaded() const { return !m_path.empty(); }

  std::vector<ReplacementRule> replacements() const;
  void setReplacements(const std::vector<ReplacementRule>& rules);

  std::vector<SkipRule> skipReplacements() const;
  void setSkipReplacements(const std::vector<SkipRule>& rules);

  std::vector<std::string> classNamesSorted() const;

  // Normalization classes: normalization.classes
  // Each class's members as text, one per line.  setClasses writes the
  // classes whose members differ (as lists) and returns whether any did.
  std::map<std::string, std::string> classes() const;
  bool setClasses(const std::map<std::string, std::string>& classes);

  // Top-level settings: settings:
  // One row per plain value or list of plain values, sorted by key.  A
  // setting inside a block is shown under one joined name
  // (rateCompensation.enabled -> rateCompensationEnabled).  setSettings
  // takes the rows back and writes only what differs from them, into the
  // node each row was read from; a new dotted name goes into its block.
  std::vector<std::pair<std::string, std::string>> settings() const;
  void setSettings(const std::vector<std::pair<std::string, std::string>>& settings);
  void setSetting(const std::string& key, const std::string& value);
  bool removeSetting(const std::string& key);

  // Allophone rules: settings.allophoneRules.rules
  std::vector<AllophoneRuleEntry> allophoneRules() const;
  void setAllophoneRules(const std::vector<AllophoneRuleEntry>& rules);

  // Special coarticulation rules: settings.specialCoarticulation.rules
  std::vector<SpecialCoarticRuleEntry> specialCoarticRules() const;
  void setSpecialCoarticRules(const std::vector<SpecialCoarticRuleEntry>& rules);

private:
  Node m_root;
  std::string m_path;
};

// Serialize the yaml_min::Node tree back to a human-readable YAML subset.
// Note: comments are not preserved.
std::string dumpYaml(const Node& root);

} // namespace tgsb_editor
