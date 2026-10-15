#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace motor {

// Conversões explícitas path <-> UTF-8. No Windows, path::string() usa a página de código local e
// pode falhar (ou trocar caracteres) com acentos; a engine trata todo texto como UTF-8.
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

// true se `path` está dentro de `root` (ou é o próprio root). Compara de forma léxica, sem acessar o disco.
inline bool IsInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path rel = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}

// Se `path` estava dentro de `from` (ou era o próprio `from`), devolve o caminho equivalente dentro de `to`.
// Útil para manter referências válidas depois de mover/renomear uma pasta ou arquivo.
inline std::filesystem::path RebasePath(const std::filesystem::path& path, const std::filesystem::path& from,
                                        const std::filesystem::path& to) {
    if (path.empty() || !IsInside(path, from)) return path;
    const std::filesystem::path rel = path.lexically_normal().lexically_relative(from.lexically_normal());
    return rel == "." ? to : (to / rel).lexically_normal();
}

} // namespace motor
