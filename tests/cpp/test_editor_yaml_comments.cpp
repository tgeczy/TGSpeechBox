// A YAML save keeps the text of what was not changed (#217).
//
// The phoneme editor's saves and the frontend's exportMergedYaml (which the
// phone apps' "export my changes" uses) rewrite only the phoneme, or the
// top-level section, that changed; everything else is copied from the file
// as it is, comments included.  "Changed" was decided by writing the block
// out again and comparing text, so a block spelled differently from the
// writer's own spelling counted as changed: a negative number (the writer
// quoted "-0.15"), a quoted key ("c_iv"), a quoted word in a language's
// settings.  A save with no edits rewrote k, g, k_ptbr, d͡ʒ, t͡ʃ and c_iv and
// dropped their comments, the Chodroff & Wilson citation on k among them;
// in a language file it dropped every comment in `settings:`.
//
// What is checked here, on copies of the shipped files: a save with no edits
// is the same text; an edit rewrites its own phoneme or section and no
// other; a negative number is written back as a number.

#include "doctest.h"

#include "editor_test_util.h"
#include "yaml_edit.h"
#include "yaml_export.h"

#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ye = nvsp_frontend::yaml_export;
using namespace tgsb_test;
using tgsb_editor::LanguageYaml;
using tgsb_editor::PhonemesYaml;

namespace {

fs::path copyPhonemes(const fs::path& dir) {
  const fs::path dst = dir / "packs" / "phonemes.yaml";
  fs::create_directories(dst.parent_path());
  fs::copy_file(repoRoot() / "packs" / "phonemes.yaml", dst, fs::copy_options::overwrite_existing);
  return dst;
}

// The lines of one phoneme's block in a phonemes.yaml text, and what comes
// before and after it.
struct Split {
  std::vector<std::string> before, block, after;
};

Split splitAtPhoneme(const std::string& text, const std::string& key) {
  const std::vector<std::string> lines = splitLines(text);
  std::vector<ye::PhonemeLineRange> ranges;
  size_t blockStart = 0, blockEnd = 0;
  ye::findPhonemeRanges(lines, ranges, blockStart, blockEnd);
  Split s;
  bool found = false;
  for (const auto& r : ranges) {
    if (r.key != key) continue;
    found = true;
    s.before.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(r.startLine));
    s.block.assign(lines.begin() + static_cast<std::ptrdiff_t>(r.startLine),
                   lines.begin() + static_cast<std::ptrdiff_t>(r.endLine));
    s.after.assign(lines.begin() + static_cast<std::ptrdiff_t>(r.endLine), lines.end());
  }
  REQUIRE_MESSAGE(found, "no phoneme '" << key << "' in the text");
  return s;
}

bool contains(const std::vector<std::string>& lines, const std::string& needle) {
  for (const auto& l : lines) if (l.find(needle) != std::string::npos) return true;
  return false;
}

// The first place two line lists differ, for the failure message.
std::string firstDifference(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) return "line " + std::to_string(i + 1) + ": \"" + a[i] + "\" became \"" + b[i] + "\"";
  }
  if (a.size() != b.size()) return std::to_string(a.size()) + " lines became " + std::to_string(b.size());
  return "";
}

}  // namespace

TEST_CASE("yaml comments: phonemes.yaml saved with no edits is the same text") {
  const fs::path dir = editorScratchDir("yaml_comments_phonemes_noop");
  const fs::path file = copyPhonemes(dir);
  const std::string before = lfOnly(readAllBytes(file));
  PhonemesYaml y;
  std::string err;
  REQUIRE(y.load(file.string(), err));
  REQUIRE(y.save(err));
  const std::string after = readAllBytes(file);
  INFO(firstDifference(splitLines(before), splitLines(after)));
  CHECK(after == before);
  // What the old save lost.
  CHECK(after.find("  \"c_iv\":\n") != std::string::npos);
  CHECK(after.find("burstSpectralTilt: -0.15\n") != std::string::npos);
  CHECK(after.find("Chodroff & Wilson 2014") != std::string::npos);
}

TEST_CASE("yaml comments: an edit of one phoneme rewrites that phoneme only") {
  const fs::path dir = editorScratchDir("yaml_comments_phonemes_edit");
  const fs::path file = copyPhonemes(dir);
  const std::string before = lfOnly(readAllBytes(file));
  PhonemesYaml y;
  std::string err;
  REQUIRE(y.load(file.string(), err));
  EditorNode* a = y.getPhonemeNode("a");
  REQUIRE(a != nullptr);
  REQUIRE(a->map.count("cf1") == 1);
  a->map["cf1"].scalar = "801";
  REQUIRE(y.save(err));

  const Split was = splitAtPhoneme(before, "a");
  const Split is = splitAtPhoneme(readAllBytes(file), "a");
  INFO(firstDifference(was.before, is.before));
  CHECK(was.before == is.before);
  INFO(firstDifference(was.after, is.after));
  CHECK(was.after == is.after);
  CHECK(contains(is.block, "cf1: 801"));
}

TEST_CASE("yaml comments: exporting one override leaves every other phoneme's lines as they are") {
  const fs::path dir = editorScratchDir("yaml_comments_export");
  const fs::path file = copyPhonemes(dir);
  const std::string before = lfOnly(readAllBytes(file));
  const std::string merged = ye::exportMergedYaml(file.string(), {{"a.cf1", "800"}}, true);
  REQUIRE(!merged.empty());

  const Split was = splitAtPhoneme(before, "a");
  const Split is = splitAtPhoneme(merged, "a");
  INFO(firstDifference(was.before, is.before));
  CHECK(was.before == is.before);
  INFO(firstDifference(was.after, is.after));
  CHECK(was.after == is.after);
  CHECK(contains(is.block, "cf1: 800"));
  CHECK(merged.find("  \"c_iv\":\n") != std::string::npos);
  CHECK(merged.find("burstSpectralTilt: -0.15\n") != std::string::npos);
  CHECK(merged.find("Chodroff & Wilson 2014") != std::string::npos);
}

TEST_CASE("yaml comments: a changed phoneme's negative numbers are written as numbers") {
  const fs::path dir = editorScratchDir("yaml_comments_negative");
  const fs::path file = copyPhonemes(dir);
  PhonemesYaml y;
  std::string err;
  REQUIRE(y.load(file.string(), err));
  EditorNode* k = y.getPhonemeNode("k");
  REQUIRE(k != nullptr);
  REQUIRE(k->map.count("burstSpectralTilt") == 1);
  REQUIRE(k->map.at("burstSpectralTilt").scalar == "-0.15");
  k->map["burstDecayRate"].scalar = "0.45";
  const EditorNode wanted = *k;
  REQUIRE(y.save(err));

  const Split is = splitAtPhoneme(readAllBytes(file), "k");
  CHECK(contains(is.block, "burstSpectralTilt: -0.15"));
  CHECK(!contains(is.block, "\"-0.15\""));
  // And it reads back as the same phoneme.
  PhonemesYaml again;
  REQUIRE(again.load(file.string(), err));
  REQUIRE(again.getPhonemeNode("k") != nullptr);
  CHECK(sameTree(wanted, *again.getPhonemeNode("k")));
}

TEST_CASE("yaml comments: what starts with a dash and is not a number is still quoted") {
  EditorNode root;
  root.type = EditorNode::Type::Map;
  const char* const values[] = {"-0.15", "-12", "-3e-2", "- x", "-", "-abc", "-e", "--1", "-1.2.3", "-.", "+2"};
  int i = 0;
  for (const char* v : values) {
    EditorNode n;
    n.type = EditorNode::Type::Scalar;
    n.scalar = v;
    const std::string key = "k" + std::to_string(i++);
    root.map[key] = n;
    root.keyOrder.push_back(key);
  }
  const std::string text = ye::dumpYaml(root);
  CHECK(text.find("k0: -0.15\n") != std::string::npos);
  CHECK(text.find("k1: -12\n") != std::string::npos);
  CHECK(text.find("k2: -3e-2\n") != std::string::npos);
  CHECK(text.find("k3: \"- x\"\n") != std::string::npos);
  CHECK(text.find("k4: \"-\"\n") != std::string::npos);
  CHECK(text.find("k5: \"-abc\"\n") != std::string::npos);
  CHECK(text.find("k6: \"-e\"\n") != std::string::npos);
  CHECK(text.find("k7: \"--1\"\n") != std::string::npos);
  CHECK(text.find("k8: \"-1.2.3\"\n") != std::string::npos);
  CHECK(text.find("k9: \"-.\"\n") != std::string::npos);
  // Whatever the spelling, it reads back as the same values.
  EditorNode back;
  std::string err;
  REQUIRE(nvsp_frontend::yaml_min::loadString(text, back, err));
  CHECK(sameTree(root, back));
}

TEST_CASE("yaml comments: negative numbers in lists read back, and a negative-number key stays quoted") {
  auto scalar = [](const char* s) {
    EditorNode n;
    n.type = EditorNode::Type::Scalar;
    n.scalar = s;
    return n;
  };
  EditorNode block;
  block.type = EditorNode::Type::Seq;
  block.seq = {scalar("-12"), scalar("-0.15"), scalar("3"), scalar("-x")};
  EditorNode flow = block;
  flow.flowStyle = true;
  EditorNode inner;
  inner.type = EditorNode::Type::Map;
  inner.map["-2.5"] = scalar("-7");
  inner.keyOrder.push_back("-2.5");

  EditorNode root;
  root.type = EditorNode::Type::Map;
  // A bare `-1: x` at the start of a line would read as a list item, and
  // being the first key it would turn the whole document into a list.
  root.map["-1"] = scalar("x");
  root.map["block"] = block;
  root.map["flow"] = flow;
  root.map["inner"] = inner;
  root.keyOrder = {"-1", "block", "flow", "inner"};

  const std::string text = ye::dumpYaml(root);
  CHECK(text.find("\"-1\": x\n") != std::string::npos);
  CHECK(text.find("  \"-2.5\": -7\n") != std::string::npos);
  CHECK(text.find("  - -12\n") != std::string::npos);
  CHECK(text.find("  - \"-x\"\n") != std::string::npos);
  CHECK(text.find("flow: [-12, -0.15, 3, \"-x\"]\n") != std::string::npos);
  EditorNode back;
  std::string err;
  REQUIRE(nvsp_frontend::yaml_min::loadString(text, back, err));
  CHECK(sameTree(root, back));

  // The same for a phoneme's own block.
  const std::string phoneme = ye::dumpSinglePhoneme("p", inner);
  CHECK(phoneme.find("    \"-2.5\": -7\n") != std::string::npos);
}

TEST_CASE("yaml comments: every language file saved with no edits is the same text") {
  const fs::path dir = editorScratchDir("yaml_comments_lang_noop");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    std::string before = lfOnly(readAllBytes(copy));
    // Six packs end without a line end; the save gives the last line one.
    if (!before.empty() && before.back() != '\n') before.push_back('\n');
    LanguageYaml y;
    std::string err;
    REQUIRE(y.load(copy.string(), err));
    REQUIRE(y.save(err));
    const std::string after = readAllBytes(copy);
    INFO(firstDifference(splitLines(before), splitLines(after)));
    CHECK(after == before);
  }
}

TEST_CASE("yaml comments: a changed setting rewrites `settings:` and no other section") {
  const fs::path dir = editorScratchDir("yaml_comments_lang_edit");
  for (const char* tagC : {"es", "hu", "en-us", "zh"}) {
    const std::string tag = tagC;
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    const std::vector<std::string> before = splitLines(readAllBytes(copy));
    LanguageYaml y;
    std::string err;
    REQUIRE(y.load(copy.string(), err));
    auto rows = y.settings();
    REQUIRE(!rows.empty());
    rows.front().second = "0.123";
    y.setSettings(rows);
    REQUIRE(y.save(err));
    const std::vector<std::string> after = splitLines(readAllBytes(copy));

    // Each top-level section but `settings:` has the lines it had.
    std::vector<ye::TopLevelRange> was, is;
    size_t headerWas = 0, headerIs = 0;
    ye::findTopLevelRanges(before, was, headerWas);
    ye::findTopLevelRanges(after, is, headerIs);
    REQUIRE(was.size() == is.size());
    REQUIRE(was.size() >= 2);
    for (size_t i = 0; i < was.size(); ++i) {
      INFO("section: " << was[i].key);
      REQUIRE(was[i].key == is[i].key);
      if (was[i].key == "settings") continue;
      const std::vector<std::string> a(before.begin() + static_cast<std::ptrdiff_t>(was[i].startLine),
                                       before.begin() + static_cast<std::ptrdiff_t>(was[i].endLine));
      const std::vector<std::string> b(after.begin() + static_cast<std::ptrdiff_t>(is[i].startLine),
                                       after.begin() + static_cast<std::ptrdiff_t>(is[i].endLine));
      INFO(firstDifference(a, b));
      CHECK(a == b);
    }
  }
}

TEST_CASE("yaml comments: exporting one settings override leaves a language's other sections as they are") {
  const fs::path dir = editorScratchDir("yaml_comments_lang_export");
  const fs::path copy = copyLangToOverride(dir, "es");
  const std::vector<std::string> before = splitLines(readAllBytes(copy));
  const std::string merged = ye::exportMergedYaml(copy.string(), {{"primaryStressDiv", "1.3"}}, false);
  REQUIRE(!merged.empty());
  const std::vector<std::string> after = splitLines(merged);

  std::vector<ye::TopLevelRange> was, is;
  size_t headerWas = 0, headerIs = 0;
  ye::findTopLevelRanges(before, was, headerWas);
  ye::findTopLevelRanges(after, is, headerIs);
  REQUIRE(was.size() == is.size());
  for (size_t i = 0; i < was.size(); ++i) {
    INFO("section: " << was[i].key);
    if (was[i].key == "settings") continue;
    const std::vector<std::string> a(before.begin() + static_cast<std::ptrdiff_t>(was[i].startLine),
                                     before.begin() + static_cast<std::ptrdiff_t>(was[i].endLine));
    const std::vector<std::string> b(after.begin() + static_cast<std::ptrdiff_t>(is[i].startLine),
                                     after.begin() + static_cast<std::ptrdiff_t>(is[i].endLine));
    INFO(firstDifference(a, b));
    CHECK(a == b);
  }
  EditorNode back;
  std::string err;
  REQUIRE(nvsp_frontend::yaml_min::loadString(merged, back, err));
  EditorNode* v = nodeAt(back, {"settings", "primaryStressDiv"});
  REQUIRE(v != nullptr);
  CHECK(v->scalar == "1.3");
}
