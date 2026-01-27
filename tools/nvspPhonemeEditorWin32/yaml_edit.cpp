#include "yaml_edit.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <sstream>

namespace nvsp_editor {

static Node* getMapChild(Node& mapNode, const char* key) {
  if (mapNode.type != Node::Type::Map) {
    mapNode.type = Node::Type::Map;
    mapNode.map.clear();
    mapNode.seq.clear();
    mapNode.scalar.clear();
  }
  return &mapNode.map[std::string(key)];
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

bool PhonemesYaml::save(std::string& outError) const {
  if (m_path.empty()) {
    outError = "No phonemes YAML loaded";
    return false;
  }
  std::ofstream f(m_path, std::ios::binary);
  if (!f) {
    outError = "Could not write file: " + m_path;
    return false;
  }
  std::string text = dumpYaml(m_root);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
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
  phonemesNode->map[newKey] = itFrom->second;
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
  std::ofstream f(m_path, std::ios::binary);
  if (!f) {
    outError = "Could not write file: " + m_path;
    return false;
  }
  std::string text = dumpYaml(m_root);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
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
    m_root.seq.clear();
    m_root.scalar.clear();
  }

  Node* norm = getNestedMap(m_root, "normalization");
  Node* repl = getNestedSeq(*norm, "replacements");
  repl->seq.clear();

  for (const auto& r : rules) {
    Node item;
    item.type = Node::Type::Map;

    Node from;
    from.type = Node::Type::Scalar;
    from.scalar = r.from;

    Node to;
    to.type = Node::Type::Scalar;
    to.scalar = r.to;

    item.map["from"] = std::move(from);
    item.map["to"] = std::move(to);

    if (!r.when.isEmpty()) {
      Node when;
      when.type = Node::Type::Map;

      if (r.when.atWordStart) {
        Node b;
        b.type = Node::Type::Scalar;
        b.scalar = "true";
        when.map["atWordStart"] = std::move(b);
      }
      if (r.when.atWordEnd) {
        Node b;
        b.type = Node::Type::Scalar;
        b.scalar = "true";
        when.map["atWordEnd"] = std::move(b);
      }
      if (!r.when.beforeClass.empty()) {
        Node s;
        s.type = Node::Type::Scalar;
        s.scalar = r.when.beforeClass;
        when.map["beforeClass"] = std::move(s);
      }
      if (!r.when.afterClass.empty()) {
        Node s;
        s.type = Node::Type::Scalar;
        s.scalar = r.when.afterClass;
        when.map["afterClass"] = std::move(s);
      }

      item.map["when"] = std::move(when);
    }

    repl->seq.push_back(std::move(item));
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

std::vector<std::pair<std::string, std::string>> LanguageYaml::settings() const {
  std::vector<std::pair<std::string, std::string>> out;
  const Node* s = m_root.get("settings");
  if (!s || !s->isMap()) return out;

  out.reserve(s->map.size());
  for (const auto& kv : s->map) {
    const std::string& key = kv.first;
    const Node& v = kv.second;
    if (!v.isScalar()) continue;
    out.emplace_back(key, v.scalar);
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

void LanguageYaml::setSettings(const std::vector<std::pair<std::string, std::string>>& settings) {
  // Ensure root is a map.
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }

  Node* s = getNestedMap(m_root, "settings");
  s->map.clear();
  s->seq.clear();
  s->scalar.clear();

  for (const auto& kv : settings) {
    if (kv.first.empty()) continue;
    Node v;
    v.type = Node::Type::Scalar;
    v.scalar = kv.second;
    s->map[kv.first] = std::move(v);
  }
}

void LanguageYaml::setSetting(const std::string& key, const std::string& value) {
  if (key.empty()) return;
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }
  Node* s = getNestedMap(m_root, "settings");
  Node v;
  v.type = Node::Type::Scalar;
  v.scalar = value;
  s->map[key] = std::move(v);
}

bool LanguageYaml::removeSetting(const std::string& key) {
  if (key.empty()) return false;
  if (m_root.type != Node::Type::Map) return false;

  auto it = m_root.map.find("settings");
  if (it == m_root.map.end()) return false;
  Node& s = it->second;
  if (!s.isMap()) return false;

  return s.map.erase(key) > 0;
}

// -------------------------
// YAML dump
// -------------------------

static bool looksLikeNumber(const std::string& s) {
  if (s.empty()) return false;
  // Accept leading +/-.
  size_t i = 0;
  if (s[0] == '+' || s[0] == '-') i = 1;
  if (i >= s.size()) return false;

  // Fast reject for obvious non-number.
  bool anyDigit = false;
  bool dot = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c >= '0' && c <= '9') {
      anyDigit = true;
      continue;
    }
    if (c == '.' && !dot) {
      dot = true;
      continue;
    }
    if (c == 'e' || c == 'E') {
      // allow scientific, but keep it simple
      return true;
    }
    return false;
  }
  return anyDigit;
}

static bool looksLikeBool(const std::string& s) {
  if (s.empty()) return false;
  std::string t;
  t.reserve(s.size());
  for (char c : s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return (t == "true" || t == "false" || t == "yes" || t == "no" || t == "on" || t == "off" || t == "0" || t == "1");
}

static bool needsQuotes(const std::string& s) {
  if (s.empty()) return true;

  // Leading/trailing spaces.
  if (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || std::isspace(static_cast<unsigned char>(s.back())))) {
    return true;
  }

  // YAML structural / comment chars.
  for (char c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F) return true;
    if (u >= 0x80) return true; // non-ASCII (IPA) -> quote
    if (c == ':' || c == '#' || c == '\n' || c == '\r' || c == '\t') return true;
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',' ) return true;
  }

  if (s[0] == '-' || s[0] == '?' || s[0] == '!' || s[0] == '*') return true;
  if (s.find("//") != std::string::npos) return true;

  // If it's a clean number/bool, we can keep it unquoted.
  // Otherwise, keep it unquoted as well; this check isn't about typing.
  return false;
}

static std::string quote(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  out.push_back('"');
  return out;
}

static std::string dumpScalar(const std::string& s) {
  // Keep plain numbers/bools unquoted unless quotes are required.
  if (!needsQuotes(s) && (looksLikeNumber(s) || looksLikeBool(s))) {
    return s;
  }
  if (!needsQuotes(s)) {
    return s;
  }
  return quote(s);
}

static std::string dumpKey(const std::string& s) {
  // Keys: be a bit more conservative; quote any non-ASCII.
  if (needsQuotes(s)) return quote(s);
  return s;
}

static void indent(std::string& out, int n) {
  out.append(static_cast<size_t>(n), ' ');
}

static void dumpNode(const Node& node, std::string& out, int ind);

static std::vector<std::string> sortedKeys(const Node& mapNode) {
  std::vector<std::string> keys;
  keys.reserve(mapNode.map.size());
  for (const auto& kv : mapNode.map) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());
  return keys;
}

// Returns a priority for top-level language YAML keys.
// Lower number = comes first. Keys not in the list get a high number (alphabetical after).
static int topLevelKeyPriority(const std::string& key) {
  // Preferred ordering for language YAML files:
  // 1. settings (most important configuration)
  // 2. normalization (IPA rules)
  // 3. transforms
  // 4. intonation
  // 5. toneContours
  // 6. everything else alphabetically
  if (key == "settings") return 0;
  if (key == "normalization") return 1;
  if (key == "transforms") return 2;
  if (key == "intonation") return 3;
  if (key == "toneContours") return 4;
  return 100; // everything else
}

static std::vector<std::string> sortedKeysTopLevel(const Node& mapNode) {
  std::vector<std::string> keys;
  keys.reserve(mapNode.map.size());
  for (const auto& kv : mapNode.map) keys.push_back(kv.first);

  std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
    int pa = topLevelKeyPriority(a);
    int pb = topLevelKeyPriority(b);
    if (pa != pb) return pa < pb;
    return a < b; // alphabetical for same priority
  });
  return keys;
}

static void dumpMap(const Node& node, std::string& out, int ind) {
  // Use special ordering for top-level keys (settings before normalization, etc.)
  auto keys = (ind == 0) ? sortedKeysTopLevel(node) : sortedKeys(node);

  for (const auto& k : keys) {
    const Node& v = node.map.at(k);
    indent(out, ind);
    out += dumpKey(k);

    if (v.type == Node::Type::Scalar) {
      out += ": ";
      out += dumpScalar(v.scalar);
      out += "\n";
      continue;
    }

    // Null / Map / Seq
    out += ":\n";
    dumpNode(v, out, ind + 2);
  }
}

static void dumpSeqItemMapInlineFirstKey(const Node& item, std::string& out, int ind) {
  // Pick a good first key.
  std::string first;
  if (item.map.find("from") != item.map.end()) first = "from";
  else if (item.map.find("key") != item.map.end()) first = "key";
  else {
    auto keys = sortedKeys(item);
    if (!keys.empty()) first = keys[0];
  }

  if (first.empty() || item.map.at(first).type != Node::Type::Scalar) {
    out += "\n";
    dumpMap(item, out, ind + 2);
    return;
  }

  out += " ";
  out += dumpKey(first);
  out += ": ";
  out += dumpScalar(item.map.at(first).scalar);
  out += "\n";

  // Remaining keys.
  auto keys = sortedKeys(item);
  for (const auto& k : keys) {
    if (k == first) continue;
    const Node& v = item.map.at(k);
    indent(out, ind + 2);
    out += dumpKey(k);
    if (v.type == Node::Type::Scalar) {
      out += ": ";
      out += dumpScalar(v.scalar);
      out += "\n";
    } else {
      out += ":\n";
      dumpNode(v, out, ind + 4);
    }
  }
}

static void dumpSeq(const Node& node, std::string& out, int ind) {
  for (const auto& item : node.seq) {
    indent(out, ind);
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
      } else {
        dumpSeqItemMapInlineFirstKey(item, out, ind);
      }
      continue;
    }

    if (item.type == Node::Type::Seq) {
      out += "\n";
      dumpSeq(item, out, ind + 2);
      continue;
    }

    // Null
    out += "\n";
  }
}

static void dumpNode(const Node& node, std::string& out, int ind) {
  switch (node.type) {
    case Node::Type::Map:
      dumpMap(node, out, ind);
      break;
    case Node::Type::Seq:
      dumpSeq(node, out, ind);
      break;
    case Node::Type::Scalar:
      indent(out, ind);
      out += dumpScalar(node.scalar);
      out += "\n";
      break;
    case Node::Type::Null:
    default:
      // nothing
      break;
  }
}

std::string dumpYaml(const Node& root) {
  std::string out;
  // Friendly header.
  out += "# Edited by nvspPhonemeEditor (Win32)\n";
  out += "# Note: YAML comments are not preserved by this editor.\n";
  out += "\n";

  dumpNode(root, out, 0);
  return out;
}

} // namespace nvsp_editor
