#ifndef TGSB_FRONTEND_TEXT_PARSER_H
#define TGSB_FRONTEND_TEXT_PARSER_H

#include <string>
#include <unordered_map>
#include <vector>

namespace nvsp_frontend {

// Run text-level plugins on IPA before it enters the IPA engine.
//
// Currently the only plugin is **stress lookup**: if a word appears in
// stressDict, its IPA stress marks (ˈ ˌ) are repositioned to match the
// dictionary pattern.
//
// If text is empty, stressDict is empty, or no corrections apply, the
// original IPA is returned unchanged.  Every failure mode is "do nothing."
std::string runTextParser(
    const std::string& text,
    const std::string& ipa,
    const std::unordered_map<std::string, std::vector<int>>& stressDict);

}  // namespace nvsp_frontend

#endif  // TGSB_FRONTEND_TEXT_PARSER_H
