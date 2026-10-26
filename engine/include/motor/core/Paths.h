#pragma once

// ToUtf8, ToGenericUtf8 e PathFromUtf8 (comuns à engine, ao hub e à distribuição).
#include "motor/common/Utf8.h"

#include <filesystem>

namespace motor {

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
