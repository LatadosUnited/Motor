#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace motor {

// Conversões explícitas path <-> UTF-8. No Windows, path::string() usa a página de código local e
// pode falhar (ou trocar caracteres) com acentos; a engine, o hub e a distribuição tratam todo texto como UTF-8.
// Vai também no SDK dos scripts (incluído pelo motor/core/Paths.h).
inline std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

inline std::string ToGenericUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

inline std::filesystem::path PathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace motor
