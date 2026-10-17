#pragma once

// SHA-256 pela API de criptografia do Windows (BCrypt): sem biblioteca nova. Usado para conferir cada arquivo
// baixado de um servidor de versões (Fase 15). Resultado em hexadecimal minúsculo (64 caracteres).

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace distrib {

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void Update(const void* data, size_t size);
    std::string Final();  // hexadecimal; depois disso o objeto recomeça vazio

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

std::string Sha256Of(std::string_view data);
// Hash de um arquivo inteiro; nullopt se não puder ser lido.
std::optional<std::string> Sha256File(const std::filesystem::path& file, std::string* error = nullptr);

} // namespace distrib
