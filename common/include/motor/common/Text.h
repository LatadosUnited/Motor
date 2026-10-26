#pragma once

#include "motor/common/Utf8.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

namespace motor {

// Para as funções que devolvem o erro num `std::string* error` opcional: só grava se pediram.
inline void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

// Minúsculas só no ASCII (os bytes do UTF-8 acima de 127 ficam como estão).
inline std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Extensão do arquivo em minúsculas, com o ponto (".png"); vazia sem extensão.
inline std::string LowerExtension(const std::filesystem::path& path) {
    return Lower(ToUtf8(path.extension()));
}

} // namespace motor
