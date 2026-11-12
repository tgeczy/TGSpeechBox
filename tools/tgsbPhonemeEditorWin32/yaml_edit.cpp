/*
TGSpeechBox — YAML round-trip editor for phoneme data.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "yaml_edit.h"
#include "yaml_export.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace ye = nvsp_frontend::yaml_export;

namespace tgsb_editor {

// Set a key in a map node, maintaining keyOrder for round-trip fidelity.
static void mapSet(Node& mapNode, const std::string& key, Node value) {
  if (mapNode.map.find(key) == mapNode.map.end()) {
    mapNode.keyOrder.push_back(key);
  }
  mapNode.map[key] = std::move(value);
}

static Node* getMapChild(Node& mapNode, const char* key) {
  if (mapNode.type != Node::Type::Map) {
    mapNode.type = Node::Type::Map;
    mapNode.map.clear();
    mapNode.keyOrder.clear();
    mapNode.seq.clear();
    mapNode.scalar.clear();
  }
  std::string k(key);
  if (mapNode.map.find(k) == mapNode.map.end()) {
    mapNode.keyOrder.push_back(k);
  }
  return &mapNode.map[k];
}

static const Node* getMapChildConst(const Node& mapNode, const char* key) {
  return mapNode.get(key);
}

static Node* getNestedMap(Node& root, const char* key) {
  Node* n = getMapChild(root, key);
  if (n->type != Node::Type::Map) {
    n->type = Node::Type::Map;
    n->map.clear();
    n->seq.clear();
    n->scalar.clear();
  }
  return n;
}

static Node* getNestedSeq(Node& root, const char* key) {
  Node* n = getMapChild(root, key);
  if (n->type != Node::Type::Seq) {
    n->type = Node::Type::Seq;
    n->seq.clear();
    n->map.clear();
    n->scalar.clear();
  }
  return n;
}

bool PhonemesYaml::load(const std::string& path, std::string& outError) {
  Node root;
  std::string err;
  if (!nvsp_frontend::yaml_min::loadFile(path, root, err)) {
    outError = err;
    return false;
  }
  // Ensure the expected structure exists.
  const Node* phonemesNode = root.get("phonemes");
  if (!phonemesNode || !phonemesNode->isMap()) {
    outError = "Expected a top-level 'phonemes:' map";
    return false;
  }
  m_root = std::move(root);
  m_path = path;
  return true;
}


std::vector<std::string> PhonemesYaml::phonemeKeysSorted() const {
  std::vector<std::string> keys;
  const Node* phonemesNode = m_root.get("phonemes");
  if (!phonemesNode || !phonemesNode->isMap()) return keys;
  keys.reserve(phonemesNode->map.size());
  for (const auto& kv : phonemesNode->map) {
    keys.push_back(kv.first);
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

Node* PhonemesYaml::getPhonemeNode(const std::string& key) {
  Node* phonemesNode = getNestedMap(m_root, "phonemes");
  auto it = phonemesNode->map.find(key);
  if (it == phonemesNode->map.end()) return nullptr;
  return &it->second;
}

const Node* PhonemesYaml::getPhonemeNode(const std::string& key) const {
  const Node* phonemesNode = m_root.get("phonemes");
  if (!phonemesNode || !phonemesNode->isMap()) return nullptr;
  auto it = phonemesNode->map.find(key);
  if (it == phonemesNode->map.end()) return nullptr;
  return &it->second;
}

bool PhonemesYaml::clonePhoneme(const std::string& fromKey, const std::string& newKey, std::string& outError) {
  if (newKey.empty()) {
    outError = "New key is empty";
    return false;
  }
  Node* phonemesNode = getNestedMap(m_root, "phonemes");
  auto itFrom = phonemesNode->map.find(fromKey);
  if (itFrom == phonemesNode->map.end()) {
    outError = "Source phoneme not found: " + fromKey;
    return false;
  }
  if (phonemesNode->map.find(newKey) != phonemesNode->map.end()) {
    outError = "Key already exists: " + newKey;
    return false;
  }
  mapSet(*phonemesNode, newKey, itFrom->second);
  return true;
}

// -------------------------
// Language YAML
// -------------------------

bool LanguageYaml::load(const std::string& path, std::string& outError) {
  Node root;
  std::string err;
  if (!nvsp_frontend::yaml_min::loadFile(path, root, err)) {
    outError = err;
    return false;
  }
  // No strict validation; language YAMLs may be minimal.
  m_root = std::move(root);
  m_path = path;
  return true;
}


bool LanguageYaml::save(std::string& outError) const {
  if (m_path.empty()) {
    outError = "No language YAML loaded";
    return false;
  }

  // --- Surgical save: read original, patch only modified top-level sections ---

  // Read original file.
  std::vector<std::string> origLines;
  {
    std::ifstream fin(m_path, std::ios::binary);
    if (fin) {
      std::string line;
      while (std::getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        origLines.push_back(line);
      }
    }
  }

  // If we can't read the original (new file?), fall back to full dump.
  if (origLines.empty()) {
    std::ofstream f(m_path, std::ios::binary);
    if (!f) {
      outError = "Could not write file: " + m_path;
      return false;
    }
    std::string text = dumpYaml(m_root);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return true;
  }

  // Find top-level key ranges in original.
  std::vector<ye::TopLevelRange> ranges;
  size_t headerEnd = 0;
  ye::findTopLevelRanges(origLines, ranges, headerEnd);

  // Build output.
  std::string output;

  // 1. File header (comments/blanks before first top-level key) — verbatim.
  for (size_t i = 0; i < headerEnd; ++i) {
    output += origLines[i];
    output += "\n";
  }

  // Build a set of keys we've output to detect new keys.
  std::unordered_set<std::string> outputKeys;

  // 2. Each top-level section: compare and keep original or re-serialize.
  for (const auto& range : ranges) {
    outputKeys.insert(range.key);
    const Node* memNode = m_root.get(range.key);

    if (!memNode) {
      // Key was deleted from memory — skip it.
      continue;
    }

    // Serialize in-memory version for comparison.
    std::string serialized = ye::dumpSingleTopLevelKey(range.key, *memNode);

    // Strip both for comparison (ignore comments, trailing whitespace).
    std::string origStripped = ye::stripForComparison(origLines, range.startLine, range.endLine);
    std::string serStripped = serialized;
    while (!serStripped.empty() && serStripped.back() == '\n') serStripped.pop_back();

    if (origStripped == serStripped) {
      // Unchanged — keep original verbatim (preserves comments, spacing).
      for (size_t i = range.startLine; i < range.endLine; ++i) {
        output += origLines[i];
        output += "\n";
      }
    } else {
      // Changed — output re-serialized version.
      output += serialized;
    }
  }

  // 3. Append any new top-level keys that weren't in the original file.
  if (m_root.isMap()) {
    auto keys = m_root.keyOrder.empty()
        ? std::vector<std::string>() : m_root.keyOrder;
    if (keys.empty()) {
      for (const auto& kv : m_root.map) keys.push_back(kv.first);
    }
    for (const auto& k : keys) {
      if (outputKeys.find(k) != outputKeys.end()) continue;
      const Node* n = m_root.get(k);
      if (n) {
        output += ye::dumpSingleTopLevelKey(k, *n);
      }
    }
  }

  // Write output.
  std::ofstream fout(m_path, std::ios::binary);
  if (!fout) {
    outError = "Could not write file: " + m_path;
    return false;
  }
  fout.write(output.data(), static_cast<std::streamsize>(output.size()));
  return true;
}

std::vector<ReplacementRule> LanguageYaml::replacements() const {
  std::vector<ReplacementRule> out;

  const Node* norm = m_root.get("normalization");
  if (!norm || !norm->isMap()) return out;

  const Node* repl = norm->get("replacements");
  if (!repl || !repl->isSeq()) return out;

  for (const auto& item : repl->seq) {
    if (!item.isMap()) continue;
    const Node* fromN = item.get("from");
    const Node* toN = item.get("to");
    if (!fromN || !fromN->isScalar() || !toN) continue;

    ReplacementRule r;
    r.from = fromN->scalar;
    if (toN->isScalar()) {
      r.to = toN->scalar;
    } else if (toN->isSeq() && !toN->seq.empty() && toN->seq[0].isScalar()) {
      r.to = toN->seq[0].scalar;
    } else {
      continue;
    }

    const Node* whenN = item.get("when");
    if (whenN && whenN->isMap()) {
      if (const Node* n = whenN->get("atWordStart")) {
        bool b;
        if (n->asBool(b)) r.when.atWordStart = b;
      }
      if (const Node* n = whenN->get("atWordEnd")) {
        bool b;
        if (n->asBool(b)) r.when.atWordEnd = b;
      }
      if (const Node* n = whenN->get("beforeClass")) {
        if (n->isScalar()) r.when.beforeClass = n->scalar;
      }
      if (const Node* n = whenN->get("afterClass")) {
        if (n->isScalar()) r.when.afterClass = n->scalar;
      }
      if (const Node* n = whenN->get("notBeforeClass")) {
        if (n->isScalar()) r.when.notBeforeClass = n->scalar;
      }
      if (const Node* n = whenN->get("notAfterClass")) {
        if (n->isScalar()) r.when.notAfterClass = n->scalar;
      }
    }

    out.push_back(std::move(r));
  }

  return out;
}

void LanguageYaml::setReplacements(const std::vector<ReplacementRule>& rules) {
  // Ensure root is a map.
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }

  Node* norm = getNestedMap(m_root, "normalization");
  Node* repl = getNestedSeq(*norm, "replacements");
  repl->seq.clear();

  for (const auto& r : rules) {
    // Bug 5: skip no-op identity replacements (from == to with no conditions).
    if (r.from == r.to && r.when.isEmpty()) continue;

    Node item;
    item.type = Node::Type::Map;

    Node from;
    from.type = Node::Type::Scalar;
    from.scalar = r.from;

    Node to;
    to.type = Node::Type::Scalar;
    to.scalar = r.to;

    mapSet(item, "from", std::move(from));
    mapSet(item, "to", std::move(to));

    if (!r.when.isEmpty()) {
      Node when;
      when.type = Node::Type::Map;

      if (r.when.atWordStart) {
        Node b;
        b.type = Node::Type::Scalar;
        b.scalar = "true";
        mapSet(when, "atWordStart", std::move(b));
      }
      if (r.when.atWordEnd) {
        Node b;
        b.type = Node::Type::Scalar;
        b.scalar = "true";
        mapSet(when, "atWordEnd", std::move(b));
      }
      if (!r.when.beforeClass.empty()) {
        Node s;
        s.type = Node::Type::Scalar;
        s.scalar = r.when.beforeClass;
        mapSet(when, "beforeClass", std::move(s));
      }
      if (!r.when.afterClass.empty()) {
        Node s;
        s.type = Node::Type::Scalar;
        s.scalar = r.when.afterClass;
        mapSet(when, "afterClass", std::move(s));
      }
      if (!r.when.notBeforeClass.empty()) {
        Node s;
        s.type = Node::Type::Scalar;
        s.scalar = r.when.notBeforeClass;
        mapSet(when, "notBeforeClass", std::move(s));
      }
      if (!r.when.notAfterClass.empty()) {
        Node s;
        s.type = Node::Type::Scalar;
        s.scalar = r.when.notAfterClass;
        mapSet(when, "notAfterClass", std::move(s));
      }

      mapSet(item, "when", std::move(when));
    }

    repl->seq.push_back(std::move(item));
  }
}

std::vector<SkipRule> LanguageYaml::skipReplacements() const {
  std::vector<SkipRule> out;
  const Node* norm = m_root.get("normalization");
  if (!norm || !norm->isMap()) return out;
  const Node* skip = norm->get("skipReplacements");
  if (!skip || !skip->isSeq()) return out;
  for (const auto& item : skip->seq) {
    if (!item.isMap()) continue;
    const Node* fromN = item.get("from");
    if (!fromN || !fromN->isScalar()) continue;
    SkipRule sr;
    sr.from = fromN->scalar;
    const Node* toN = item.get("to");
    if (toN && toN->isScalar()) sr.to = toN->scalar;
    out.push_back(std::move(sr));
  }
  return out;
}

void LanguageYaml::setSkipReplacements(const std::vector<SkipRule>& rules) {
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }
  Node* norm = getNestedMap(m_root, "normalization");
  if (rules.empty()) {
    norm->map.erase("skipReplacements");
    auto& ko = norm->keyOrder;
    ko.erase(std::remove(ko.begin(), ko.end(), "skipReplacements"), ko.end());
    return;
  }
  Node* skip = getNestedSeq(*norm, "skipReplacements");
  skip->seq.clear();
  for (const auto& sr : rules) {
    Node item;
    item.type = Node::Type::Map;
    Node fromN;
    fromN.type = Node::Type::Scalar;
    fromN.scalar = sr.from;
    mapSet(item, "from", std::move(fromN));
    if (!sr.to.empty()) {
      Node toN;
      toN.type = Node::Type::Scalar;
      toN.scalar = sr.to;
      mapSet(item, "to", std::move(toN));
    }
    skip->seq.push_back(std::move(item));
  }
}

std::vector<std::string> LanguageYaml::classNamesSorted() const {
  std::vector<std::string> out;
  const Node* norm = m_root.get("normalization");
  if (!norm || !norm->isMap()) return out;
  const Node* classes = norm->get("classes");
  if (!classes || !classes->isMap()) return out;

  out.reserve(classes->map.size());
  for (const auto& kv : classes->map) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

// A class's members as the Class Editor's box holds them: one per line.
// The box is a Win32 multi-line edit, so lines may end in \r\n.
static std::vector<std::string> splitMemberLines(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t')) cur.erase(cur.begin());
    while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
    if (!cur.empty()) out.push_back(cur);
    cur.clear();
  };
  for (char c : text) {
    if (c == '\n' || c == '\r') flush();
    else cur.push_back(c);
  }
  flush();
  return out;
}

std::map<std::string, std::string> LanguageYaml::classes() const {
  std::map<std::string, std::string> out;
  const Node* norm = m_root.get("normalization");
  if (!norm || !norm->isMap()) return out;
  const Node* classesNode = norm->get("classes");
  if (!classesNode || !classesNode->isMap()) return out;

  for (const auto& kv : classesNode->map) {
    if (kv.second.isSeq()) {
      // A class is a list of symbols (the only form the frontend reads).
      std::string joined;
      for (const auto& item : kv.second.seq) {
        if (!item.isScalar()) continue;
        if (!joined.empty()) joined += "\n";
        joined += item.scalar;
      }
      out[kv.first] = joined;
    } else if (kv.second.isScalar()) {
      // What the editor wrote before #162; shown so it can be put right.
      out[kv.first] = kv.second.scalar;
    } else {
      out[kv.first] = "";
    }
  }
  return out;
}

// Only a class whose members differ from what is in the file is written, as
// a list; a removed class is taken out; everything else stays as it is, so
// OK with nothing changed changes nothing (#162).
bool LanguageYaml::setClasses(const std::map<std::string, std::string>& classes) {
  const std::map<std::string, std::string> before = this->classes();

  bool anyChange = false;
  for (const auto& kv : before) {
    if (classes.find(kv.first) == classes.end()) anyChange = true;
  }
  for (const auto& kv : classes) {
    auto it = before.find(kv.first);
    if (it == before.end() || splitMemberLines(it->second) != splitMemberLines(kv.second)) anyChange = true;
  }
  if (!anyChange) return false;

  // Ensure normalization exists
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }
  Node* norm = getNestedMap(m_root, "normalization");
  Node* classesNode = getNestedMap(*norm, "classes");

  for (const auto& kv : before) {
    if (classes.find(kv.first) != classes.end()) continue;
    classesNode->map.erase(kv.first);
    auto& ko = classesNode->keyOrder;
    ko.erase(std::remove(ko.begin(), ko.end(), kv.first), ko.end());
  }
  for (const auto& kv : classes) {
    const std::vector<std::string> members = splitMemberLines(kv.second);
    auto it = before.find(kv.first);
    if (it != before.end() && splitMemberLines(it->second) == members) continue;
    Node list;
    list.type = Node::Type::Seq;
    for (const auto& m : members) {
      Node item;
      item.type = Node::Type::Scalar;
      item.scalar = m;
      list.seq.push_back(std::move(item));
    }
    mapSet(*classesNode, kv.first, std::move(list));
  }
  return true;
}

// One row of the Language settings dialog: the key it shows, its value, and
// where under `settings:` the value lives.  The path is what an edit goes
// back to; the shown key alone cannot say (rateCompensationEnabled is
// rateCompensation.enabled, and the frontend reads no flat key of that name).
struct SettingRow {
  std::string key;
  std::string value;
  std::vector<std::string> path;
};

// Flatten nested settings into camelCase keys
// e.g., trajectoryLimit.enabled -> trajectoryLimitEnabled
// e.g., trajectoryLimit.maxHzPerMs.cf2 -> trajectoryLimitMaxHzPerMsCf2
static void flattenSettings(const Node& node, const std::string& prefix,
                            std::vector<std::string>& path,
                            std::vector<SettingRow>& out) {
  if (!node.isMap()) return;

  for (const auto& kv : node.map) {
    const std::string& key = kv.first;
    const Node& v = kv.second;

    // Build the flattened key name
    std::string flatKey;
    if (prefix.empty()) {
      flatKey = key;
    } else {
      // CamelCase join: trajectoryLimit + enabled -> trajectoryLimitEnabled
      // Capitalize the first letter of the nested key
      flatKey = prefix;
      if (!key.empty()) {
        flatKey += static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
        flatKey += key.substr(1);
      }
    }

    path.push_back(key);
    if (v.isScalar()) {
      out.push_back({flatKey, v.scalar, path});
    } else if (v.isMap()) {
      // Recurse into nested maps
      flattenSettings(v, flatKey, path, out);
    } else if (v.isSeq()) {
      // For sequences, join elements with commas (e.g., applyTo: [cf2, cf3] -> "cf2,cf3").
      // A list holding anything but plain values (the rule lists) has no
      // row: it cannot be shown as one value, and is left to its own editor.
      std::string joined;
      bool allScalars = true;
      for (size_t i = 0; i < v.seq.size(); ++i) {
        if (!v.seq[i].isScalar()) { allScalars = false; break; }
        if (!joined.empty()) joined += ",";
        joined += v.seq[i].scalar;
      }
      if (allScalars && !joined.empty()) {
        out.push_back({flatKey, joined, path});
      }
    }
    path.pop_back();
  }
}

// The rows of a `settings:` map, sorted by key, every key naming one row.
static std::vector<SettingRow> settingRows(const Node& settingsNode) {
  std::vector<SettingRow> rows;
  std::vector<std::string> path;
  flattenSettings(settingsNode, "", path, rows);

  // Two settings can join to one name: Danish has a flat
  // boundarySmoothingEnabled and a boundarySmoothing block with its own
  // `enabled`.  The one inside a block is then shown by its dotted path
  // (boundarySmoothing.enabled), so each row can be told apart and edited.
  std::unordered_map<std::string, int> uses;
  for (const auto& r : rows) uses[r.key]++;
  for (auto& r : rows) {
    if (uses[r.key] < 2 || r.path.size() < 2) continue;
    std::string dotted;
    for (const auto& p : r.path) {
      if (!dotted.empty()) dotted += ".";
      dotted += p;
    }
    r.key = dotted;
  }

  std::stable_sort(rows.begin(), rows.end(),
                   [](const SettingRow& a, const SettingRow& b) { return a.key < b.key; });
  return rows;
}

std::vector<std::pair<std::string, std::string>> LanguageYaml::settings() const {
  std::vector<std::pair<std::string, std::string>> out;
  const Node* s = m_root.get("settings");
  if (!s || !s->isMap()) return out;

  for (auto& r : settingRows(*s)) out.emplace_back(std::move(r.key), std::move(r.value));
  return out;
}

// Split a comma-separated string into a sequence node
static Node makeSequenceFromCommaSeparated(const std::string& value) {
  Node seq;
  seq.type = Node::Type::Seq;

  size_t start = 0;
  size_t pos;
  while ((pos = value.find(',', start)) != std::string::npos) {
    std::string item = value.substr(start, pos - start);
    // Trim whitespace
    while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
    while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
    if (!item.empty()) {
      Node n;
      n.type = Node::Type::Scalar;
      n.scalar = item;
      seq.seq.push_back(std::move(n));
    }
    start = pos + 1;
  }
  // Last item
  std::string item = value.substr(start);
  while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
  while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
  if (!item.empty()) {
    Node n;
    n.type = Node::Type::Scalar;
    n.scalar = item;
    seq.seq.push_back(std::move(n));
  }

  return seq;
}

// The node a row was read from, or nullptr.
static Node* settingNodeAt(Node& settingsNode, const std::vector<std::string>& path) {
  Node* n = &settingsNode;
  for (const auto& p : path) {
    if (!n->isMap()) return nullptr;
    auto it = n->map.find(p);
    if (it == n->map.end()) return nullptr;
    n = &it->second;
  }
  return n;
}

// Write a row's value back into its own node: a list stays a list.
static void writeSettingValue(Node& node, const std::string& value) {
  if (node.isSeq()) {
    const bool flow = node.flowStyle;
    node = makeSequenceFromCommaSeparated(value);
    node.flowStyle = flow;
    return;
  }
  node.type = Node::Type::Scalar;
  node.scalar = value;
  node.map.clear();
  node.keyOrder.clear();
  node.seq.clear();
}

// Remove a row's node, and any block that held nothing else.
static bool eraseSettingAt(Node& settingsNode, const std::vector<std::string>& path) {
  if (path.empty()) return false;
  std::vector<Node*> chain;
  chain.push_back(&settingsNode);
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    Node* n = chain.back();
    if (!n->isMap()) return false;
    auto it = n->map.find(path[i]);
    if (it == n->map.end()) return false;
    chain.push_back(&it->second);
  }
  if (!chain.back()->isMap()) return false;
  bool erased = false;
  for (size_t i = path.size(); i-- > 0;) {
    Node* parent = chain[i];
    if (parent->map.erase(path[i]) == 0) break;
    auto& ko = parent->keyOrder;
    ko.erase(std::remove(ko.begin(), ko.end(), path[i]), ko.end());
    erased = true;
    if (!parent->map.empty()) break;
  }
  return erased;
}

// Add a setting that has no row yet.  A plain name is written as a flat key,
// which is how the frontend reads every name the dialog's key list offers; a
// dotted name (rateCompensation.minimumDurations.vowelMs) goes into the
// block it names.
static void addSetting(Node& settingsNode, const std::string& key, const std::string& value) {
  std::vector<std::string> path;
  {
    size_t start = 0;
    size_t dot;
    while ((dot = key.find('.', start)) != std::string::npos) {
      path.push_back(key.substr(start, dot - start));
      start = dot + 1;
    }
    path.push_back(key.substr(start));
  }
  // A name that does not lead through blocks (an empty part, or a part that
  // is already a plain value) is kept as typed.
  bool usable = true;
  {
    const Node* n = &settingsNode;
    for (size_t i = 0; i < path.size(); ++i) {
      if (path[i].empty()) { usable = false; break; }
      if (!n) continue;
      auto it = n->map.find(path[i]);
      const Node* child = (it == n->map.end()) ? nullptr : &it->second;
      if (child && i + 1 < path.size() && !child->isMap()) { usable = false; break; }
      n = child;
    }
  }
  if (!usable) path.assign(1, key);

  Node* n = &settingsNode;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    if (n->map.find(path[i]) == n->map.end()) {
      Node block;
      block.type = Node::Type::Map;
      mapSet(*n, path[i], std::move(block));
    }
    n = &n->map[path[i]];
  }
  auto it = n->map.find(path.back());
  if (it != n->map.end()) {
    // Something without a row of its own is already there.  A block or a
    // rule list is not replaced by one value.
    if (it->second.isMap()) return;
    if (it->second.isSeq()) {
      for (const auto& el : it->second.seq) if (!el.isScalar()) return;
    }
    writeSettingValue(it->second, value);
    return;
  }
  Node v;
  v.type = Node::Type::Scalar;
  v.scalar = value;
  mapSet(*n, path.back(), std::move(v));
}

// The dialog hands back every row.  Only what differs from the rows it was
// given is written: a changed value goes into the node it was read from, a
// missing row removes its node, a new row is added.  Everything else under
// `settings:` (blocks, lists, rule lists, key order) stays as it is, so OK
// with nothing changed changes nothing (#161).
void LanguageYaml::setSettings(const std::vector<std::pair<std::string, std::string>>& settings) {
  std::vector<SettingRow> before;
  if (const Node* s = m_root.get("settings"); s && s->isMap()) before = settingRows(*s);

  // Rows as wanted; a key given twice keeps its last value.
  std::vector<std::string> wantedOrder;
  std::unordered_map<std::string, std::string> wanted;
  for (const auto& kv : settings) {
    if (kv.first.empty()) continue;
    if (wanted.find(kv.first) == wanted.end()) wantedOrder.push_back(kv.first);
    wanted[kv.first] = kv.second;
  }

  std::unordered_set<std::string> beforeKeys;
  bool anyChange = false;
  for (const auto& r : before) {
    beforeKeys.insert(r.key);
    auto it = wanted.find(r.key);
    if (it == wanted.end() || it->second != r.value) anyChange = true;
  }
  for (const auto& k : wantedOrder) {
    if (beforeKeys.find(k) == beforeKeys.end()) anyChange = true;
  }
  if (!anyChange) return;

  // Ensure root is a map.
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }
  Node* s = getNestedMap(m_root, "settings");

  for (const auto& r : before) {
    auto it = wanted.find(r.key);
    if (it == wanted.end()) {
      eraseSettingAt(*s, r.path);
    } else if (it->second != r.value) {
      if (Node* n = settingNodeAt(*s, r.path)) writeSettingValue(*n, it->second);
    }
  }
  for (const auto& k : wantedOrder) {
    if (beforeKeys.find(k) == beforeKeys.end()) addSetting(*s, k, wanted[k]);
  }
}

void LanguageYaml::setSetting(const std::string& key, const std::string& value) {
  if (key.empty()) return;
  auto rows = settings();
  bool found = false;
  for (auto& kv : rows) {
    if (kv.first == key) { kv.second = value; found = true; break; }
  }
  if (!found) rows.emplace_back(key, value);
  setSettings(rows);
}

bool LanguageYaml::removeSetting(const std::string& key) {
  if (key.empty()) return false;
  auto rows = settings();
  const size_t n = rows.size();
  rows.erase(std::remove_if(rows.begin(), rows.end(),
                            [&](const auto& kv) { return kv.first == key; }), rows.end());
  if (rows.size() == n) return false;
  setSettings(rows);
  return true;
}

// -------------------------
// Allophone rules YAML I/O
// -------------------------

// Helper: read a YAML sequence of scalars into a vector<string>.
static std::vector<std::string> readStringSeq(const Node* n) {
  std::vector<std::string> out;
  if (!n || !n->isSeq()) return out;
  for (const auto& item : n->seq) {
    if (item.isScalar()) out.push_back(item.scalar);
  }
  return out;
}

// Helper: read a scalar as double (0.0 if missing or parse error).
static double readDouble(const Node* n, double def = 0.0) {
  if (!n || !n->isScalar()) return def;
  try { return std::stod(n->scalar); } catch (...) { return def; }
}

// Helper: read a scalar as bool (false if missing).
static bool readBoolNode(const Node* n, bool def = false) {
  if (!n) return def;
  bool b;
  if (n->asBool(b)) return b;
  return def;
}

// Helper: make a scalar node.
static Node makeScalar(const std::string& s) {
  Node n;
  n.type = Node::Type::Scalar;
  n.scalar = s;
  return n;
}

// Helper: make a scalar node from double.
static Node makeScalarD(double v) {
  std::ostringstream os;
  os << v;
  return makeScalar(os.str());
}

// Helper: write a vector<string> as a YAML sequence of scalars.
static Node makeStringSeqNode(const std::vector<std::string>& vec) {
  Node seq;
  seq.type = Node::Type::Seq;
  for (const auto& s : vec) seq.seq.push_back(makeScalar(s));
  return seq;
}

std::vector<AllophoneRuleEntry> LanguageYaml::allophoneRules() const {
  std::vector<AllophoneRuleEntry> out;
  const Node* s = m_root.get("settings");
  if (!s || !s->isMap()) return out;
  const Node* ar = s->get("allophoneRules");
  if (!ar || !ar->isMap()) return out;
  const Node* rulesN = ar->get("rules");
  if (!rulesN || !rulesN->isSeq()) return out;

  for (const auto& item : rulesN->seq) {
    if (!item.isMap()) continue;
    AllophoneRuleEntry r;
    if (const Node* n = item.get("name")) if (n->isScalar()) r.name = n->scalar;
    r.phonemes = readStringSeq(item.get("phonemes"));
    r.flags = readStringSeq(item.get("flags"));
    r.notFlags = readStringSeq(item.get("notFlags"));
    if (const Node* n = item.get("tokenType")) if (n->isScalar()) r.tokenType = n->scalar;
    if (const Node* n = item.get("position")) if (n->isScalar()) r.position = n->scalar;
    if (const Node* n = item.get("place")) if (n->isScalar()) r.place = n->scalar;
    if (const Node* n = item.get("stress")) if (n->isScalar()) r.stress = n->scalar;
    r.after = readStringSeq(item.get("after"));
    r.before = readStringSeq(item.get("before"));
    r.afterFlags = readStringSeq(item.get("afterFlags"));
    r.notAfterFlags = readStringSeq(item.get("notAfterFlags"));
    r.beforeFlags = readStringSeq(item.get("beforeFlags"));
    r.notBeforeFlags = readStringSeq(item.get("notBeforeFlags"));
    r.beforeSamePhoneme = readBoolNode(item.get("beforeSamePhoneme"));
    r.afterSamePhoneme  = readBoolNode(item.get("afterSamePhoneme"));
    if (const Node* n = item.get("action")) if (n->isScalar()) r.action = n->scalar;
    // Replace params
    if (const Node* n = item.get("replaceTo")) if (n->isScalar()) r.replaceTo = n->scalar;
    r.replaceDurationMs = readDouble(item.get("replaceDurationMs"));
    r.replaceRemovesClosure = readBoolNode(item.get("replaceRemovesClosure"));
    r.replaceRemovesAspiration = readBoolNode(item.get("replaceRemovesAspiration"));
    r.replaceClosureScale = readDouble(item.get("replaceClosureScale"));
    r.replaceAspirationScale = readDouble(item.get("replaceAspirationScale"));
    // Scale params
    r.durationScale = readDouble(item.get("durationScale"), 1.0);
    r.fadeScale = readDouble(item.get("fadeScale"), 1.0);
    if (const Node* fs = item.get("fieldScales")) {
      if (fs->isMap()) {
        for (const auto& kv : fs->map) {
          if (kv.second.isScalar()) {
            try { r.fieldScales.emplace_back(kv.first, std::stod(kv.second.scalar)); }
            catch (...) {}
          }
        }
      }
    }
    // Shift params
    if (const Node* fsh = item.get("fieldShifts")) {
      if (fsh->isSeq()) {
        for (const auto& se : fsh->seq) {
          if (!se.isMap()) continue;
          AllophoneRuleEntry::ShiftEntry entry;
          if (const Node* n = se.get("field")) if (n->isScalar()) entry.field = n->scalar;
          entry.deltaHz = readDouble(se.get("deltaHz"));
          entry.targetHz = readDouble(se.get("targetHz"));
          entry.blend = readDouble(se.get("blend"), 1.0);
          r.fieldShifts.push_back(std::move(entry));
        }
      }
    }
    // Insert params
    if (const Node* n = item.get("insertPhoneme")) if (n->isScalar()) r.insertPhoneme = n->scalar;
    r.insertDurationMs = readDouble(item.get("insertDurationMs"), 18.0);
    r.insertFadeMs = readDouble(item.get("insertFadeMs"), 3.0);
    r.insertContexts = readStringSeq(item.get("insertContexts"));
    out.push_back(std::move(r));
  }
  return out;
}

void LanguageYaml::setAllophoneRules(const std::vector<AllophoneRuleEntry>& rules) {
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
  }
  Node* s = getNestedMap(m_root, "settings");
  Node* ar = getNestedMap(*s, "allophoneRules");

  // Rebuild the rules sequence; preserve other keys (like "enabled").
  Node rulesSeq;
  rulesSeq.type = Node::Type::Seq;

  for (const auto& r : rules) {
    Node item;
    item.type = Node::Type::Map;
    if (!r.name.empty()) mapSet(item, "name", makeScalar(r.name));
    if (!r.phonemes.empty()) mapSet(item, "phonemes", makeStringSeqNode(r.phonemes));
    if (!r.flags.empty()) mapSet(item, "flags", makeStringSeqNode(r.flags));
    if (!r.notFlags.empty()) mapSet(item, "notFlags", makeStringSeqNode(r.notFlags));
    if (r.tokenType != "phoneme") mapSet(item, "tokenType", makeScalar(r.tokenType));
    if (r.position != "any") mapSet(item, "position", makeScalar(r.position));
    if (r.place != "any") mapSet(item, "place", makeScalar(r.place));
    if (r.stress != "any") mapSet(item, "stress", makeScalar(r.stress));
    if (!r.after.empty()) mapSet(item, "after", makeStringSeqNode(r.after));
    if (!r.before.empty()) mapSet(item, "before", makeStringSeqNode(r.before));
    if (!r.afterFlags.empty()) mapSet(item, "afterFlags", makeStringSeqNode(r.afterFlags));
    if (!r.notAfterFlags.empty()) mapSet(item, "notAfterFlags", makeStringSeqNode(r.notAfterFlags));
    if (!r.beforeFlags.empty()) mapSet(item, "beforeFlags", makeStringSeqNode(r.beforeFlags));
    if (!r.notBeforeFlags.empty()) mapSet(item, "notBeforeFlags", makeStringSeqNode(r.notBeforeFlags));
    if (r.beforeSamePhoneme) mapSet(item, "beforeSamePhoneme", makeScalar("true"));
    if (r.afterSamePhoneme) mapSet(item, "afterSamePhoneme", makeScalar("true"));
    if (!r.action.empty()) mapSet(item, "action", makeScalar(r.action));
    // Replace
    if (!r.replaceTo.empty()) mapSet(item, "replaceTo", makeScalar(r.replaceTo));
    if (r.replaceDurationMs != 0.0) mapSet(item, "replaceDurationMs", makeScalarD(r.replaceDurationMs));
    if (r.replaceRemovesClosure) mapSet(item, "replaceRemovesClosure", makeScalar("true"));
    if (r.replaceRemovesAspiration) mapSet(item, "replaceRemovesAspiration", makeScalar("true"));
    if (r.replaceClosureScale != 0.0) mapSet(item, "replaceClosureScale", makeScalarD(r.replaceClosureScale));
    if (r.replaceAspirationScale != 0.0) mapSet(item, "replaceAspirationScale", makeScalarD(r.replaceAspirationScale));
    // Scale
    if (r.durationScale != 1.0) mapSet(item, "durationScale", makeScalarD(r.durationScale));
    if (r.fadeScale != 1.0) mapSet(item, "fadeScale", makeScalarD(r.fadeScale));
    if (!r.fieldScales.empty()) {
      Node fs;
      fs.type = Node::Type::Map;
      for (const auto& kv : r.fieldScales) mapSet(fs, kv.first, makeScalarD(kv.second));
      mapSet(item, "fieldScales", std::move(fs));
    }
    // Shift
    if (!r.fieldShifts.empty()) {
      Node fsh;
      fsh.type = Node::Type::Seq;
      for (const auto& se : r.fieldShifts) {
        Node entry;
        entry.type = Node::Type::Map;
        if (!se.field.empty()) mapSet(entry, "field", makeScalar(se.field));
        if (se.deltaHz != 0.0) mapSet(entry, "deltaHz", makeScalarD(se.deltaHz));
        if (se.targetHz != 0.0) mapSet(entry, "targetHz", makeScalarD(se.targetHz));
        if (se.blend != 1.0) mapSet(entry, "blend", makeScalarD(se.blend));
        fsh.seq.push_back(std::move(entry));
      }
      mapSet(item, "fieldShifts", std::move(fsh));
    }
    // Insert
    if (!r.insertPhoneme.empty()) mapSet(item, "insertPhoneme", makeScalar(r.insertPhoneme));
    if (r.insertDurationMs != 18.0) mapSet(item, "insertDurationMs", makeScalarD(r.insertDurationMs));
    if (r.insertFadeMs != 3.0) mapSet(item, "insertFadeMs", makeScalarD(r.insertFadeMs));
    if (!r.insertContexts.empty()) mapSet(item, "insertContexts", makeStringSeqNode(r.insertContexts));
    rulesSeq.seq.push_back(std::move(item));
  }

  ar->map["rules"] = std::move(rulesSeq);
}

// -------------------------
// Special coarticulation rules YAML I/O
// -------------------------

std::vector<SpecialCoarticRuleEntry> LanguageYaml::specialCoarticRules() const {
  std::vector<SpecialCoarticRuleEntry> out;
  const Node* s = m_root.get("settings");
  if (!s || !s->isMap()) return out;
  const Node* sc = s->get("specialCoarticulation");
  if (!sc || !sc->isMap()) return out;
  const Node* rulesN = sc->get("rules");
  if (!rulesN || !rulesN->isSeq()) return out;

  for (const auto& item : rulesN->seq) {
    if (!item.isMap()) continue;
    SpecialCoarticRuleEntry r;
    // A rule read from the file holds what the file holds: the frontend
    // reads a missing vowelFilter, formant or side as empty, not as the
    // value a new rule starts from.
    r.vowelFilter.clear();
    r.formant.clear();
    r.side.clear();
    if (const Node* n = item.get("name")) if (n->isScalar()) r.name = n->scalar;
    r.triggers = readStringSeq(item.get("triggers"));
    if (const Node* n = item.get("vowelFilter")) if (n->isScalar()) r.vowelFilter = n->scalar;
    if (const Node* n = item.get("formant")) if (n->isScalar()) r.formant = n->scalar;
    r.deltaHz = readDouble(item.get("deltaHz"));
    if (const Node* n = item.get("side")) if (n->isScalar()) r.side = n->scalar;
    r.cumulative = readBoolNode(item.get("cumulative"));
    r.unstressedScale = readDouble(item.get("unstressedScale"), 1.0);
    r.phraseFinalStressedScale = readDouble(item.get("phraseFinalStressedScale"), 1.0);
    out.push_back(std::move(r));
  }
  return out;
}

void LanguageYaml::setSpecialCoarticRules(const std::vector<SpecialCoarticRuleEntry>& rules) {
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
  }
  Node* s = getNestedMap(m_root, "settings");
  Node* sc = getNestedMap(*s, "specialCoarticulation");

  Node rulesSeq;
  rulesSeq.type = Node::Type::Seq;

  for (const auto& r : rules) {
    Node item;
    item.type = Node::Type::Map;
    if (!r.name.empty()) mapSet(item, "name", makeScalar(r.name));
    if (!r.triggers.empty()) mapSet(item, "triggers", makeStringSeqNode(r.triggers));
    // vowelFilter, formant and side are always written: the frontend has no
    // defaults for them, and drops or never applies a rule without them (#213).
    if (!r.vowelFilter.empty()) mapSet(item, "vowelFilter", makeScalar(r.vowelFilter));
    if (!r.formant.empty()) mapSet(item, "formant", makeScalar(r.formant));
    if (r.deltaHz != 0.0) mapSet(item, "deltaHz", makeScalarD(r.deltaHz));
    if (!r.side.empty()) mapSet(item, "side", makeScalar(r.side));
    if (r.cumulative) mapSet(item, "cumulative", makeScalar("true"));
    if (r.unstressedScale != 1.0) mapSet(item, "unstressedScale", makeScalarD(r.unstressedScale));
    if (r.phraseFinalStressedScale != 1.0) mapSet(item, "phraseFinalStressedScale", makeScalarD(r.phraseFinalStressedScale));
    rulesSeq.seq.push_back(std::move(item));
  }

  sc->map["rules"] = std::move(rulesSeq);
}

// -------------------------
// YAML dump (delegates to yaml_export)
// -------------------------

std::string dumpYaml(const Node& root) {
  std::string out = "# Edited by tgsbPhonemeEditor (Win32)\n\n";
  out += ye::dumpYaml(root);
  return out;
}

bool PhonemesYaml::save(std::string& outError) const {
  if (m_path.empty()) {
    outError = "No phonemes YAML loaded";
    return false;
  }

  // --- Surgical save: read original, patch only modified phonemes ---

  // Read original file.
  std::vector<std::string> origLines;
  {
    std::ifstream fin(m_path, std::ios::binary);
    if (fin) {
      std::string line;
      while (std::getline(fin, line)) {
        // Strip trailing \r for consistent handling.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        origLines.push_back(line);
      }
    }
  }

  // If we can't read the original (new file?), fall back to full dump.
  if (origLines.empty()) {
    std::ofstream f(m_path, std::ios::binary);
    if (!f) {
      outError = "Could not write file: " + m_path;
      return false;
    }
    std::string text = dumpYaml(m_root);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return true;
  }

  // Find phoneme line ranges in the original file.
  std::vector<ye::PhonemeLineRange> ranges;
  size_t phonemesBlockStart = 0;
  size_t phonemesBlockEnd = origLines.size();
  ye::findPhonemeRanges(origLines, ranges, phonemesBlockStart, phonemesBlockEnd);

  // Build a map of original phoneme key -> range index for fast lookup.
  std::unordered_map<std::string, size_t> origKeyToRange;
  for (size_t ri = 0; ri < ranges.size(); ++ri) {
    origKeyToRange[ranges[ri].key] = ri;
  }

  // Get the in-memory phonemes map.
  const Node* phonemesNode = m_root.get("phonemes");
  if (!phonemesNode || !phonemesNode->isMap()) {
    outError = "No phonemes map in memory";
    return false;
  }

  // Collect in-memory phoneme keys in their original file order,
  // then append any new keys at the end.
  std::vector<std::string> orderedPhonemeKeys;
  std::unordered_set<std::string> seenKeys;
  for (const auto& r : ranges) {
    if (phonemesNode->map.find(r.key) != phonemesNode->map.end()) {
      orderedPhonemeKeys.push_back(r.key);
      seenKeys.insert(r.key);
    }
    // If a key was deleted from memory, we simply skip it.
  }
  // Append new phonemes that weren't in the original file.
  for (const auto& kv : phonemesNode->map) {
    if (seenKeys.find(kv.first) == seenKeys.end()) {
      orderedPhonemeKeys.push_back(kv.first);
    }
  }

  // Build the output.
  std::string output;

  // 1. Everything before the phonemes block (comments, etc.) — verbatim.
  for (size_t i = 0; i <= phonemesBlockStart; ++i) {
    output += origLines[i];
    output += "\n";
  }

  // 2. Each phoneme: either original lines (if unchanged) or re-serialized.
  for (const auto& pkey : orderedPhonemeKeys) {
    const Node& memNode = phonemesNode->map.at(pkey);
    std::string serialized = ye::dumpSinglePhoneme(pkey, memNode);

    auto origIt = origKeyToRange.find(pkey);
    if (origIt != origKeyToRange.end()) {
      // This phoneme existed in the original file.
      const ye::PhonemeLineRange& range = ranges[origIt->second];

      // Strip comments and compare data content.
      std::string origStripped = ye::stripForComparison(origLines, range.startLine, range.endLine);
      std::string serStripped = serialized;
      while (!serStripped.empty() && serStripped.back() == '\n') serStripped.pop_back();

      bool changed = (origStripped != serStripped);

      if (!changed) {
        // Keep original lines verbatim (with comments, spacing, etc.).
        for (size_t i = range.startLine; i < range.endLine; ++i) {
          output += origLines[i];
          output += "\n";
        }
      } else {
        // Output re-serialized phoneme (comments within this block are lost).
        output += serialized;
      }
    } else {
      // New phoneme — serialize from scratch.
      output += serialized;
    }
  }

  // 3. Everything after the phonemes block — verbatim.
  for (size_t i = phonemesBlockEnd; i < origLines.size(); ++i) {
    output += origLines[i];
    output += "\n";
  }

  // Write the output.
  std::ofstream fout(m_path, std::ios::binary);
  if (!fout) {
    outError = "Could not write file: " + m_path;
    return false;
  }
  fout.write(output.data(), static_cast<std::streamsize>(output.size()));
  return true;
}


} // namespace tgsb_editor
