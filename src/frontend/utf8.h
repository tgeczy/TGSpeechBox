#ifndef NVSP_FRONTEND_UTF8_H
#define NVSP_FRONTEND_UTF8_H

#include <string>
#include <string_view>

namespace nvsp_frontend {

// Best-effort UTF-8 -> UTF-32. Invalid sequences become U+FFFD.
std::u32string utf8ToU32(std::string_view s);

// UTF-32 -> UTF-8.
std::string u32ToUtf8(std::u32string_view s);

// Lowercase ASCII and convert '_' -> '-' (for language tags).
std::string normalizeLangTag(std::string_view tag);

} // namespace nvsp_frontend

#endif
