#pragma once

// Hub do Motor (Fase 14): o estado (configuração, versões instaladas, projetos), as ações (abrir, criar,
// adicionar, remover) e a janela (GLFW + ImGui). As ações são as mesmas para os botões e para a linha de
// comando (main.cpp); a lógica que dá para testar sem janela fica em HubCore.

#include "HubCore.h"
#include "HubPlatform.h"

#include <future>
#include <string>
#include <vector>

struct GLFWwindow;

namespace hub {

class HubApp {
public:
    // `editorArgs`: argumentos a mais para o editor em toda abertura (os que vêm depois de "--" na linha de
    // comando do hub, ex.: --no-settings --mcp para testes automáticos).
    HubApp(platform::Paths paths, std::vector<std::string> editorArgs);

    void Reload();  // relê a configuração, as versões instaladas e os projetos

    const platform::Paths& GetPaths() const { return m_paths; }
    const HubConfig& Config() const { return m_config; }
    const std::vector<InstalledVersion>& Versions() const { return m_versions; }
    const std::vector<ProjectInfo>& Projects() const { return m_projects; }  // na ordem de Config().projects

    // Ações: true se deu certo; `message` diz o que aconteceu (ou o erro), em português.
    // `version` por valor: a ação relê a lista de versões (uma referência para ela ficaria inválida).
    bool OpenProject(const fs::path& root, InstalledVersion version, std::string& message);
    bool CreateProject(const fs::path& parent, const std::string& name, InstalledVersion version,
                       std::string& message);
    bool AddProjectFolder(const fs::path& folder, std::string& message);
    bool RemoveProject(const fs::path& root, std::string& message);  // só da lista: a pasta fica
    bool AddVersion(const fs::path& source, std::string& message);   // copia para Versões\<versão>
    bool RemoveVersion(const std::string& version, std::string& message);  // para a Lixeira
    bool SetDefaultVersion(const std::string& version, std::string& message);

    int RunWindow(int initialTab = 0);  // abre a janela (0 Projetos, 1 Versões) e roda até fechar

private:
    void SaveConfig();
    void RefreshProjects();
    void SetStatus(std::string message, bool error);

    // Interface
    void DrawFrame();
    void DrawProjectsTab();
    void DrawProjectCard(size_t index);
    void DrawVersionsTab();
    void DrawNewProjectPopup();
    void DrawOpenWithPopup();
    void DrawRemoveVersionPopup();
    void DrawAddingVersionPopup();
    void RequestOpen(size_t index, bool chooseVersion);  // abre direto ou pergunta a versão
    void StartAddVersion(const fs::path& source);

    platform::Paths m_paths;
    std::vector<std::string> m_editorArgs;
    HubConfig m_config;
    std::vector<InstalledVersion> m_versions;
    std::vector<ProjectInfo> m_projects;

    GLFWwindow* m_window = nullptr;
    double m_lastRefresh = 0.0;
    int m_selectTab = -1;  // aba a selecionar no próximo quadro (--aba)
    std::string m_search;
    std::string m_status;
    bool m_statusError = false;

    // Novo projeto
    bool m_openNewProject = false;
    std::string m_newName;
    std::string m_newParent;  // UTF-8
    std::string m_newVersion;

    // Abrir com outra versão
    bool m_openOpenWith = false;
    fs::path m_openWithRoot;
    std::string m_openWithVersion;

    // Remover versão
    bool m_openRemoveVersion = false;
    std::string m_removeVersion;

    // Adicionar versão (cópia em segundo plano: a pasta tem centenas de MB)
    struct AddResult {
        std::optional<InstalledVersion> version;
        std::string error;
    };
    std::future<AddResult> m_adding;
    fs::path m_addingSource;
};

} // namespace hub
