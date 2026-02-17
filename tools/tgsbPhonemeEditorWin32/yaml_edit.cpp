/*
TGSpeechBox — YAML round-trip editor for phoneme data.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "yaml_edit.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <sstream>
#include <unordered_set>

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

std::map<std::string, std::string> LanguageYaml::classes() const {
  std::map<std::string, std::string> out;
  const Node* norm = m_root.get("normalization");
  if (!norm || !norm->isMap()) return out;
  const Node* classesNode = norm->get("classes");
  if (!classesNode || !classesNode->isMap()) return out;

  for (const auto& kv : classesNode->map) {
    if (kv.second.isScalar()) {
      out[kv.first] = kv.second.scalar;
    }
  }
  return out;
}

void LanguageYaml::setClasses(const std::map<std::string, std::string>& classes) {
  // Ensure normalization exists
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
  }

  Node* norm = nullptr;
  auto it = m_root.map.find("normalization");
  if (it == m_root.map.end()) {
    Node n;
    n.type = Node::Type::Map;
    mapSet(m_root, "normalization", std::move(n));
    norm = &m_root.map["normalization"];
  } else {
    norm = &it->second;
    if (norm->type != Node::Type::Map) {
      norm->type = Node::Type::Map;
      norm->map.clear();
    }
  }

  // Build the classes node
  Node classesNode;
  classesNode.type = Node::Type::Map;
  for (const auto& kv : classes) {
    Node val;
    val.type = Node::Type::Scalar;
    val.scalar = kv.second;
    mapSet(classesNode, kv.first, std::move(val));
  }
  norm->map["classes"] = std::move(classesNode);
}

// Helper to flatten nested settings into dotted/camelCase keys
// e.g., trajectoryLimit.enabled -> trajectoryLimitEnabled
// e.g., trajectoryLimit.maxHzPerMs.cf2 -> trajectoryLimitMaxHzPerMsCf2
static void flattenSettings(const Node& node, const std::string& prefix,
                            std::vector<std::pair<std::string, std::string>>& out) {
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
    
    if (v.isScalar()) {
      out.emplace_back(flatKey, v.scalar);
    } else if (v.isMap()) {
      // Recurse into nested maps
      flattenSettings(v, flatKey, out);
    } else if (v.isSeq()) {
      // For sequences, join elements with commas (e.g., applyTo: [cf2, cf3] -> "cf2,cf3")
      std::string joined;
      for (size_t i = 0; i < v.seq.size(); ++i) {
        if (v.seq[i].isScalar()) {
          if (!joined.empty()) joined += ",";
          joined += v.seq[i].scalar;
        }
      }
      if (!joined.empty()) {
        out.emplace_back(flatKey, joined);
      }
    }
  }
}

std::vector<std::pair<std::string, std::string>> LanguageYaml::settings() const {
  std::vector<std::pair<std::string, std::string>> out;
  const Node* s = m_root.get("settings");
  if (!s || !s->isMap()) return out;

  flattenSettings(*s, "", out);
  
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

// Map of flattened setting key prefixes to their nested paths.
// This allows us to reconstruct the nested YAML structure from flattened keys.
struct NestedKeyMapping {
  const char* flatPrefix;      // e.g., "trajectoryLimit"
  const char* nestedPath;      // e.g., "trajectoryLimit" (top-level nested map)
  const char* subPath;         // e.g., nullptr or "lateralOnglide" for deeper nesting
};

// Known nested setting prefixes and their structure
static const NestedKeyMapping kNestedMappings[] = {
  // trajectoryLimit settings
  {"trajectoryLimitEnabled", "trajectoryLimit", nullptr},
  {"trajectoryLimitApplyTo", "trajectoryLimit", nullptr},
  {"trajectoryLimitWindowMs", "trajectoryLimit", nullptr},
  {"trajectoryLimitApplyAcrossWordBoundary", "trajectoryLimit", nullptr},
  {"trajectoryLimitLiquidRateScale", "trajectoryLimit", nullptr},
  {"trajectoryLimitMaxHzPerMsCf2", "trajectoryLimit", "maxHzPerMs"},
  {"trajectoryLimitMaxHzPerMsCf3", "trajectoryLimit", "maxHzPerMs"},
  
  // liquidDynamics settings
  {"liquidDynamicsEnabled", "liquidDynamics", nullptr},
  {"liquidDynamicsLateralOnglideF1Delta", "liquidDynamics", "lateralOnglide"},
  {"liquidDynamicsLateralOnglideF2Delta", "liquidDynamics", "lateralOnglide"},
  {"liquidDynamicsLateralOnglideDurationPct", "liquidDynamics", "lateralOnglide"},
  {"liquidDynamicsRhoticF3DipEnabled", "liquidDynamics", "rhoticF3Dip"},
  {"liquidDynamicsRhoticF3Minimum", "liquidDynamics", "rhoticF3Dip"},
  {"liquidDynamicsRhoticF3DipDurationPct", "liquidDynamics", "rhoticF3Dip"},
  {"liquidDynamicsLabialGlideTransitionEnabled", "liquidDynamics", "labialGlideTransition"},
  {"liquidDynamicsLabialGlideStartF1", "liquidDynamics", "labialGlideTransition"},
  {"liquidDynamicsLabialGlideStartF2", "liquidDynamics", "labialGlideTransition"},
  {"liquidDynamicsLabialGlideTransitionPct", "liquidDynamics", "labialGlideTransition"},
  
  // boundarySmoothing settings (place-of-articulation scales)
  {"boundarySmoothingAlveolarF1Scale", "boundarySmoothing", "alveolar"},
  {"boundarySmoothingAlveolarF2Scale", "boundarySmoothing", "alveolar"},
  {"boundarySmoothingAlveolarF3Scale", "boundarySmoothing", "alveolar"},
  {"boundarySmoothingLabialF1Scale", "boundarySmoothing", "labial"},
  {"boundarySmoothingLabialF2Scale", "boundarySmoothing", "labial"},
  {"boundarySmoothingLabialF3Scale", "boundarySmoothing", "labial"},
  {"boundarySmoothingPalatalF1Scale", "boundarySmoothing", "palatal"},
  {"boundarySmoothingPalatalF2Scale", "boundarySmoothing", "palatal"},
  {"boundarySmoothingPalatalF3Scale", "boundarySmoothing", "palatal"},
  {"boundarySmoothingVelarF1Scale", "boundarySmoothing", "velar"},
  {"boundarySmoothingVelarF2Scale", "boundarySmoothing", "velar"},
  {"boundarySmoothingVelarF3Scale", "boundarySmoothing", "velar"},
  {"boundarySmoothingWithinSyllableScale", "boundarySmoothing", nullptr},
  {"boundarySmoothingWithinSyllableFadeScale", "boundarySmoothing", nullptr},

  // boundarySmoothing settings (general)
  {"boundarySmoothingEnabled", "boundarySmoothing", nullptr},
  {"boundarySmoothingF1Scale", "boundarySmoothing", nullptr},
  {"boundarySmoothingF2Scale", "boundarySmoothing", nullptr},
  {"boundarySmoothingF3Scale", "boundarySmoothing", nullptr},
  {"boundarySmoothingPlosiveSpansPhone", "boundarySmoothing", nullptr},
  {"boundarySmoothingNasalF1Instant", "boundarySmoothing", nullptr},
  {"boundarySmoothingNasalF2F3SpansPhone", "boundarySmoothing", nullptr},
  {"boundarySmoothingFricToStopFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingFricToVowelFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingLiquidToStopFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingLiquidToVowelFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingNasalToStopFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingNasalToVowelFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingStopToFricFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingStopToVowelFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingVowelToFricFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingVowelToLiquidFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingVowelToNasalFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingVowelToStopFadeMs", "boundarySmoothing", nullptr},
  {"boundarySmoothingVowelToVowelFadeMs", "boundarySmoothing", nullptr},

  // clusterTiming settings
  {"clusterTimingEnabled", "clusterTiming", nullptr},
  {"clusterTimingFricBeforeStopScale", "clusterTiming", nullptr},
  {"clusterTimingStopBeforeFricScale", "clusterTiming", nullptr},
  {"clusterTimingFricBeforeFricScale", "clusterTiming", nullptr},
  {"clusterTimingStopBeforeStopScale", "clusterTiming", nullptr},
  {"clusterTimingTripleClusterMiddleScale", "clusterTiming", nullptr},
  {"clusterTimingAffricateInClusterScale", "clusterTiming", nullptr},
  {"clusterTimingWordMedialConsonantScale", "clusterTiming", nullptr},
  {"clusterTimingWordFinalObstruentScale", "clusterTiming", nullptr},

  // lengthContrast settings
  // Note: nested keys are "shortVowelCeiling" / "longVowelFloor" (no Ms suffix),
  // while flat keys in pack.cpp use "lengthContrastShortVowelCeilingMs". The
  // flat key here omits "Ms" so extractLeafKey produces the correct nested key.
  {"lengthContrastEnabled", "lengthContrast", nullptr},
  {"lengthContrastShortVowelCeiling", "lengthContrast", nullptr},
  {"lengthContrastLongVowelFloor", "lengthContrast", nullptr},
  {"lengthContrastGeminateClosureScale", "lengthContrast", nullptr},
  {"lengthContrastGeminateReleaseScale", "lengthContrast", nullptr},
  {"lengthContrastPreGeminateVowelScale", "lengthContrast", nullptr},

  // clusterBlend settings
  {"clusterBlendEnabled", "clusterBlend", nullptr},
  {"clusterBlendStrength", "clusterBlend", nullptr},
  {"clusterBlendNasalToStopScale", "clusterBlend", nullptr},
  {"clusterBlendFricToStopScale", "clusterBlend", nullptr},
  {"clusterBlendStopToFricScale", "clusterBlend", nullptr},
  {"clusterBlendNasalToFricScale", "clusterBlend", nullptr},
  {"clusterBlendLiquidToStopScale", "clusterBlend", nullptr},
  {"clusterBlendLiquidToFricScale", "clusterBlend", nullptr},
  {"clusterBlendFricToFricScale", "clusterBlend", nullptr},
  {"clusterBlendStopToStopScale", "clusterBlend", nullptr},
  {"clusterBlendDefaultPairScale", "clusterBlend", nullptr},
  {"clusterBlendHomorganicScale", "clusterBlend", nullptr},
  {"clusterBlendWordBoundaryScale", "clusterBlend", nullptr},
  {"clusterBlendF1Scale", "clusterBlend", nullptr},
  {"clusterBlendForwardDriftStrength", "clusterBlend", nullptr},

  // prominence settings
  {"prominenceEnabled", "prominence", nullptr},
  {"prominencePrimaryStressWeight", "prominence", nullptr},
  {"prominenceSecondaryStressWeight", "prominence", nullptr},
  {"prominenceSecondaryStressLevel", "prominence", nullptr},
  {"prominenceLongVowelWeight", "prominence", nullptr},
  {"prominenceLongVowelMode", "prominence", nullptr},
  {"prominenceWordInitialBoost", "prominence", nullptr},
  {"prominenceWordFinalReduction", "prominence", nullptr},
  {"prominenceDurationProminentFloorMs", "prominence", nullptr},
  {"prominenceDurationReducedCeiling", "prominence", nullptr},
  {"prominenceAmplitudeBoostDb", "prominence", nullptr},
  {"prominenceAmplitudeReductionDb", "prominence", nullptr},
  {"prominencePitchFromProminence", "prominence", nullptr},

  // rateCompensation settings
  {"rateCompEnabled", "rateCompensation", nullptr},
  {"rateCompVowelFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompFricativeFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompStopFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompNasalFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompLiquidFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompAffricateFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompSemivowelFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompTapFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompTrillFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompVoicedConsonantFloorMs", "rateCompensation", "minimumDurations"},
  {"rateCompWordFinalBonusMs", "rateCompensation", nullptr},
  {"rateCompFloorSpeedScale", "rateCompensation", nullptr},
  {"rateCompClusterProportionGuard", "rateCompensation", nullptr},
  {"rateCompClusterMaxRatioShift", "rateCompensation", nullptr},
  {"rateCompSchwaReductionEnabled", "rateCompensation", "schwaReduction"},
  {"rateCompSchwaThreshold", "rateCompensation", "schwaReduction"},
  {"rateCompSchwaScale", "rateCompensation", "schwaReduction"},

  // syllableDuration settings
  {"syllableDurationEnabled", "syllableDuration", nullptr},
  {"syllableDurationOnsetScale", "syllableDuration", nullptr},
  {"syllableDurationCodaScale", "syllableDuration", nullptr},
  {"syllableDurationUnstressedOpenNucleusScale", "syllableDuration", nullptr},

  // allophoneRules settings (scalar fields only — rules array handled separately)
  {"allophoneRulesEnabled", "allophoneRules", nullptr},

  // specialCoarticulation settings (scalar fields only — rules array handled separately)
  {"specialCoarticulationEnabled", "specialCoarticulation", nullptr},
  {"specialCoarticMaxDeltaHz", "specialCoarticulation", nullptr},
};

// Extract the leaf key name from a flattened key given the prefix info
// e.g., "trajectoryLimitEnabled" with prefix "trajectoryLimit" -> "enabled"
// e.g., "trajectoryLimitMaxHzPerMsCf2" with prefix "trajectoryLimit" and subPath "maxHzPerMs" -> "cf2"
static std::string extractLeafKey(const std::string& flatKey, const char* nestedPath, const char* subPath) {
  std::string prefix = nestedPath;
  if (subPath) {
    // Capitalize first letter of subPath for camelCase
    prefix += static_cast<char>(std::toupper(static_cast<unsigned char>(subPath[0])));
    prefix += (subPath + 1);
  }
  
  if (flatKey.size() <= prefix.size()) return flatKey;
  
  // The leaf key starts after the prefix, with first letter lowercased
  std::string leaf = flatKey.substr(prefix.size());
  if (!leaf.empty()) {
    leaf[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(leaf[0])));
  }
  return leaf;
}

// Check if a value looks like a comma-separated list (for sequences like applyTo)
static bool looksLikeList(const std::string& value) {
  return value.find(',') != std::string::npos;
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

void LanguageYaml::setSettings(const std::vector<std::pair<std::string, std::string>>& settings) {
  // Ensure root is a map.
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }

  Node* s = getNestedMap(m_root, "settings");

  // Preserve complex sub-trees that contain sequence-of-maps data
  // (the flatten/unflatten cycle would lose these).
  Node savedAllophoneRules, savedSpecialCoartic;
  if (const Node* ar = s->get("allophoneRules")) {
    if (ar->isMap() && ar->get("rules")) savedAllophoneRules = *ar;
  }
  if (const Node* sc = s->get("specialCoarticulation")) {
    if (sc->isMap() && sc->get("rules")) savedSpecialCoartic = *sc;
  }

  s->map.clear();
  s->keyOrder.clear();
  s->seq.clear();
  s->scalar.clear();

  for (const auto& kv : settings) {
    if (kv.first.empty()) continue;
    
    // Check if this is a known nested key
    bool handled = false;
    for (const auto& mapping : kNestedMappings) {
      if (kv.first == mapping.flatPrefix) {
        // This is an exact match to a known nested key pattern
        std::string leafKey = extractLeafKey(kv.first, mapping.nestedPath, mapping.subPath);
        
        // Ensure the top-level nested map exists
        if (s->map.find(mapping.nestedPath) == s->map.end()) {
          Node nm;
          nm.type = Node::Type::Map;
          mapSet(*s, mapping.nestedPath, std::move(nm));
        }
        Node* target = &s->map[mapping.nestedPath];

        // If there's a subPath, ensure that nested map exists too
        if (mapping.subPath) {
          if (target->map.find(mapping.subPath) == target->map.end()) {
            Node nm;
            nm.type = Node::Type::Map;
            mapSet(*target, mapping.subPath, std::move(nm));
          }
          target = &target->map[mapping.subPath];
        }

        // Set the leaf value
        Node v;
        // Check if this should be a sequence (like applyTo)
        if (looksLikeList(kv.second) && leafKey == "applyTo") {
          v = makeSequenceFromCommaSeparated(kv.second);
        } else {
          v.type = Node::Type::Scalar;
          v.scalar = kv.second;
        }
        mapSet(*target, leafKey, std::move(v));
        
        handled = true;
        break;
      }
    }
    
    if (!handled) {
      // Regular flat setting
      Node v;
      v.type = Node::Type::Scalar;
      v.scalar = kv.second;
      mapSet(*s, kv.first, std::move(v));
    }
  }

  // Restore complex sub-trees, merging with any scalar keys the loop just wrote.
  if (!savedAllophoneRules.map.empty()) {
    Node& ar = s->map["allophoneRules"];
    // The loop may have written "enabled" into this map via kNestedMappings.
    // Merge the saved "rules" key back in.
    if (const Node* rules = savedAllophoneRules.get("rules")) {
      mapSet(ar, "rules", *rules);
    }
  }
  if (!savedSpecialCoartic.map.empty()) {
    Node& sc = s->map["specialCoarticulation"];
    if (const Node* rules = savedSpecialCoartic.get("rules")) {
      mapSet(sc, "rules", *rules);
    }
  }
}

void LanguageYaml::setSetting(const std::string& key, const std::string& value) {
  if (key.empty()) return;
  if (m_root.type != Node::Type::Map) {
    m_root.type = Node::Type::Map;
    m_root.map.clear();
    m_root.keyOrder.clear();
    m_root.seq.clear();
    m_root.scalar.clear();
  }
  Node* s = getNestedMap(m_root, "settings");
  
  // Check if this is a known nested key
  for (const auto& mapping : kNestedMappings) {
    if (key == mapping.flatPrefix) {
      std::string leafKey = extractLeafKey(key, mapping.nestedPath, mapping.subPath);
      
      // Ensure the top-level nested map exists
      if (s->map.find(mapping.nestedPath) == s->map.end()) {
        s->map[mapping.nestedPath].type = Node::Type::Map;
      }
      Node* target = &s->map[mapping.nestedPath];
      
      // If there's a subPath, ensure that nested map exists too
      if (mapping.subPath) {
        if (target->map.find(mapping.subPath) == target->map.end()) {
          target->map[mapping.subPath].type = Node::Type::Map;
        }
        target = &target->map[mapping.subPath];
      }
      
      // Set the leaf value
      Node v;
      if (looksLikeList(value) && leafKey == "applyTo") {
        v = makeSequenceFromCommaSeparated(value);
      } else {
        v.type = Node::Type::Scalar;
        v.scalar = value;
      }
      target->map[leafKey] = std::move(v);
      return;
    }
  }
  
  // Regular flat setting
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

  // Check if this is a known nested key
  for (const auto& mapping : kNestedMappings) {
    if (key == mapping.flatPrefix) {
      std::string leafKey = extractLeafKey(key, mapping.nestedPath, mapping.subPath);
      
      auto topIt = s.map.find(mapping.nestedPath);
      if (topIt == s.map.end()) return false;
      
      Node* target = &topIt->second;
      if (mapping.subPath) {
        auto subIt = target->map.find(mapping.subPath);
        if (subIt == target->map.end()) return false;
        target = &subIt->second;
      }
      
      return target->map.erase(leafKey) > 0;
    }
  }

  return s.map.erase(key) > 0;
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
    if (const Node* n = item.get("stress")) if (n->isScalar()) r.stress = n->scalar;
    r.after = readStringSeq(item.get("after"));
    r.before = readStringSeq(item.get("before"));
    r.afterFlags = readStringSeq(item.get("afterFlags"));
    r.notAfterFlags = readStringSeq(item.get("notAfterFlags"));
    r.beforeFlags = readStringSeq(item.get("beforeFlags"));
    r.notBeforeFlags = readStringSeq(item.get("notBeforeFlags"));
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
    if (r.stress != "any") mapSet(item, "stress", makeScalar(r.stress));
    if (!r.after.empty()) mapSet(item, "after", makeStringSeqNode(r.after));
    if (!r.before.empty()) mapSet(item, "before", makeStringSeqNode(r.before));
    if (!r.afterFlags.empty()) mapSet(item, "afterFlags", makeStringSeqNode(r.afterFlags));
    if (!r.notAfterFlags.empty()) mapSet(item, "notAfterFlags", makeStringSeqNode(r.notAfterFlags));
    if (!r.beforeFlags.empty()) mapSet(item, "beforeFlags", makeStringSeqNode(r.beforeFlags));
    if (!r.notBeforeFlags.empty()) mapSet(item, "notBeforeFlags", makeStringSeqNode(r.notBeforeFlags));
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
    if (r.vowelFilter != "all") mapSet(item, "vowelFilter", makeScalar(r.vowelFilter));
    if (r.formant != "f2") mapSet(item, "formant", makeScalar(r.formant));
    if (r.deltaHz != 0.0) mapSet(item, "deltaHz", makeScalarD(r.deltaHz));
    if (r.side != "both") mapSet(item, "side", makeScalar(r.side));
    if (r.cumulative) mapSet(item, "cumulative", makeScalar("true"));
    if (r.unstressedScale != 1.0) mapSet(item, "unstressedScale", makeScalarD(r.unstressedScale));
    if (r.phraseFinalStressedScale != 1.0) mapSet(item, "phraseFinalStressedScale", makeScalarD(r.phraseFinalStressedScale));
    rulesSeq.seq.push_back(std::move(item));
  }

  sc->map["rules"] = std::move(rulesSeq);
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

// Return insertion-order keys if available, else fall back to sorted.
static std::vector<std::string> orderedKeys(const Node& mapNode) {
  if (!mapNode.keyOrder.empty()) {
    // keyOrder may be stale if keys were added programmatically without
    // updating it.  Build a set of what's in keyOrder, then append any
    // missing map keys at the end (sorted) to be safe.
    std::vector<std::string> result;
    result.reserve(mapNode.map.size());
    std::unordered_set<std::string> seen;
    for (const auto& k : mapNode.keyOrder) {
      if (mapNode.map.find(k) != mapNode.map.end() && seen.insert(k).second) {
        result.push_back(k);
      }
    }
    if (result.size() < mapNode.map.size()) {
      // Some keys were added without keyOrder.  Append them sorted.
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

// Emit a map in flow style: {key: val, key: val}
static void dumpFlowMap(const Node& node, std::string& out) {
  out += "{";
  auto keys = orderedKeys(node);
  bool first = true;
  for (const auto& k : keys) {
    if (!first) out += ", ";
    first = false;
    out += dumpKey(k);
    out += ": ";
    const Node& v = node.map.at(k);
    if (v.isScalar()) {
      out += dumpScalar(v.scalar);
    } else if (v.isSeq() && v.flowStyle) {
      // Nested flow seq inside flow map.
      out += "[";
      bool firstItem = true;
      for (const auto& item : v.seq) {
        if (!firstItem) out += ", ";
        firstItem = false;
        out += dumpScalar(item.scalar);
      }
      out += "]";
    } else {
      // Shouldn't happen in well-formed flow maps, but be safe.
      out += dumpScalar(v.isScalar() ? v.scalar : "");
    }
  }
  out += "}";
}

// Emit a seq in flow style: [val, val, val]
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

static void dumpMap(const Node& node, std::string& out, int ind) {
  // Use special ordering for top-level keys (settings before normalization, etc.)
  // Preserve insertion order for non-top-level maps.
  auto keys = (ind == 0) ? sortedKeysTopLevel(node) : orderedKeys(node);

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

    // Flow-style map: emit inline {k: v, ...}
    if (v.isMap() && v.flowStyle) {
      out += ": ";
      dumpFlowMap(v, out);
      out += "\n";
      continue;
    }

    // Flow-style seq: emit inline [v, v, ...]
    if (v.isSeq() && v.flowStyle) {
      out += ": ";
      dumpFlowSeq(v, out);
      out += "\n";
      continue;
    }

    // Null / Map / Seq (block style)
    out += ":\n";
    dumpNode(v, out, ind + 2);
  }
}

static void dumpSeqItemMapInlineFirstKey(const Node& item, std::string& out, int ind) {
  // Pick a good first key: prefer insertion order, else "from"/"key".
  auto keys = orderedKeys(item);
  std::string first;
  if (!keys.empty()) {
    // Use insertion-order first key if it's a scalar.
    if (item.map.at(keys[0]).type == Node::Type::Scalar) {
      first = keys[0];
    }
  }
  // Fallback: prefer "from" or "key" for readability.
  if (first.empty()) {
    if (item.map.find("from") != item.map.end()) first = "from";
    else if (item.map.find("key") != item.map.end()) first = "key";
    else if (!keys.empty()) first = keys[0];
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

  // Remaining keys in insertion order.
  for (const auto& k : keys) {
    if (k == first) continue;
    const Node& v = item.map.at(k);
    indent(out, ind + 2);
    out += dumpKey(k);
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
      } else if (item.flowStyle) {
        // Emit as inline flow map: - {key: val, key: val}
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
  out += "# Edited by tgsbPhonemeEditor (Win32)\n";
  out += "\n";

  dumpNode(root, out, 0);
  return out;
}

// Serialize a single phoneme node as YAML lines at 4-space indent (under "phonemes:\n  key:\n").
static std::string dumpSinglePhoneme(const std::string& key, const Node& node) {
  std::string out;
  // Phoneme key line at 2-space indent.
  out += "  ";
  out += dumpKey(key);
  out += ":\n";
  // Fields at 4-space indent.
  if (node.isMap()) {
    auto keys = orderedKeys(node);
    for (const auto& k : keys) {
      const Node& v = node.map.at(k);
      out += "    ";
      out += dumpKey(k);
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

// Find the line range [start, end) for a phoneme key within the phonemes: block.
// The phoneme key is at 2-space indent, its fields at 4+ spaces.
// Returns false if not found.
struct PhonemeLineRange {
  std::string key;         // unquoted phoneme key
  size_t startLine = 0;    // index of the "  key:" line
  size_t endLine = 0;      // one past the last line of this phoneme's fields
};

// Parse the original file to find all phoneme blocks and their line ranges.
static void findPhonemeRanges(const std::vector<std::string>& lines,
                              std::vector<PhonemeLineRange>& ranges,
                              size_t& phonemesBlockStart,
                              size_t& phonemesBlockEnd) {
  phonemesBlockStart = 0;
  phonemesBlockEnd = lines.size();

  // Find "phonemes:" top-level key.
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

  // Walk lines after "phonemes:" looking for 2-space-indented keys (phoneme names).
  PhonemeLineRange cur;
  bool inPhoneme = false;
  for (size_t i = pStart + 1; i < lines.size(); ++i) {
    const std::string& ln = lines[i];

    // A line with no leading space = new top-level key -> end of phonemes block.
    if (!ln.empty() && ln[0] != ' ' && ln[0] != '#') {
      phonemesBlockEnd = i;
      if (inPhoneme) {
        cur.endLine = i;
        ranges.push_back(cur);
      }
      return;
    }

    // Check for 2-space-indented key (phoneme name).
    if (ln.size() >= 3 && ln[0] == ' ' && ln[1] == ' ' && ln[2] != ' ' && ln[2] != '#') {
      // This is a phoneme key line like "  ɑ:" or "  \"ɑ\":"
      if (inPhoneme) {
        cur.endLine = i;
        ranges.push_back(cur);
      }
      // Parse the key (strip quotes, strip trailing ':').
      std::string rawKey = ln.substr(2);
      // Trim trailing whitespace.
      while (!rawKey.empty() && (rawKey.back() == ' ' || rawKey.back() == '\r')) rawKey.pop_back();
      // Strip trailing colon.
      if (!rawKey.empty() && rawKey.back() == ':') rawKey.pop_back();
      // Unquote.
      if (rawKey.size() >= 2 && rawKey.front() == '"' && rawKey.back() == '"') {
        rawKey = rawKey.substr(1, rawKey.size() - 2);
      }

      cur = PhonemeLineRange();
      cur.key = rawKey;
      cur.startLine = i;
      inPhoneme = true;
    }
  }
  // End of file.
  phonemesBlockEnd = lines.size();
  if (inPhoneme) {
    cur.endLine = lines.size();
    ranges.push_back(cur);
  }
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
  std::vector<PhonemeLineRange> ranges;
  size_t phonemesBlockStart = 0;
  size_t phonemesBlockEnd = origLines.size();
  findPhonemeRanges(origLines, ranges, phonemesBlockStart, phonemesBlockEnd);

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
    std::string serialized = dumpSinglePhoneme(pkey, memNode);

    auto origIt = origKeyToRange.find(pkey);
    if (origIt != origKeyToRange.end()) {
      // This phoneme existed in the original file.
      const PhonemeLineRange& range = ranges[origIt->second];
      // Reconstruct original lines for comparison.
      std::string origBlock;
      for (size_t i = range.startLine; i < range.endLine; ++i) {
        // Skip pure comment/blank lines for comparison purposes,
        // but include them in the original block output.
        origBlock += origLines[i];
        origBlock += "\n";
      }

      // Compare stripped versions (ignore trailing whitespace differences).
      // If the data is the same, keep original (preserves comments, spacing).
      // Simple comparison: strip all lines and compare key-value content.
      bool changed = false;

      // Quick comparison: serialize from memory and compare text.
      // This won't match if original had comments or different formatting,
      // so we do a data-level comparison instead.
      // Parse both blocks' key-value data and compare.
      // Simplest approach: if the serialized phoneme differs from the
      // stripped original, it was modified.
      // For robustness, compare the in-memory Node to a freshly-parsed
      // version of the original block.
      // But that's complex.  Instead, use a simpler heuristic:
      // serialize the in-memory node and compare to original text
      // with comments stripped.  If they match, keep original.

      // Strip comments and blank lines from original for comparison.
      std::string origStripped;
      for (size_t i = range.startLine; i < range.endLine; ++i) {
        const std::string& ln = origLines[i];
        std::string trimmed = ln;
        // Trim trailing whitespace.
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\r')) trimmed.pop_back();
        // Skip pure comment lines and blank lines.
        size_t firstNonSpace = trimmed.find_first_not_of(' ');
        if (firstNonSpace == std::string::npos) continue;
        if (trimmed[firstNonSpace] == '#') continue;
        // Strip inline comments for comparison.
        origStripped += trimmed;
        origStripped += "\n";
      }

      // Strip trailing whitespace from serialized too.
      std::string serStripped;
      for (size_t i = 0; i < serialized.size(); ++i) {
        serStripped += serialized[i];
      }
      // Normalize: remove trailing newline for comparison.
      while (!origStripped.empty() && origStripped.back() == '\n') origStripped.pop_back();
      while (!serStripped.empty() && serStripped.back() == '\n') serStripped.pop_back();

      changed = (origStripped != serStripped);

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
