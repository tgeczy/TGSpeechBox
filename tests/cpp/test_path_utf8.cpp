// A path given to the frontend as UTF-8 is the file the caller meant.
//
// nvspFrontend_create and nvspFrontend_setOverrideDirectory take UTF-8.  On
// Windows the frontend then handed the bytes to std::filesystem and
// std::ifstream, which read a narrow string in the system code page, so a
// folder with a letter outside ASCII was not found.  pathFromUtf8 and
// pathToUtf8 (utf8.h) are the two functions every such path now goes through.
//
// These write a real file under such a name and read it back through the two
// functions: what matters is that the file opens, not what the path's bytes
// are.  tests/test_pack_dir_unicode.py does the same through the public API
// with whole packs.

#include <filesystem>
#include <fstream>
#include <string>

#include "doctest.h"

#include "utf8.h"

using nvsp_frontend::pathFromUtf8;
using nvsp_frontend::pathToUtf8;
using nvsp_frontend::u32ToUtf8;

namespace fs = std::filesystem;

namespace {

// The system's temporary folder as UTF-8, whatever the account is called.
std::string tempDirUtf8() { return pathToUtf8(fs::temp_directory_path()); }

struct TempTree {
    fs::path root;
    explicit TempTree(const fs::path& p) : root(p) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

}  // namespace

TEST_CASE("pathFromUtf8: a file under a folder named beyond ASCII opens, and reads back what was written") {
    static const char32_t* const kNames[] = {
        U"plain",                 // the control
        U"Tamás José",  // in the Western code page
        U"Győző",       // European, outside it
        U"Всеволод",
        U"用户",
    };
    for (const char32_t* name : kNames) {
        const std::string folderUtf8 = tempDirUtf8() + "/tgsb_path_test_" + u32ToUtf8(name);
        CAPTURE(folderUtf8);
        TempTree tree(pathFromUtf8(folderUtf8));
        const std::string fileUtf8 = folderUtf8 + "/phonemes.yaml";
        {
            std::ofstream out(pathFromUtf8(fileUtf8), std::ios::binary);
            REQUIRE(out.good());
            out << "written under " << folderUtf8;
        }
        std::ifstream in(pathFromUtf8(fileUtf8), std::ios::binary);
        REQUIRE(in.good());
        std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(got == "written under " + folderUtf8);
        CHECK(fs::exists(pathFromUtf8(fileUtf8)));
    }
}

TEST_CASE("pathToUtf8 gives back the UTF-8 that pathFromUtf8 was given") {
    static const char32_t* const kNames[] = {
        U"plain", U"Tamás", U"Győző", U"Все", U"用户", U"a b/c.yaml",
    };
    for (const char32_t* name : kNames) {
        const std::string utf8 = u32ToUtf8(name);
        CAPTURE(utf8);
        CHECK(pathToUtf8(pathFromUtf8(utf8)) == utf8);
    }
    CHECK(pathFromUtf8("").empty());
    CHECK(pathToUtf8(fs::path()).empty());
}

TEST_CASE("pathToUtf8: a folder's entries come back as names that open") {
    // The language list is read from a directory listing: each name found
    // there is turned into text and used to open a file again.
    const std::string folderUtf8 = tempDirUtf8() + "/tgsb_path_list_" + u32ToUtf8(U"Győző 用户");
    TempTree tree(pathFromUtf8(folderUtf8));
    { std::ofstream out(pathFromUtf8(folderUtf8 + "/hu.yaml")); out << "x"; }
    int seen = 0;
    for (const auto& entry : fs::directory_iterator(pathFromUtf8(folderUtf8))) {
        ++seen;
        CHECK(pathToUtf8(entry.path().stem()) == "hu");
        std::ifstream in(pathFromUtf8(pathToUtf8(entry.path())));
        CHECK(in.good());
    }
    CHECK(seen == 1);
}

#ifdef _WIN32
TEST_CASE("pathFromUtf8: bytes that are not UTF-8 are a path in the system code page, as before") {
    // A command-line tool hands the frontend its argv, which Windows gives
    // in the system code page.  0xE1 alone is not UTF-8.
    const std::string ansi = std::string("caf") + static_cast<char>(0xE1);
    CHECK(pathFromUtf8(ansi) == fs::path(ansi));
}
#endif
