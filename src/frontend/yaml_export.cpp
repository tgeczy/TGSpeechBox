/*
 * yaml_export — YAML serialization with comment-preserving surgical merge.
 *
 * Serialization functions extracted from tools/tgsbPhonemeEditorWin32/yaml_edit.cpp.
 * The surgical-save strategy reads the original file, compares per-section (settings)
 * or per-phoneme (phonemes.yaml), and keeps original text verbatim for unchanged
 * sections — preserving comments, whitespace, and formatting.
 *
 * Copyright 2025-2026 Tamas Geczy.
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "yaml_export.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <cstdint>

namespace nvsp_frontend::yaml_export {

// ── Scalar helpers ────────────────────────────────────────────────

static bool looksLikeNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') i = 1;
    if (i >= s.size()) return false;
    bool anyDigit = false;
    bool dot = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') { anyDigit = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        if (c == 'e' || c == 'E') return true;
        return false;
    }
    return anyDigit;
}

static bool looksLikeBool(const std::string& s) {
    if (s.empty()) return false;
    std::string t;
    t.reserve(s.size());
    for (char c : s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return (t == "true" || t == "false" || t == "yes" || t == "no" ||
            t == "on" || t == "off" || t == "0" || t == "1");
}

// A plain decimal number: optional sign, digits with at most one dot, and an
// optional exponent.  Stricter than looksLikeNumber, which takes anything
// with an 'e' in it; this one decides whether a leading '-' is a minus sign.
static bool isPlainNumber(const std::string& s) {
    size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    size_t digits = 0;
    bool dot = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') { ++digits; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        break;
    }
    if (digits == 0) return false;
    if (i == s.size()) return true;
    if (s[i] != 'e' && s[i] != 'E') return false;
    ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    size_t expDigits = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        ++expDigits;
    }
    return expDigits > 0;
}

static bool needsQuotes(const std::string& s) {
    if (s.empty()) return true;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return true;
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) return true;
        if (u >= 0x80) return true;  // non-ASCII (IPA) -> quote
        if (c == ':' || c == '#' || c == '\n' || c == '\r' || c == '\t') return true;
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') return true;
    }
    // A negative number is a number: -0.15 is written back as -0.15, not
    // "-0.15" (#217).  Any other leading '-' could read as a list item.
    if (s[0] == '-' && !isPlainNumber(s)) return true;
    if (s[0] == '?' || s[0] == '!' || s[0] == '*') return true;
    if (s.find("//") != std::string::npos) return true;
    return false;
}

static std::string quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

static std::string dumpScalar(const std::string& s) {
    if (!needsQuotes(s)) return s;
    return quote(s);
}

static std::string dumpKeyStr(const std::string& s) {
    // A key keeps its quotes even when it is a negative number: `-1: x` at
    // the start of a line reads as a list item.
    if (needsQuotes(s) || s[0] == '-') return quote(s);
    return s;
}

static void emitIndent(std::string& out, int n) {
    out.append(static_cast<size_t>(n), ' ');
}

// ── Key ordering ──────────────────────────────────────────────────

static std::vector<std::string> sortedKeys(const Node& mapNode) {
    std::vector<std::string> keys;
    keys.reserve(mapNode.map.size());
    for (const auto& kv : mapNode.map) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    return keys;
}

static std::vector<std::string> orderedKeys(const Node& mapNode) {
    if (!mapNode.keyOrder.empty()) {
        std::vector<std::string> result;
        result.reserve(mapNode.map.size());
        std::unordered_set<std::string> seen;
        for (const auto& k : mapNode.keyOrder) {
            if (mapNode.map.find(k) != mapNode.map.end() && seen.insert(k).second) {
                result.push_back(k);
            }
        }
        if (result.size() < mapNode.map.size()) {
            std::vector<std::string> extra;
            for (const auto& kv : mapNode.map) {
                if (seen.find(kv.first) == seen.end()) extra.push_back(kv.first);
            }
            std::sort(extra.begin(), extra.end());
            for (auto& k : extra) result.push_back(std::move(k));
        }
        return result;
    }
    return sortedKeys(mapNode);
}

static int topLevelKeyPriority(const std::string& key) {
    if (key == "settings") return 0;
    if (key == "normalization") return 1;
    if (key == "transforms") return 2;
    if (key == "intonation") return 3;
    if (key == "toneContours") return 4;
    return 100;
}

static std::vector<std::string> sortedKeysTopLevel(const Node& mapNode) {
    std::vector<std::string> keys;
    keys.reserve(mapNode.map.size());
    for (const auto& kv : mapNode.map) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
        int pa = topLevelKeyPriority(a);
        int pb = topLevelKeyPriority(b);
        if (pa != pb) return pa < pb;
        return a < b;
    });
    return keys;
}

// ── Node serialization ────────────────────────────────────────────

static void dumpNode(const Node& node, std::string& out, int ind);

static void dumpFlowMap(const Node& node, std::string& out) {
    out += "{";
    auto keys = orderedKeys(node);
    bool first = true;
    for (const auto& k : keys) {
        if (!first) out += ", ";
        first = false;
        out += dumpKeyStr(k);
        out += ": ";
        const Node& v = node.map.at(k);
        if (v.isScalar()) {
            out += dumpScalar(v.scalar);
        } else if (v.isSeq() && v.flowStyle) {
            out += "[";
            bool firstItem = true;
            for (const auto& item : v.seq) {
                if (!firstItem) out += ", ";
                firstItem = false;
                out += dumpScalar(item.scalar);
            }
            out += "]";
        } else {
            out += dumpScalar(v.isScalar() ? v.scalar : "");
        }
    }
    out += "}";
}

static void dumpFlowSeq(const Node& node, std::string& out) {
    out += "[";
    bool first = true;
    for (const auto& item : node.seq) {
        if (!first) out += ", ";
        first = false;
        out += dumpScalar(item.scalar);
    }
    out += "]";
}

static void dumpSeqItemMapInlineFirstKey(const Node& item, std::string& out, int ind) {
    auto keys = orderedKeys(item);
    std::string first;
    if (!keys.empty() && item.map.at(keys[0]).type == Node::Type::Scalar) {
        first = keys[0];
    }
    if (first.empty()) {
        if (item.map.find("from") != item.map.end()) first = "from";
        else if (item.map.find("key") != item.map.end()) first = "key";
        else if (!keys.empty()) first = keys[0];
    }
    if (first.empty() || item.map.at(first).type != Node::Type::Scalar) {
        out += "\n";
        dumpNode(item, out, ind + 2);
        return;
    }
    out += " ";
    out += dumpKeyStr(first);
    out += ": ";
    out += dumpScalar(item.map.at(first).scalar);
    out += "\n";
    for (const auto& k : keys) {
        if (k == first) continue;
        const Node& v = item.map.at(k);
        emitIndent(out, ind + 2);
        out += dumpKeyStr(k);
        if (v.type == Node::Type::Scalar) {
            out += ": ";
            out += dumpScalar(v.scalar);
            out += "\n";
        } else if (v.isMap() && v.flowStyle) {
            out += ": ";
            dumpFlowMap(v, out);
            out += "\n";
        } else if (v.isSeq() && v.flowStyle) {
            out += ": ";
            dumpFlowSeq(v, out);
            out += "\n";
        } else {
            out += ":\n";
            dumpNode(v, out, ind + 4);
        }
    }
}

static void dumpMap(const Node& node, std::string& out, int ind) {
    auto keys = (ind == 0) ? sortedKeysTopLevel(node) : orderedKeys(node);
    for (const auto& k : keys) {
        const Node& v = node.map.at(k);
        emitIndent(out, ind);
        out += dumpKeyStr(k);
        if (v.type == Node::Type::Scalar) {
            out += ": ";
            out += dumpScalar(v.scalar);
            out += "\n";
            continue;
        }
        if (v.isMap() && v.flowStyle) {
            out += ": ";
            dumpFlowMap(v, out);
            out += "\n";
            continue;
        }
        if (v.isSeq() && v.flowStyle) {
            out += ": ";
            dumpFlowSeq(v, out);
            out += "\n";
            continue;
        }
        out += ":\n";
        dumpNode(v, out, ind + 2);
    }
}

static void dumpSeq(const Node& node, std::string& out, int ind) {
    for (const auto& item : node.seq) {
        emitIndent(out, ind);
        out += "-";
        if (item.type == Node::Type::Scalar) {
            out += " ";
            out += dumpScalar(item.scalar);
            out += "\n";
            continue;
        }
        if (item.type == Node::Type::Map) {
            if (item.map.empty()) {
                out += " {}\n";
            } else if (item.flowStyle) {
                out += " ";
                dumpFlowMap(item, out);
                out += "\n";
            } else {
                dumpSeqItemMapInlineFirstKey(item, out, ind);
            }
            continue;
        }
        if (item.type == Node::Type::Seq) {
            if (item.flowStyle) {
                out += " ";
                dumpFlowSeq(item, out);
                out += "\n";
            } else {
                out += "\n";
                dumpSeq(item, out, ind + 2);
            }
            continue;
        }
        out += "\n";  // Null
    }
}

static void dumpNode(const Node& node, std::string& out, int ind) {
    switch (node.type) {
        case Node::Type::Map:    dumpMap(node, out, ind); break;
        case Node::Type::Seq:    dumpSeq(node, out, ind); break;
        case Node::Type::Scalar:
            emitIndent(out, ind);
            out += dumpScalar(node.scalar);
            out += "\n";
            break;
        case Node::Type::Null:
        default:
            break;
    }
}

// ── Public serialization API ──────────────────────────────────────

std::string dumpYaml(const Node& root) {
    std::string out;
    dumpNode(root, out, 0);
    return out;
}

std::string dumpSingleTopLevelKey(const std::string& key, const Node& value) {
    std::string out;
    out += dumpKeyStr(key) + ":";
    if (value.isScalar()) {
        out += " " + value.scalar + "\n";
    } else {
        out += "\n";
        dumpNode(value, out, 2);
    }
    return out;
}

std::string dumpSinglePhoneme(const std::string& key, const Node& node) {
    std::string out;
    out += "  ";
    out += dumpKeyStr(key);
    out += ":\n";
    if (node.isMap()) {
        auto keys = orderedKeys(node);
        for (const auto& k : keys) {
            const Node& v = node.map.at(k);
            out += "    ";
            out += dumpKeyStr(k);
            if (v.type == Node::Type::Scalar) {
                out += ": ";
                out += dumpScalar(v.scalar);
                out += "\n";
            } else if (v.isMap() && v.flowStyle) {
                out += ": ";
                dumpFlowMap(v, out);
                out += "\n";
            } else if (v.isSeq() && v.flowStyle) {
                out += ": ";
                dumpFlowSeq(v, out);
                out += "\n";
            } else {
                out += ":\n";
                dumpNode(v, out, 6);
            }
        }
    }
    return out;
}

// ── Surgical comparison helpers ───────────────────────────────────

void findTopLevelRanges(const std::vector<std::string>& lines,
                               std::vector<TopLevelRange>& ranges,
                               size_t& headerEnd) {
    headerEnd = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        if (ln.empty()) continue;
        if (ln[0] == ' ' || ln[0] == '#' || ln[0] == '\r') continue;
        auto colon = ln.find(':');
        if (colon == std::string::npos) continue;
        TopLevelRange r;
        r.key = ln.substr(0, colon);
        r.startLine = i;
        if (!ranges.empty()) {
            ranges.back().endLine = i;
        } else {
            headerEnd = i;
        }
        ranges.push_back(r);
    }
    if (!ranges.empty()) {
        ranges.back().endLine = lines.size();
    }
}

std::string stripForComparison(const std::vector<std::string>& lines,
                                      size_t start, size_t end) {
    std::string out;
    for (size_t i = start; i < end; ++i) {
        std::string ln = lines[i];
        while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\r')) ln.pop_back();
        size_t firstNonSpace = ln.find_first_not_of(' ');
        if (firstNonSpace == std::string::npos) continue;
        if (ln[firstNonSpace] == '#') continue;
        size_t hashPos = ln.find(" #", firstNonSpace);
        if (hashPos != std::string::npos) {
            ln = ln.substr(0, hashPos);
            while (!ln.empty() && ln.back() == ' ') ln.pop_back();
        }
        out += ln;
        out += "\n";
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

bool sameData(const Node& a, const Node& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case Node::Type::Scalar:
            return a.scalar == b.scalar;
        case Node::Type::Seq:
            if (a.seq.size() != b.seq.size()) return false;
            for (size_t i = 0; i < a.seq.size(); ++i) {
                if (!sameData(a.seq[i], b.seq[i])) return false;
            }
            return true;
        case Node::Type::Map:
            if (a.map.size() != b.map.size()) return false;
            for (const auto& kv : a.map) {
                auto it = b.map.find(kv.first);
                if (it == b.map.end() || !sameData(kv.second, it->second)) return false;
            }
            return true;
        case Node::Type::Null:
        default:
            return true;
    }
}

void findPhonemeRanges(const std::vector<std::string>& lines,
                              std::vector<PhonemeLineRange>& ranges,
                              size_t& phonemesBlockStart,
                              size_t& phonemesBlockEnd) {
    phonemesBlockStart = 0;
    phonemesBlockEnd = lines.size();
    size_t pStart = SIZE_MAX;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        if (ln.size() >= 9 && ln.substr(0, 9) == "phonemes:") {
            pStart = i;
            phonemesBlockStart = i;
            break;
        }
    }
    if (pStart == SIZE_MAX) return;
    PhonemeLineRange cur;
    bool inPhoneme = false;
    for (size_t i = pStart + 1; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        if (!ln.empty() && ln[0] != ' ' && ln[0] != '#') {
            phonemesBlockEnd = i;
            if (inPhoneme) {
                cur.endLine = i;
                ranges.push_back(cur);
            }
            return;
        }
        if (ln.size() >= 3 && ln[0] == ' ' && ln[1] == ' ' && ln[2] != ' ' && ln[2] != '#') {
            if (inPhoneme) {
                cur.endLine = i;
                ranges.push_back(cur);
            }
            std::string rawKey = ln.substr(2);
            while (!rawKey.empty() && (rawKey.back() == ' ' || rawKey.back() == '\r')) rawKey.pop_back();
            if (!rawKey.empty() && rawKey.back() == ':') rawKey.pop_back();
            if (rawKey.size() >= 2 && rawKey.front() == '"' && rawKey.back() == '"') {
                rawKey = rawKey.substr(1, rawKey.size() - 2);
            }
            cur = PhonemeLineRange();
            cur.key = rawKey;
            cur.startLine = i;
            inPhoneme = true;
        }
    }
    phonemesBlockEnd = lines.size();
    if (inPhoneme) {
        cur.endLine = lines.size();
        ranges.push_back(cur);
    }
}

// ── Node tree patching ────────────────────────────────────────────

/// Navigate into a Node tree using dot-notation and set a scalar value.
/// Creates intermediate map nodes as needed.
static void setNestedValue(Node& root, const std::string& dotKey, const std::string& value) {
    // Split dotKey by '.'
    std::vector<std::string> parts;
    std::istringstream ss(dotKey);
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (!part.empty()) parts.push_back(part);
    }
    if (parts.empty()) return;

    // Navigate to parent, creating maps as needed.
    Node* cur = &root;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        if (cur->type != Node::Type::Map) {
            cur->type = Node::Type::Map;
            cur->map.clear();
            cur->keyOrder.clear();
        }
        auto it = cur->map.find(parts[i]);
        if (it == cur->map.end()) {
            cur->keyOrder.push_back(parts[i]);
            cur = &cur->map[parts[i]];
            cur->type = Node::Type::Map;
        } else {
            cur = &it->second;
        }
    }

    // Set the leaf value.
    const std::string& leafKey = parts.back();
    if (cur->type != Node::Type::Map) {
        cur->type = Node::Type::Map;
        cur->map.clear();
        cur->keyOrder.clear();
    }
    auto it = cur->map.find(leafKey);
    if (it == cur->map.end()) {
        cur->keyOrder.push_back(leafKey);
    }
    Node& leaf = cur->map[leafKey];
    leaf.type = Node::Type::Scalar;
    leaf.scalar = value;
}

// ── Surgical merge: settings (language YAML) ──────────────────────

static std::string surgicalMergeSettings(
    const std::vector<std::string>& origLines,
    Node& root,
    const std::vector<std::pair<std::string, std::string>>& overrides) {

    // The file as it was read, before the overrides go in.  A section whose
    // data the overrides did not change keeps its lines, however they are
    // spelled (#217).
    const Node pristine = root;

    // Apply overrides into the "settings" block of the Node tree.
    Node* settings = nullptr;
    for (auto& kv : root.map) {
        if (kv.first == "settings") { settings = &kv.second; break; }
    }
    for (const auto& [dotKey, value] : overrides) {
        if (settings) {
            setNestedValue(*settings, dotKey, value);
        }
    }

    // Surgical comparison per top-level section.
    std::vector<TopLevelRange> ranges;
    size_t headerEnd = 0;
    findTopLevelRanges(origLines, ranges, headerEnd);

    std::string output;
    // File header (comments before first key) — verbatim.
    for (size_t i = 0; i < headerEnd; ++i) {
        output += origLines[i];
        output += "\n";
    }

    std::unordered_set<std::string> outputKeys;
    for (const auto& range : ranges) {
        outputKeys.insert(range.key);
        const Node* memNode = root.get(range.key);
        if (!memNode) continue;

        std::string serialized = dumpSingleTopLevelKey(range.key, *memNode);
        const Node* origNode = pristine.get(range.key);

        if (origNode && sameData(*origNode, *memNode)) {
            for (size_t i = range.startLine; i < range.endLine; ++i) {
                output += origLines[i];
                output += "\n";
            }
        } else {
            output += serialized;
        }
    }

    // Append new top-level keys not in original.
    if (root.isMap()) {
        auto keys = root.keyOrder.empty() ? std::vector<std::string>() : root.keyOrder;
        if (keys.empty()) {
            for (const auto& kv : root.map) keys.push_back(kv.first);
        }
        for (const auto& k : keys) {
            if (outputKeys.find(k) != outputKeys.end()) continue;
            const Node* n = root.get(k);
            if (n) output += dumpSingleTopLevelKey(k, *n);
        }
    }

    return output;
}

// ── Surgical merge: phonemes ──────────────────────────────────────

static std::string surgicalMergePhonemes(
    const std::vector<std::string>& origLines,
    Node& root,
    const std::vector<std::pair<std::string, std::string>>& overrides) {

    // The file as it was read, before the overrides go in.  A phoneme whose
    // data the overrides did not change keeps its lines, however they are
    // spelled: a quoted key, a negative number, its comments (#217).
    const Node pristine = root;
    const Node* pristinePhonemes = pristine.get("phonemes");

    // Apply overrides into the "phonemes" block of the Node tree.
    // Override keys are "phonemeKey.fieldName" or "phonemeKey.frameEx.fieldName".
    Node* phonemes = nullptr;
    for (auto& kv : root.map) {
        if (kv.first == "phonemes") { phonemes = &kv.second; break; }
    }
    if (phonemes) {
        for (const auto& [fullKey, value] : overrides) {
            // Split: first segment is phoneme key, rest is field path.
            auto firstDot = fullKey.find('.');
            if (firstDot == std::string::npos) continue;
            std::string phonemeKey = fullKey.substr(0, firstDot);
            std::string fieldPath = fullKey.substr(firstDot + 1);

            // Navigate to the phoneme node, create if needed.
            auto it = phonemes->map.find(phonemeKey);
            if (it == phonemes->map.end()) {
                phonemes->keyOrder.push_back(phonemeKey);
                phonemes->map[phonemeKey].type = Node::Type::Map;
                it = phonemes->map.find(phonemeKey);
            }
            setNestedValue(it->second, fieldPath, value);
        }
    }

    // Surgical comparison per phoneme.
    std::vector<PhonemeLineRange> ranges;
    size_t phonemesBlockStart = 0, phonemesBlockEnd = origLines.size();
    findPhonemeRanges(origLines, ranges, phonemesBlockStart, phonemesBlockEnd);

    std::string output;

    // Everything before the phonemes block — verbatim.
    for (size_t i = 0; i < phonemesBlockStart; ++i) {
        output += origLines[i];
        output += "\n";
    }

    // The "phonemes:" line itself.
    if (phonemesBlockStart < origLines.size()) {
        output += origLines[phonemesBlockStart];
        output += "\n";
    }

    // Each phoneme: compare and keep original or re-serialize.
    std::unordered_set<std::string> outputKeys;
    for (const auto& range : ranges) {
        outputKeys.insert(range.key);
        if (!phonemes) {
            // No phonemes node — keep original.
            for (size_t i = range.startLine; i < range.endLine; ++i) {
                output += origLines[i];
                output += "\n";
            }
            continue;
        }
        const Node* memNode = phonemes->get(range.key);
        if (!memNode) continue;

        std::string serialized = dumpSinglePhoneme(range.key, *memNode);
        const Node* origNode = (pristinePhonemes && pristinePhonemes->isMap())
            ? pristinePhonemes->get(range.key) : nullptr;

        if (origNode && sameData(*origNode, *memNode)) {
            for (size_t i = range.startLine; i < range.endLine; ++i) {
                output += origLines[i];
                output += "\n";
            }
        } else {
            output += serialized;
        }
    }

    // Append new phonemes not in original.
    if (phonemes && phonemes->isMap()) {
        auto keys = orderedKeys(*phonemes);
        for (const auto& k : keys) {
            if (outputKeys.find(k) != outputKeys.end()) continue;
            const Node* n = phonemes->get(k);
            if (n) output += dumpSinglePhoneme(k, *n);
        }
    }

    // Everything after the phonemes block — verbatim.
    for (size_t i = phonemesBlockEnd; i < origLines.size(); ++i) {
        output += origLines[i];
        output += "\n";
    }

    return output;
}

// ── Public API ────────────────────────────────────────────────────

std::string exportMergedYaml(
    const std::string& baseFilePath,
    const std::vector<std::pair<std::string, std::string>>& overrides,
    bool isPhonemes) {

    // Read original file as lines.
    std::vector<std::string> origLines;
    {
        std::ifstream fin(baseFilePath, std::ios::binary);
        if (!fin) return {};
        std::string line;
        while (std::getline(fin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            origLines.push_back(line);
        }
    }
    if (origLines.empty()) return {};

    // Parse into Node tree.
    Node root;
    std::string parseError;
    if (!yaml_min::loadFile(baseFilePath, root, parseError)) return {};
    if (root.type == Node::Type::Null) return {};

    // If no overrides, return original file verbatim.
    if (overrides.empty()) {
        std::string out;
        for (const auto& ln : origLines) {
            out += ln;
            out += "\n";
        }
        return out;
    }

    if (isPhonemes) {
        return surgicalMergePhonemes(origLines, root, overrides);
    } else {
        return surgicalMergeSettings(origLines, root, overrides);
    }
}

} // namespace nvsp_frontend::yaml_export
