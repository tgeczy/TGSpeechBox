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

  bool isEmpty() const {
    return (!atWordStart) && (!atWordEnd) && beforeClass.empty() && afterClass.empty()
        && notBeforeClass.empty() && notAfterClass.empty();
  }
};

struct ReplacementRule {
  std::string from;
  std::string to;
  ReplacementWhen when;
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
  std::string stress = "any";          // "any"/"stressed"/"unstressed"/"next-unstressed"/"prev-stressed"
  std::vector<std::string> after;      // neighbor IPA filter
  std::vector<std::string> before;
  std::vector<std::string> afterFlags;    // neighbor flag filters
  std::vector<std::string> notAfterFlags;
  std::vector<std::string> beforeFlags;
  std::vector<std::string> notBeforeFlags;
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

  std::vector<std::string> classNamesSorted() const;

  // Normalization classes: normalization.classes
  std::map<std::string, std::string> classes() const;
  void setClasses(const std::map<std::string, std::string>& classes);

  // Top-level settings: settings:
  // Only scalar values are represented here.
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
