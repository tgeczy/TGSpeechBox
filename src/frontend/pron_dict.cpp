#include "pron_dict.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace nvsp_frontend {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Convert a string to uppercase ASCII (for case-insensitive keys).
static std::string toUpper(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::toupper(c)));
  }
  return out;
}

/// Strip a CMUdict variant suffix like "(2)" from the end of a word,
/// returning the base form.  "READ(2)" -> "READ",  "A" -> "A".
static std::string stripVariantSuffix(const std::string& word) {
  if (word.size() >= 3 && word.back() == ')') {
    auto pos = word.rfind('(');
    if (pos != std::string::npos && pos > 0) {
      return word.substr(0, pos);
    }
  }
  return word;
}

// ---------------------------------------------------------------------------
// PronDict
// ---------------------------------------------------------------------------

bool PronDict::loadTSV(const std::string& path, std::string& outError) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    outError = "Cannot open pronunciation dictionary: " + path;
    return false;
  }

  entries_.clear();
  loaded_ = false;

  std::string line;
  int lineNo = 0;
  while (std::getline(f, line)) {
    ++lineNo;
    // Trim trailing \r (Windows line endings).
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) continue;

    // Find the tab separator.
    auto tab = line.find('\t');
    if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) {
      continue; // skip malformed lines silently
    }

    std::string rawWord = line.substr(0, tab);
    std::string ipa = line.substr(tab + 1);

    // Normalize key: strip variant suffix, uppercase.
    std::string key = toUpper(stripVariantSuffix(rawWord));

    entries_[key].push_back(std::move(ipa));
  }

  loaded_ = true;
  return true;
}

std::string PronDict::lookup(const std::string& word) const {
  if (!loaded_) return {};
  auto it = entries_.find(toUpper(word));
  if (it == entries_.end() || it->second.empty()) return {};
  return it->second.front();
}

std::vector<std::string> PronDict::lookupAll(const std::string& word) const {
  if (!loaded_) return {};
  auto it = entries_.find(toUpper(word));
  if (it == entries_.end()) return {};
  return it->second;
}

size_t PronDict::size() const {
  return entries_.size();
}

bool PronDict::loaded() const {
  return loaded_;
}

} // namespace nvsp_frontend
