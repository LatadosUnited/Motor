#pragma once

#include "viewport/ColliderOverlay.h"

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
    ColliderOverlay viewportColliders = ColliderOverlay::Selected;  // contornos de collider no viewport
    bool mcpEnabled = false;  // servidor MCP (Ver > Servidor MCP) ligado ao abrir o editor
    int mcpPort = 47800;

    static constexpr size_t kMaxRecent = 8;

    static std::filesystem::path FilePath();
    static EditorSettings Load();
    void Save() const;

    // Linha de comando (antes do Load): --settings <arquivo> usa outro arquivo; --no-settings não lê nem grava
    // nenhum (agentes e testes automáticos não tocam na configuração do usuário).
    static void UseFile(const std::filesystem::path& file);
    static void Disable();
    static bool IsDisabled();

    void AddRecentProject(const std::filesystem::path& projectFile);
    void RemoveRecentProject(const std::filesystem::path& projectFile);
};

} // namespace editor
