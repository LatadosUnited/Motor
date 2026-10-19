#include "motor/common/FileWrite.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <format>
#include <fstream>
#include <thread>

namespace motor {

namespace {
// Para as mensagens de erro (sem o Paths.h da engine: esta biblioteca não depende de nada).
std::string Utf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

#ifdef _WIN32
// Erros de arquivo preso por outro processo: passam sozinhos em milissegundos.
bool IsTransient(DWORD code) {
    return code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_LOCK_VIOLATION;
}
#endif
} // namespace

bool ReplaceFileAtomic(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    ec.clear();
#ifdef _WIN32
    // 10, 20, 40... ms, até ~1,3 s no total.
    auto wait = std::chrono::milliseconds(10);
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD code = GetLastError();
        if (!IsTransient(code) || attempt >= 7) {
            ec = std::error_code(static_cast<int>(code), std::system_category());
            return false;
        }
        std::this_thread::sleep_for(wait);
        wait *= 2;
    }
#else
    std::filesystem::rename(from, to, ec);  // POSIX: troca atômica, substitui o destino
    return !ec;
#endif
}

bool WriteFileAtomic(const std::filesystem::path& file, std::string_view data, std::string* error) {
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(data.data(), static_cast<std::streamsize>(data.size())) || !out.flush()) {
            if (error) *error = std::format("Não foi possível escrever '{}'.", Utf8(temp));
            return false;
        }
    }
    std::error_code ec;
    if (!ReplaceFileAtomic(temp, file, ec)) {
        if (error) *error = std::format("Não foi possível salvar '{}': {}", Utf8(file), ec.message());
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    return true;
}

} // namespace motor
