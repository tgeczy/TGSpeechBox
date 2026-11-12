/*
 * yaml_export — YAML serialization with comment-preserving surgical merge.
 *
 * Extracted from the Win32 phoneme editor's yaml_edit.cpp to make YAML
 * export available from the C frontend library.  Used by exportData() to
 * produce merged YAML (base file + user overrides) for mobile export.
 *
 * Copyright 2025-2026 Tamas Geczy.
 * Licensed under the MIT License. See LICENSE for details.
 */

#pragma once

#include "yaml_min.h"

#include <string>
#include <utility>
#include <vector>

namespace nvsp_frontend::yaml_export {

using Node = yaml_min::Node;

// ── Node-tree serialization ───────────────────────────────────────

/// Serialize a yaml_min::Node tree to YAML text (full dump, no comment preservation).
std::string dumpYaml(const Node& root);

/// Serialize a single top-level key and its value.
std::string dumpSingleTopLevelKey(const std::string& key, const Node& value);

/// Serialize a single phoneme definition (at 2-space indent under "phonemes:").
std::string dumpSinglePhoneme(const std::string& key, const Node& node);

// ── Surgical save helpers ─────────────────────────────────────────
// Exposed for reuse by the Win32 phoneme editor's save paths.

struct TopLevelRange {
    std::string key;
    size_t startLine = 0;
    size_t endLine = 0;
};

struct PhonemeLineRange {
    std::string key;
    size_t startLine = 0;
    size_t endLine = 0;
};

void findTopLevelRanges(const std::vector<std::string>& lines,
                        std::vector<TopLevelRange>& ranges,
                        size_t& headerEnd);

void findPhonemeRanges(const std::vector<std::string>& lines,
                       std::vector<PhonemeLineRange>& ranges,
                       size_t& phonemesBlockStart,
                       size_t& phonemesBlockEnd);

std::string stripForComparison(const std::vector<std::string>& lines,
                               size_t start, size_t end);

/// True when two trees hold the same data: node types, scalar text, list
/// items in order, map keys and their values.  Key order, flow style and
/// how the text was spelled (quotes, comments) are not data.  A surgical
/// save keeps a block's original lines when its data is unchanged.
bool sameData(const Node& a, const Node& b);

// ── Surgical merge ────────────────────────────────────────────────

/// Read a base YAML file, apply key-value overrides at the Node level,
/// and return merged YAML text with comments preserved via surgical comparison.
///
/// For settings (language YAML): overrides use dot-notation keys that
/// navigate into nested blocks (e.g. "boundarySmoothing.enabled").
///
/// For phonemes (phonemes.yaml): overrides use "phonemeKey.fieldName"
/// or "phonemeKey.frameEx.fieldName" notation.
///
/// @param baseFilePath  Path to the base YAML file on disk.
/// @param overrides     Vector of (dotKey, value) pairs.
/// @param isPhonemes    True if the file is phonemes.yaml (uses per-phoneme surgical save).
/// @return Merged YAML text, or empty string on error.
std::string exportMergedYaml(
    const std::string& baseFilePath,
    const std::vector<std::pair<std::string, std::string>>& overrides,
    bool isPhonemes);

} // namespace nvsp_frontend::yaml_export
