// Shared helpers for the phoneme editor's tests (test_editor_*.cpp).
//
// The editor's YAML model is exercised the way its dialogs use it, on copies
// of the shipped packs: a copy of packs/lang/<tag>.yaml goes into a scratch
// "override" directory, the editor loads, edits and saves it there, and the
// frontend's own loader then reads it back through its override directory
// (<dir>/packs/lang/<tag>.yaml shadows the shipped file).  Nothing under
// packs/ is written.

#ifndef TGSB_TEST_EDITOR_TEST_UTIL_H
#define TGSB_TEST_EDITOR_TEST_UTIL_H

#include "doctest.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "nvspFrontend.h"
#include "pack.h"
#include "pack_fixture.h"
#include "yaml_min.h"

namespace tgsb_test {

using EditorNode = nvsp_frontend::yaml_min::Node;

// An empty directory of this test's own under the system temp directory.
inline std::filesystem::path editorScratchDir(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path d = fs::temp_directory_path() / "tgsb_editor_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

inline std::string readAllBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline void writeAllBytes(const std::filesystem::path& p, const std::string& s) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

// The text with \r\n read as \n: a checkout may hold either, and the
// editor writes \n.
inline std::string lfOnly(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out.push_back(s[i]);
    }
    return out;
}

inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : lfOnly(s)) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

inline std::filesystem::path repoRoot() {
    const std::string root = findPackDir();
    REQUIRE_MESSAGE(!root.empty(), "could not locate 'packs' directory by walking up from cwd");
    return std::filesystem::path(root);
}

// Every packs/lang/<tag>.yaml, by tag, sorted.
inline std::vector<std::string> languageTags() {
    namespace fs = std::filesystem;
    std::vector<std::string> tags;
    for (const auto& e : fs::directory_iterator(repoRoot() / "packs" / "lang")) {
        if (e.path().extension() == ".yaml") tags.push_back(e.path().stem().string());
    }
    std::sort(tags.begin(), tags.end());
    return tags;
}

inline std::filesystem::path shippedLangFile(const std::string& tag) {
    return repoRoot() / "packs" / "lang" / (tag + ".yaml");
}

// Copy packs/lang/<tag>.yaml into an override directory; returns the copy.
inline std::filesystem::path copyLangToOverride(const std::filesystem::path& overrideDir,
                                                const std::string& tag) {
    namespace fs = std::filesystem;
    const fs::path dst = overrideDir / "packs" / "lang" / (tag + ".yaml");
    fs::create_directories(dst.parent_path());
    fs::copy_file(shippedLangFile(tag), dst, fs::copy_options::overwrite_existing);
    return dst;
}

inline EditorNode parseYamlFile(const std::filesystem::path& p) {
    EditorNode root;
    std::string err;
    const bool ok = nvsp_frontend::yaml_min::loadFile(p.string(), root, err);
    REQUIRE_MESSAGE(ok, "could not parse " << p.string() << ": " << err);
    return root;
}

// Same data: type, scalar text, list items in order, map keys and values.
// Key order and flow style ([a, b] against a block list) are layout, not data.
inline bool sameTree(const EditorNode& a, const EditorNode& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case EditorNode::Type::Null: return true;
        case EditorNode::Type::Scalar: return a.scalar == b.scalar;
        case EditorNode::Type::Seq:
            if (a.seq.size() != b.seq.size()) return false;
            for (size_t i = 0; i < a.seq.size(); ++i) {
                if (!sameTree(a.seq[i], b.seq[i])) return false;
            }
            return true;
        case EditorNode::Type::Map: {
            if (a.map.size() != b.map.size()) return false;
            auto ia = a.map.begin();
            auto ib = b.map.begin();
            for (; ia != a.map.end(); ++ia, ++ib) {
                if (ia->first != ib->first) return false;
                if (!sameTree(ia->second, ib->second)) return false;
            }
            return true;
        }
    }
    return false;
}

// The node at a path of map keys, or nullptr.
inline EditorNode* nodeAt(EditorNode& root, std::initializer_list<const char*> path) {
    EditorNode* n = &root;
    for (const char* k : path) {
        if (!n->isMap()) return nullptr;
        auto it = n->map.find(k);
        if (it == n->map.end()) return nullptr;
        n = &it->second;
    }
    return n;
}

// Load a language through the frontend's own loader, with an optional
// override directory shadowing the shipped lang files.
inline nvsp_frontend::PackSet loadPackWithOverride(const std::string& tag,
                                                   const std::filesystem::path& overrideDir = {}) {
    nvsp_frontend::PackSet pack;
    std::string err;
    const bool ok = nvsp_frontend::loadPackSet(repoRoot().string(), tag, pack, err,
                                               overrideDir.empty() ? std::string() : overrideDir.string());
    REQUIRE_MESSAGE(ok, "loadPackSet failed for '" << tag << "': " << err);
    return pack;
}

// Every frame the frontend emits for an IPA string, flattened to numbers
// (duration, fade, then the frame's fields; a silence frame is a marker).
inline std::vector<double> framesFor(const std::string& tag, const std::string& ipa, double speed,
                                     const std::filesystem::path& overrideDir = {}) {
    std::vector<double> out;
    nvspFrontend_handle_t h = nvspFrontend_create(repoRoot().string().c_str());
    REQUIRE(h != nullptr);
    if (!overrideDir.empty()) nvspFrontend_setOverrideDirectory(h, overrideDir.string().c_str());
    const int langOk = nvspFrontend_setLanguage(h, tag.c_str());
    if (!langOk) {
        const char* e = nvspFrontend_getLastError(h);
        const std::string msg = e ? e : "(unknown)";
        nvspFrontend_destroy(h);
        FAIL("nvspFrontend_setLanguage('" << tag << "') failed: " << msg);
    }
    auto cb = [](void* user, const nvspFrontend_Frame* frame, double durationMs, double fadeMs, int) {
        auto* v = static_cast<std::vector<double>*>(user);
        v->push_back(durationMs);
        v->push_back(fadeMs);
        if (!frame) { v->push_back(-12345.0); return; }
        const size_t n = sizeof(nvspFrontend_Frame) / sizeof(double);
        std::vector<double> fields(n);
        std::memcpy(fields.data(), frame, n * sizeof(double));
        v->insert(v->end(), fields.begin(), fields.end());
    };
    const int ok = nvspFrontend_queueIPA(h, ipa.c_str(), speed, 120.0, 0.5, ".", 0, cb, &out);
    nvspFrontend_destroy(h);
    REQUIRE(ok == 1);
    return out;
}

inline const double kEditorTestSpeeds[] = {0.5, 1.0, 1.6, 2.0, 3.0};

}  // namespace tgsb_test

#endif  // TGSB_TEST_EDITOR_TEST_UTIL_H
