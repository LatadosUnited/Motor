#include "EditorSettings.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace editor {

using json = nlohmann::ordered_json;
using motor::PathFromUtf8;
using motor::ToUtf8;

namespace {
std::filesystem::path g_fileOverride;  // --settings
bool g_disabled = false;              // --no-settings
} // namespace

void EditorSettings::UseFile(const std::filesystem::path& file) {
    std::error_code ec;
    g_fileOverride = std::filesystem::absolute(file, ec);
    if (ec) g_fileOverride = file;
}

void EditorSettings::Disable() { g_disabled = true; }

bool EditorSettings::IsDisabled() { return g_disabled; }

std::filesystem::path EditorSettings::FilePath() {
    if (!g_fileOverride.empty()) return g_fileOverride;
#ifdef _WIN32
    if (const wchar_t* appData = _wgetenv(L"APPDATA")) {  // versão wide: a ANSI estraga acentos
        return std::filesystem::path(appData) / "Motor" / "editor.json";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "motor" / "editor.json";
    }
#endif
    return "editor.json";
}

EditorSettings EditorSettings::Load() {
    EditorSettings settings;
    if (g_disabled) return settings;
    std::ifstream in(FilePath(), std::ios::binary);
    if (!in) return settings;  // primeira execução

    std::stringstream ss;
    ss << in.rdbuf();
    try {
        const json root = json::parse(ss.str());
        settings.lastProject = PathFromUtf8(root.value("lastProject", std::string()));
        settings.lastScene = PathFromUtf8(root.value("lastScene", std::string()));
        for (const auto& p : root.value("recentProjects", json::array())) {
            if (p.is_string()) settings.recentProjects.push_back(PathFromUtf8(p.get<std::string>()));
        }
        settings.projectGridView = root.value("projectGridView", settings.projectGridView);
        settings.projectIconSize = std::clamp(root.value("projectIconSize", settings.projectIconSize), 40.0f, 160.0f);
        ViewportPrefs& v = settings.viewport;
        v.colliders = ParseColliderOverlay(root.value("viewportColliders", std::string()));
        v.snap = root.value("snapEnabled", v.snap);
        v.snapMove = root.value("snapMove", v.snapMove);
        v.snapRotate = root.value("snapRotate", v.snapRotate);
        v.snapScale = root.value("snapScale", v.snapScale);
        v.grid = root.value("gridVisible", v.grid);
        v.gridFine = root.value("gridFine", v.gridFine);
        v.gridHeight = root.value("gridHeight", v.gridHeight);
        v.stats = root.value("viewportStats", v.stats);
        v.navMesh = root.value("viewportNavMesh", v.navMesh);
        v.Clamp();
        settings.playSideBySide = root.value("playSideBySide", settings.playSideBySide);
        settings.mcpEnabled = root.value("mcpEnabled", settings.mcpEnabled);
        settings.mcpPort = std::clamp(root.value("mcpPort", settings.mcpPort), 1024, 65535);
    } catch (const json::exception& e) {
        motor::log::Warn("Configurações do editor ignoradas ({}): {}", ToUtf8(FilePath()), e.what());
    }
    return settings;
}

void EditorSettings::Save() const {
    if (g_disabled) return;
    json root;
    root["lastProject"] = ToUtf8(lastProject);
    root["lastScene"] = ToUtf8(lastScene);
    json recent = json::array();
    for (const auto& p : recentProjects) recent.push_back(ToUtf8(p));
    root["recentProjects"] = std::move(recent);
    root["projectGridView"] = projectGridView;
    root["projectIconSize"] = projectIconSize;
    root["viewportColliders"] = std::string(ColliderOverlayName(viewport.colliders));
    root["snapEnabled"] = viewport.snap;
    root["snapMove"] = viewport.snapMove;
    root["snapRotate"] = viewport.snapRotate;
    root["snapScale"] = viewport.snapScale;
    root["gridVisible"] = viewport.grid;
    root["gridFine"] = viewport.gridFine;
    root["gridHeight"] = viewport.gridHeight;
    root["viewportStats"] = viewport.stats;
    root["viewportNavMesh"] = viewport.navMesh;
    root["playSideBySide"] = playSideBySide;
    root["mcpEnabled"] = mcpEnabled;
    root["mcpPort"] = mcpPort;

    const std::filesystem::path file = FilePath();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out || !(out << root.dump(2, ' ', false, json::error_handler_t::replace) << '\n')) {
        motor::log::Warn("Não foi possível salvar as configurações do editor em {}", ToUtf8(file));
    }
}

void EditorSettings::AddRecentProject(const std::filesystem::path& projectFile) {
    RemoveRecentProject(projectFile);
    recentProjects.insert(recentProjects.begin(), projectFile);
    if (recentProjects.size() > kMaxRecent) recentProjects.resize(kMaxRecent);
}

void EditorSettings::RemoveRecentProject(const std::filesystem::path& projectFile) {
    std::erase_if(recentProjects, [&](const std::filesystem::path& p) {
        return p.lexically_normal() == projectFile.lexically_normal();
    });
}

} // namespace editor
