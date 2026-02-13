/*
TGSpeechBox — Minimal YAML parser implementation.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "yaml_min.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <sstream>

namespace nvsp_frontend::yaml_min {

static std::string ltrim(std::string s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  s.erase(0, i);
  return s;
}

static std::string rtrim(std::string s) {
  while (!s.empty()) {
    char c = s.back();
    if (c == ' ' || c == '\t' || c == '\r') {
      s.pop_back();
      continue;
    }
    break;
  }
  return s;
}

static std::string trim(std::string s) {
  return rtrim(ltrim(std::move(s)));
}

static bool isIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/';
}

static std::string unquoteScalar(const std::string& s) {
  if (s.size() >= 2) {
    char q = s.front();
    if ((q == '"' || q == '\'') && s.back() == q) {
      std::string out;
      out.reserve(s.size() - 2);
      for (size_t i = 1; i + 1 < s.size(); ++i) {
        char c = s[i];
        if (q == '"' && c == '\\' && i + 1 < s.size() - 1) {
          char n = s[i + 1];
          switch (n) {
            case '"': out.push_back('"'); ++i; continue;
            case '\\': out.push_back('\\'); ++i; continue;
            case 'n': out.push_back('\n'); ++i; continue;
            case 't': out.push_back('\t'); ++i; continue;
            case 'r': out.push_back('\r'); ++i; continue;
            default: break;
          }
        }
        out.push_back(c);
      }
      return out;
    }
  }
  return s;
}

static std::string stripInlineComment(const std::string& s) {
  // Remove a trailing " # comment" unless the '#' is inside quotes.
  bool inSingle = false;
  bool inDouble = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\'' && !inDouble) {
      inSingle = !inSingle;
    } else if (c == '"' && !inSingle) {
      inDouble = !inDouble;
    } else if (c == '#' && !inSingle && !inDouble) {
      // If '#' is first non-space char, caller will treat as comment-line.
      return rtrim(s.substr(0, i));
    }
  }
  return rtrim(s);
}

struct Line {
  int lineNo = 0;      // 1-based
  int indent = 0;      // spaces
  std::string text;    // trimmed (no leading spaces), no trailing comment
};

static bool readLines(const std::string& path, std::vector<Line>& out, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "Could not open file";
    return false;
  }

  std::string raw;
  int lineNo = 0;
  while (std::getline(f, raw)) {
    ++lineNo;
    // Strip UTF-8 BOM at start of file.
    if (lineNo == 1 && raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
      raw.erase(0, 3);
    }

    // Count leading spaces.
    int indent = 0;
    while (indent < static_cast<int>(raw.size()) && raw[indent] == ' ') {
      ++indent;
    }

    std::string t = raw.substr(static_cast<size_t>(indent));
    t = rtrim(t);

    // Skip empty lines.
    if (t.empty()) continue;

    // Skip full-line comments.
    std::string tNoLead = ltrim(t);
    if (!tNoLead.empty() && tNoLead[0] == '#') continue;

    t = stripInlineComment(t);
    if (t.empty()) continue;

    out.push_back(Line{lineNo, indent, t});
  }

  return true;
}

static bool parseScalar(const std::string& raw, Node& out) {
  out = Node{};
  out.type = Node::Type::Scalar;
  out.scalar = unquoteScalar(trim(raw));
  return true;
}

static bool parseInlineSeq(const std::string& raw, Node& out) {
  std::string s = trim(raw);
  if (s.size() < 2 || s.front() != '[' || s.back() != ']') return false;
  Node n;
  n.type = Node::Type::Seq;
  std::string inner = trim(s.substr(1, s.size() - 2));
  if (inner.empty()) {
    out = std::move(n);
    return true;
  }

  // Very small CSV-ish split that respects quotes.
  bool inSingle = false;
  bool inDouble = false;
  std::string cur;
  for (size_t i = 0; i < inner.size(); ++i) {
    char c = inner[i];
    if (c == '\'' && !inDouble) inSingle = !inSingle;
    if (c == '"' && !inSingle) inDouble = !inDouble;

    if (c == ',' && !inSingle && !inDouble) {
      Node item;
      parseScalar(cur, item);
      n.seq.push_back(std::move(item));
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) {
    Node item;
    parseScalar(cur, item);
    n.seq.push_back(std::move(item));
  }

  out = std::move(n);
  return true;
}

static bool splitKeyValue(const std::string& s, std::string& outKey, std::string& outVal, bool& hasVal) {
  // Find first ':' not inside quotes.
  bool inSingle = false;
  bool inDouble = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\'' && !inDouble) inSingle = !inSingle;
    if (c == '"' && !inSingle) inDouble = !inDouble;
    if (c == ':' && !inSingle && !inDouble) {
      outKey = trim(s.substr(0, i));
      outVal = trim(s.substr(i + 1));
      hasVal = !outVal.empty();
      outKey = unquoteScalar(outKey);
      return !outKey.empty();
    }
  }
  return false;
}

static bool parseBlock(const std::vector<Line>& lines, size_t& idx, int indent, Node& out, std::string& err);

static bool parseMap(const std::vector<Line>& lines, size_t& idx, int indent, Node& out, std::string& err) {
  Node n;
  n.type = Node::Type::Map;

  while (idx < lines.size()) {
    const Line& ln = lines[idx];
    if (ln.indent < indent) break;
    if (ln.indent > indent) {
      err = "Unexpected indentation";
      return false;
    }

    std::string key;
    std::string val;
    bool hasVal = false;
    if (!splitKeyValue(ln.text, key, val, hasVal)) {
      err = "Expected 'key: value'";
      return false;
    }

    Node valueNode;
    if (!hasVal) {
      // Nested block.
      ++idx;
      if (idx >= lines.size() || lines[idx].indent <= indent) {
        // Empty map value.
        valueNode.type = Node::Type::Null;
      } else {
        if (!parseBlock(lines, idx, lines[idx].indent, valueNode, err)) {
          // parseBlock sets err.
          return false;
        }
      }
    } else {
      // Scalar or inline list.
      Node tmp;
      if (parseInlineSeq(val, tmp)) {
        valueNode = std::move(tmp);
      } else {
        parseScalar(val, valueNode);
      }
      ++idx;
    }

    n.map[std::move(key)] = std::move(valueNode);
  }

  out = std::move(n);
  return true;
}

static bool parseSeqItemInlineMap(const std::string& s, Node& outMapNode) {
  // Handle a very small subset: "key: value" after a dash.
  std::string key, val;
  bool hasVal = false;
  if (!splitKeyValue(s, key, val, hasVal)) return false;
  if (!hasVal) return false;

  Node n;
  n.type = Node::Type::Map;
  Node v;
  Node tmp;
  if (parseInlineSeq(val, tmp)) {
    v = std::move(tmp);
  } else {
    parseScalar(val, v);
  }
  n.map[std::move(key)] = std::move(v);
  outMapNode = std::move(n);
  return true;
}

static bool parseSeq(const std::vector<Line>& lines, size_t& idx, int indent, Node& out, std::string& err) {
  Node n;
  n.type = Node::Type::Seq;

  while (idx < lines.size()) {
    const Line& ln = lines[idx];
    if (ln.indent < indent) break;
    if (ln.indent != indent) {
      err = "Unexpected indentation in sequence";
      return false;
    }
    const std::string& t = ln.text;
    if (t.size() < 1 || t[0] != '-') {
      // Sequence ended; caller will treat as map key or sibling.
      break;
    }
    std::string after = trim(t.substr(1));
    if (!after.empty() && after[0] == ' ') after = trim(after.substr(1));

    Node item;
    // If it's "- key: value", parse as an inline map.
    if (parseSeqItemInlineMap(after, item)) {
      ++idx;
      // Merge nested lines into the same map item.
      if (idx < lines.size() && lines[idx].indent > indent) {
        Node nested;
        if (!parseBlock(lines, idx, lines[idx].indent, nested, err)) return false;
        if (nested.isMap()) {
          for (auto& kv : nested.map) {
            item.map[kv.first] = std::move(kv.second);
          }
        }
      }
      n.seq.push_back(std::move(item));
      continue;
    }

    if (after.empty()) {
      // Pure nested item.
      ++idx;
      if (idx >= lines.size() || lines[idx].indent <= indent) {
        item.type = Node::Type::Null;
      } else {
        if (!parseBlock(lines, idx, lines[idx].indent, item, err)) return false;
      }
      n.seq.push_back(std::move(item));
      continue;
    }

    // Scalar or inline list.
    Node tmp;
    if (parseInlineSeq(after, tmp)) {
      item = std::move(tmp);
    } else {
      parseScalar(after, item);
    }
    ++idx;
    n.seq.push_back(std::move(item));
  }

  out = std::move(n);
  return true;
}

static bool parseBlock(const std::vector<Line>& lines, size_t& idx, int indent, Node& out, std::string& err) {
  if (idx >= lines.size()) {
    out.type = Node::Type::Null;
    return true;
  }

  // Determine map vs seq based on first line at this indent.
  const Line& ln = lines[idx];
  if (ln.indent != indent) {
    err = "Indent mismatch";
    return false;
  }

  if (!ln.text.empty() && ln.text[0] == '-') {
    return parseSeq(lines, idx, indent, out, err);
  }
  return parseMap(lines, idx, indent, out, err);
}

bool Node::asBool(bool& out) const {
  if (!isScalar()) return false;
  std::string s;
  s.reserve(scalar.size());
  for (char c : scalar) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (s == "true" || s == "yes" || s == "on" || s == "1") { out = true; return true; }
  if (s == "false" || s == "no" || s == "off" || s == "0") { out = false; return true; }
  return false;
}

bool Node::asNumber(double& out) const {
  if (!isScalar()) return false;

  // Locale-independent number parsing.
  // NVDA may set the process numeric locale to one that uses ',' as a decimal
  // separator (Hungarian, Polish, Spanish, etc.). YAML requires '.' decimals.
  // Using strtod/atof would respect the process locale and mis-parse values
  // like '0.6' as '0', effectively zeroing voicing and causing "whisper".
  std::istringstream iss(scalar);
  iss.imbue(std::locale::classic());

  iss >> std::ws;
  double v = 0.0;
  iss >> v;
  if (!iss) return false;

  iss >> std::ws;
  if (!iss.eof()) return false;

  out = v;
  return true;
}

std::string Node::asString(const std::string& fallback) const {
  if (!isScalar()) return fallback;
  return scalar;
}

const Node* Node::get(std::string_view key) const {
  if (!isMap()) return nullptr;
  auto it = map.find(std::string(key));
  if (it == map.end()) return nullptr;
  return &it->second;
}

bool loadFile(const std::string& path, Node& outRoot, std::string& outError) {
  std::vector<Line> lines;
  std::string err;
  if (!readLines(path, lines, err)) {
    outError = err + ": " + path;
    return false;
  }

  outRoot = Node{};
  if (lines.empty()) {
    outRoot.type = Node::Type::Map;
    return true;
  }

  size_t idx = 0;
  std::string parseErr;
  if (!parseBlock(lines, idx, lines[0].indent, outRoot, parseErr)) {
    int lineNo = (idx < lines.size()) ? lines[idx].lineNo : lines.back().lineNo;
    std::ostringstream oss;
    oss << path << ":" << lineNo << ": " << parseErr;
    outError = oss.str();
    return false;
  }

  return true;
}

} // namespace nvsp_frontend::yaml_min
