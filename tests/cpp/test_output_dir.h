// Where the tests put the files they write for ear-inspection.
//
// The doctests render ~100 WAVs and trace sidecars per run. They used to
// land wherever the exe happened to be started from -- usually the repo
// root, which turned into a zoo of test_output_*.wav (2026-09-13). Every
// writer now routes a relative path through outputPath(), which puts it
// under <repo>/test-wavs/ (gitignored), found by walking up from cwd the
// same way findPackDir() finds packs/. Absolute paths are left alone.

#ifndef TGSB_TEST_OUTPUT_DIR_H
#define TGSB_TEST_OUTPUT_DIR_H

#include <filesystem>
#include <string>

namespace tgsb_test {

inline std::string outputPath(const std::string& rel) {
    namespace fs = std::filesystem;
    fs::path in(rel);
    if (in.is_absolute()) return rel;
    fs::path root = fs::current_path();
    fs::path p = root;
    for (int i = 0; i < 10; ++i) {
        if (fs::is_directory(p / "packs")) { root = p; break; }
        if (!p.has_parent_path() || p == p.parent_path()) break;
        p = p.parent_path();
    }
    fs::path out = root / "test-wavs" / in;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    return out.string();
}

}  // namespace tgsb_test

#endif  // TGSB_TEST_OUTPUT_DIR_H
