#pragma once

#include <filesystem>
#include <vector>

namespace editor {

// Preferências do editor que valem entre sessões (não pertencem a um projeto).
// Ficam em %APPDATA%/Motor/editor.json (Windows) ou ~/.config/motor/editor.json.
struct EditorSettings {
    std::filesystem::path lastProject;  // arquivo .mproject
    std::filesystem::path lastScene;    // última cena aberta no lastProject
    std::vector<std::filesystem::path> recentProjects;
    bool projectGridView = false;   // painel Projeto: grade (true) ou lista
    float projectIconSize = 72.0f;
    bool mcpEnabled = false;  // servidor MCP (Ver > Servidor MCP) ligado ao abrir o editor
    int mcpPort = 47800;

    static constexpr size_t kMaxRecent = 8;

    static std::filesystem::path FilePath();
    static EditorSettings Load();
    void Save() const;

    void AddRecentProject(const std::filesystem::path& projectFile);
    void RemoveRecentProject(const std::filesystem::path& projectFile);
};

} // namespace editor
