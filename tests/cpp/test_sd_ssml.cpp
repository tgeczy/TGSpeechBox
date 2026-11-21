// The text of a Speech Dispatcher SPEAK message is what the client wrote.
//
// Speech Dispatcher sends a module SSML: the client's text in <speak>, a
// <mark/> after each sentence, and "<", ">" and "&" as "&lt;", "&gt;" and
// "&amp;" (speech-dispatcher 0.11.1, src/server/index_marking.c).  The Linux
// module removed the tags and left the entities, so "Tom & Jerry" was read
// as "Tom and amp Jerry".  Found while checking this repository against the
// audit of the maintainer's SSI-263 project, whose module had the fault next
// to this one (a "<" sent as a single character made no sound).
//
// sdSsmlToText is a plain function in a header, so it is tested here on
// every platform; tests/test_linux_speechd_module.py drives the built
// module itself on Linux.

#include <string>

#include "doctest.h"

#include "platforms/speechd/sd_ssml.h"

using tgsb_sd::sdSsmlToText;

TEST_CASE("sdSsmlToText: what the server wraps around plain text comes off") {
    CHECK(sdSsmlToText("<speak>Hello world</speak>") == "Hello world");
    CHECK(sdSsmlToText("<speak></speak>") == "");
    CHECK(sdSsmlToText("no markup at all") == "no markup at all");
    CHECK(sdSsmlToText("") == "");
    // The server's own index mark after a sentence leaves the text as typed.
    CHECK(sdSsmlToText("<speak>One.<mark name=\"__spd_0\"/> Two.</speak>") == "One. Two.");
    // A client's SSML: the tags go, the words stay.
    CHECK(sdSsmlToText("<speak>a <emphasis>very</emphasis> good <break time=\"200ms\"/>day</speak>") ==
          "a very good day");
}

TEST_CASE("sdSsmlToText: the three entities the server writes are their characters") {
    // What speech-dispatcher sends for the plain text "Tom & Jerry", "a < b" and "a > b".
    CHECK(sdSsmlToText("<speak>Tom &amp; Jerry</speak>") == "Tom & Jerry");
    CHECK(sdSsmlToText("<speak>a &lt; b</speak>") == "a < b");
    CHECK(sdSsmlToText("<speak>a &gt; b</speak>") == "a > b");
    CHECK(sdSsmlToText("<speak>&lt;</speak>") == "<");
    CHECK(sdSsmlToText("<speak>&lt;b&gt; R&amp;D &lt;/b&gt;</speak>") == "<b> R&D </b>");
}

TEST_CASE("sdSsmlToText: the entities a client's own SSML may use") {
    CHECK(sdSsmlToText("<speak>it&apos;s &quot;so&quot;</speak>") == "it's \"so\"");
    CHECK(sdSsmlToText("&#60;&#x3C;&#X3c;") == "<<<");
    CHECK(sdSsmlToText("caf&#233; &#x151; &#x4F60;") == "caf\xC3\xA9 \xC5\x91 \xE4\xBD\xA0");   // é ő 你
    CHECK(sdSsmlToText("&#x1F600;") == "\xF0\x9F\x98\x80");                                      // U+1F600
}

TEST_CASE("sdSsmlToText: a decoded character is text, never markup") {
    // "&lt;speak&gt;" is the text "<speak>", typed by the user: it must not
    // be removed as a tag after it has been decoded.
    CHECK(sdSsmlToText("<speak>type &lt;speak&gt; here</speak>") == "type <speak> here");
    // "&amp;lt;" is the text "&lt;", not "<".
    CHECK(sdSsmlToText("<speak>&amp;lt;</speak>") == "&lt;");
    CHECK(sdSsmlToText("<speak>&amp;amp;</speak>") == "&amp;");
}

TEST_CASE("sdSsmlToText: an ampersand that begins nothing known stays as it is") {
    static const char* const kSame[] = {
        "R&D", "a & b", "AT&T;", "&", "&;", "&unknown;", "&#;", "&#x;", "&#xZZ;", "&#12a;",
        "&#0;", "&#8;", "&#xD800;", "&#x110000;", "&#99999999999;", "trailing &amp", "&averyveryverylongname;",
    };
    for (const char* s : kSame) {
        CAPTURE(s);
        CHECK(sdSsmlToText(s) == s);
    }
    // Tab, line feed and carriage return are characters XML allows.
    CHECK(sdSsmlToText("a&#9;b&#10;c&#13;d") == "a\tb\nc\rd");
}

TEST_CASE("sdSsmlToText: UTF-8 text passes through untouched") {
    const std::string text = "\xC3\xA1rv\xC3\xADzt\xC5\xB1r\xC5\x91 \xE4\xBD\xA0\xE5\xA5\xBD";   // árvíztűrő 你好
    CHECK(sdSsmlToText("<speak>" + text + "</speak>") == text);
}
