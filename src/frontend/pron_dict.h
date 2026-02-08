#ifndef TGSB_FRONTEND_PRON_DICT_H
#define TGSB_FRONTEND_PRON_DICT_H

#include <string>
#include <unordered_map>
#include <vector>

namespace nvsp_frontend {

/// Simple pronunciation dictionary.
///
/// Loads a TSV file where each line is:  WORD<TAB>IPA
///
/// CMUdict variant entries like "read(2)" are stored under the base word
/// ("READ") so that a single lookup returns all known pronunciations.
///
/// Lookups are case-insensitive (keys stored as uppercase ASCII).

class PronDict {
public:
  /// Load a TSV file (word<TAB>ipa per line).
  /// Returns false on error; writes a human-readable message to outError.
  bool loadTSV(const std::string& path, std::string& outError);

  /// Look up a word (case-insensitive).
  /// Returns the first (most common) pronunciation, or empty string if
  /// the word is not in the dictionary.
  std::string lookup(const std::string& word) const;

  /// Look up all variant pronunciations for a word.
  /// Returns an empty vector if the word is not found.
  std::vector<std::string> lookupAll(const std::string& word) const;

  /// Number of base words loaded (not counting variants).
  size_t size() const;

  /// True if a dictionary file has been successfully loaded.
  bool loaded() const;

private:
  /// Key = uppercase base word, Value = IPA strings (first = most common).
  std::unordered_map<std::string, std::vector<std::string>> entries_;
  bool loaded_ = false;
};

} // namespace nvsp_frontend

#endif
