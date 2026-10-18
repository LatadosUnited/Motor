#include "motor/core/EngineAssets.h"

#include "motor/core/Paths.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace motor {

namespace fs = std::filesystem;

fs::path ExecutableDir() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);  // caminho longo (\\?\...)
    }
    return fs::path(buffer).parent_path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

fs::path SourceEngineAssetsDir() {
    return PathFromUtf8(MOTOR_ENGINE_ASSETS);
}

fs::path EngineAssetsDir() {
    // Decidido uma vez: a pasta não muda enquanto o processo roda.
    static const fs::path dir = [] {
        std::error_code ec;
        const fs::path exported = ExecutableDir() / kEngineAssetsFolder;
        if (fs::is_directory(exported / "shaders", ec)) return exported;
        return SourceEngineAssetsDir();
    }();
    return dir;
}

fs::path EngineDocsDir() {
    std::error_code ec;
    const fs::path installed = ExecutableDir() / "docs";
    if (fs::is_directory(installed, ec)) return installed;
    return SourceEngineAssetsDir().parent_path().parent_path() / "docs";
}

} // namespace motor
