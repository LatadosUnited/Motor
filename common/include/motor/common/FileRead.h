#pragma once

#include "motor/common/Text.h"
#include "motor/common/Utf8.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace motor {

// O arquivo inteiro como texto, sem converter fim de linha. nullopt se não abriu; `.value_or("")` para quem trata
// arquivo ausente como vazio.
inline std::optional<std::string> ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// O arquivo inteiro em `out`. Erro: false, e `error` recebe a mensagem com o caminho.
inline bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& out,
                           std::string* error = nullptr) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(path)));
        return false;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    out.resize(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) {
        SetError(error, std::format("Erro ao ler '{}'.", ToUtf8(path)));
        return false;
    }
    return true;
}

} // namespace motor
