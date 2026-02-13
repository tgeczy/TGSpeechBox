/*
TGSpeechBox — Minimal YAML parser interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_YAML_MIN_H
#define TGSB_FRONTEND_YAML_MIN_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nvsp_frontend::yaml_min {

struct Node {
  enum class Type {
    Null,
    Scalar,
    Map,
    Seq,
  };

  Type type = Type::Null;
  // For scalars, we keep the raw text without quotes.
  std::string scalar;

  std::unordered_map<std::string, Node> map;
  std::vector<Node> seq;

  bool isScalar() const { return type == Type::Scalar; }
  bool isMap() const { return type == Type::Map; }
  bool isSeq() const { return type == Type::Seq; }

  // Typed scalar helpers. Return true on success.
  bool asBool(bool& out) const;
  bool asNumber(double& out) const;
  std::string asString(const std::string& fallback = "") const;

  const Node* get(std::string_view key) const;
};

// Parse a YAML file using a small, indentation-based subset.
// Supported:
// - maps (key: value)
// - sequences (- item)
// - nested blocks by indentation
// - scalar strings, bools, numbers
// - comments (# ...) on their own line or after a scalar

// Returns true on success. On failure, outError contains a message with a 1-based line number.
bool loadFile(const std::string& path, Node& outRoot, std::string& outError);

} // namespace nvsp_frontend::yaml_min

#endif
