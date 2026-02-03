#pragma once

#include <string>
#include <utility>
#include <vector>

#include "yaml_min.h"

namespace nvsp_editor {

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

  // Top-level settings: settings:
  // Only scalar values are represented here.
  std::vector<std::pair<std::string, std::string>> settings() const;
  void setSettings(const std::vector<std::pair<std::string, std::string>>& settings);
  void setSetting(const std::string& key, const std::string& value);
  bool removeSetting(const std::string& key);

private:
  Node m_root;
  std::string m_path;
};

// Serialize the yaml_min::Node tree back to a human-readable YAML subset.
// Note: comments are not preserved.
std::string dumpYaml(const Node& root);

} // namespace nvsp_editor
