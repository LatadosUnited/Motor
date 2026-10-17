#include "Sha256.h"

#include <HubCore.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace distrib {

#ifdef _WIN32
struct Sha256::Impl {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    void Open() {
        if (!BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
            throw std::runtime_error("BCryptCreateHash falhou.");
        }
    }
};

Sha256::Sha256() : m_impl(std::make_unique<Impl>()) {
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&m_impl->alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("SHA-256 indisponível (BCryptOpenAlgorithmProvider).");
    }
    m_impl->Open();
}

Sha256::~Sha256() {
    if (m_impl->hash) BCryptDestroyHash(m_impl->hash);
    if (m_impl->alg) BCryptCloseAlgorithmProvider(m_impl->alg, 0);
}

void Sha256::Update(const void* data, size_t size) {
    // BCryptHashData recebe ULONG: blocos grandes vão em partes.
    auto* bytes = static_cast<const unsigned char*>(data);
    while (size > 0) {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(size, 1u << 30));
        BCryptHashData(m_impl->hash, const_cast<PUCHAR>(bytes), chunk, 0);
        bytes += chunk;
        size -= chunk;
    }
}

std::string Sha256::Final() {
    unsigned char digest[32] = {};
    BCryptFinishHash(m_impl->hash, digest, sizeof(digest), 0);
    BCryptDestroyHash(m_impl->hash);
    m_impl->hash = nullptr;
    m_impl->Open();

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (unsigned char b : digest) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 15]);
    }
    return out;
}
#else
struct Sha256::Impl {};
Sha256::Sha256() : m_impl(std::make_unique<Impl>()) {
    throw std::runtime_error("SHA-256 só no Windows (BCrypt).");
}
Sha256::~Sha256() = default;
void Sha256::Update(const void*, size_t) {}
std::string Sha256::Final() { return {}; }
#endif

std::string Sha256Of(std::string_view data) {
    Sha256 sha;
    sha.Update(data.data(), data.size());
    return sha.Final();
}

std::optional<std::string> Sha256File(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = std::format("Não foi possível ler '{}'.", hub::ToUtf8(file));
        return std::nullopt;
    }
    Sha256 sha;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) sha.Update(buffer.data(), static_cast<size_t>(got));
    }
    if (in.bad()) {
        if (error) *error = std::format("Erro lendo '{}'.", hub::ToUtf8(file));
        return std::nullopt;
    }
    return sha.Final();
}

} // namespace distrib
