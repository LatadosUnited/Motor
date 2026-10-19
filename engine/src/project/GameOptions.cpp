#include "motor/project/GameOptions.h"

#include "motor/common/FileWrite.h"
#include "motor/core/Paths.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace motor {

using json = nlohmann::ordered_json;

namespace {
constexpr const char* kFormatName = "motor-opcoes";
constexpr int kFormatVersion = 1;

constexpr ShadowSetting kShadowSettings[] = {ShadowSetting::Off, ShadowSetting::Low, ShadowSetting::Medium,
                                             ShadowSetting::High};

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}
} // namespace

const char* ShadowSettingName(ShadowSetting shadows) {
    switch (shadows) {
        case ShadowSetting::Off: return "desligadas";
        case ShadowSetting::Low: return "baixas";
        case ShadowSetting::Medium: return "medias";
        case ShadowSetting::High: break;
    }
    return "altas";
}

const char* ShadowSettingLabel(ShadowSetting shadows) {
    switch (shadows) {
        case ShadowSetting::Off: return "Desligadas";
        case ShadowSetting::Low: return "Baixas";
        case ShadowSetting::Medium: return "Médias";
        case ShadowSetting::High: break;
    }
    return "Altas";
}

void GameOptions::Clamp() {
    if (width < 0 || height < 0 || (width == 0) != (height == 0)) width = height = 0;
    int best = kMsaaChoices[0];
    for (int choice : kMsaaChoices) {
        if (std::abs(choice - msaa) < std::abs(best - msaa)) best = choice;
    }
    msaa = best;
    const int level = static_cast<int>(shadows);
    if (level < 0 || level > static_cast<int>(ShadowSetting::High)) shadows = ShadowSetting::High;
}

std::string GameOptionsToJson(const GameOptions& options) {
    json root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;
    root["fullscreen"] = options.fullscreen;
    root["width"] = options.width;
    root["height"] = options.height;
    root["vsync"] = options.vsync;
    root["msaa"] = options.msaa;
    root["shadows"] = ShadowSettingName(options.shadows);
    return root.dump(2, ' ', false, json::error_handler_t::replace) + '\n';
}

bool GameOptionsFromJson(std::string_view text, GameOptions& options, const GameOptions& defaults,
                         std::string* error) {
    options = defaults;
    try {
        const json root = json::parse(text);
        if (!root.is_object()) {
            SetError(error, "as opções não são um objeto JSON");
            return false;
        }
        GameOptions read = defaults;
        read.fullscreen = root.value("fullscreen", defaults.fullscreen);
        read.width = root.value("width", defaults.width);
        read.height = root.value("height", defaults.height);
        read.vsync = root.value("vsync", defaults.vsync);
        read.msaa = root.value("msaa", defaults.msaa);
        const std::string shadows = root.value("shadows", std::string(ShadowSettingName(defaults.shadows)));
        for (ShadowSetting s : kShadowSettings) {
            if (shadows == ShadowSettingName(s)) read.shadows = s;
        }
        read.Clamp();
        options = read;
        return true;
    } catch (const json::exception& e) {
        SetError(error, std::format("opções inválidas: {}", e.what()));
        return false;
    }
}

bool LoadGameOptions(const std::filesystem::path& file, GameOptions& options, const GameOptions& defaults,
                     std::string* error) {
    options = defaults;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SetError(error, std::format("'{}' não existe", ToUtf8(file)));
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return GameOptionsFromJson(ss.str(), options, defaults, error);
}

bool SaveGameOptions(const std::filesystem::path& file, const GameOptions& options, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return WriteFileAtomic(file, GameOptionsToJson(options), error);
}

std::filesystem::path PlayerDataDir(std::string_view game) {
    // Nome de pasta: sem os caracteres que o Windows não aceita (o nome do projeto já é validado, mas por garantia).
    std::string folder;
    for (char c : game) folder += std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos ? c : '_';
    while (!folder.empty() && (folder.back() == ' ' || folder.back() == '.')) folder.pop_back();
    if (folder.empty()) folder = "Jogo";
#ifdef _WIN32
    if (const wchar_t* appData = _wgetenv(L"APPDATA"); appData && *appData) {  // wide: a ANSI estraga acentos
        return std::filesystem::path(appData) / PathFromUtf8(folder);
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / folder;
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / folder;
    }
#endif
    return {};
}

} // namespace motor
