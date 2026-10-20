#pragma once

#include <motor/assets/AssetLoaders.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace motor {
class Project;
class Texture2D;
}

namespace editor {

// Painel "Projeto": navegador da pasta Assets do projeto aberto.
class ProjectPanel {
public:
    struct Callbacks {
        std::function<void()> createProject;                                  // sem projeto aberto
        std::function<void()> openProject;
        std::function<void(const std::filesystem::path&)> openScene;          // duplo clique em .mscene
        std::function<bool(const std::filesystem::path&)> createSceneFile;    // grava uma cena vazia
        std::function<void(const std::filesystem::path& from, const std::filesystem::path& to)> pathMoved;
        std::function<void(const std::filesystem::path&)> pathDeleted;
        std::function<void(const std::filesystem::path&)> addMeshToScene;     // .obj -> nova entidade
        std::function<void(const std::filesystem::path&)> applyTexture;       // textura -> entidade selecionada
        std::function<void(const std::filesystem::path&)> selectAsset;        // clique num arquivo (Inspector)
        std::function<bool(const std::filesystem::path&)> createMaterialFile; // grava um material padrão
        std::function<void(const std::filesystem::path&)> addPrefabToScene;   // .mprefab -> nova instância
        std::function<void(const std::filesystem::path&)> addModelToScene;    // .gltf/.glb/.fbx -> instância do prefab
        std::function<void(const std::filesystem::path&)> reimportModel;      // gera o prefab do modelo de novo
        // Entidade da Hierarquia solta numa pasta: vira um prefab nela.
        std::function<void(uint32_t entity, const std::filesystem::path& folder)> createPrefabFromEntity;
        std::function<void()> createScript;                                   // Criar > Novo script (pasta Scripts/)
        std::function<void(const std::filesystem::path&)> openScript;         // duplo clique em .cpp/.h: editor de código
    };

    struct ViewOptions {
        bool grid = false;
        float iconSize = 72.0f;  // lado do ícone no modo grade, em pixels
    };

    // project pode ser nulo. currentScene destaca a cena aberta na lista.
    void Draw(const motor::Project* project, const std::filesystem::path& currentScene, const Callbacks& callbacks);

    void Refresh() { m_needsRefresh = true; }
    bool IsFocused() const { return m_focused; }  // atalhos globais (Del, F2...) devem ceder ao painel

    // Arquivos soltos na janela (vindos do Explorer): no próximo frame são copiados para a pasta sob o mouse,
    // ou para a pasta aberta no painel.
    void QueueImport(std::vector<std::filesystem::path> files);

    const ViewOptions& View() const { return m_view; }
    void SetView(const ViewOptions& view) { m_view = view; }

private:
    enum class Filter { All, Scenes, Textures, Meshes, Materials, Prefabs };

    struct Entry {
        std::filesystem::path path;
        std::string name;       // sem extensão para arquivos
        std::string extension;
        std::string location;   // pasta relativa a Assets (mostrada nos resultados da busca)
        motor::AssetType type = motor::AssetType::Unknown;
        bool isDirectory = false;
        uintmax_t size = 0;
        std::filesystem::file_time_type modified{};
    };

    struct Thumbnail {
        std::shared_ptr<motor::Texture2D> texture;  // nulo se a imagem não abriu
        std::filesystem::file_time_type modified{};
    };

    void ResetFor(const motor::Project* project);
    void Rescan();
    bool PassesFilter(const Entry& e) const;
    const std::vector<std::filesystem::path>& Subfolders(const std::filesystem::path& dir);
    void SetCurrentDir(const std::filesystem::path& dir);

    // --- desenho ---
    void DrawToolbar(const Callbacks& callbacks);
    void DrawFolderTree(const std::filesystem::path& dir, const Callbacks& callbacks);
    void DrawContents(const Callbacks& callbacks);
    void DrawListRow(const Entry& entry, const Callbacks& callbacks);
    void DrawGridCell(const Entry& entry, float cellWidth, const Callbacks& callbacks);
    void DrawEntryIcon(const Entry& entry, float x, float y, float size);
    void DrawRenameField(float width, const Callbacks& callbacks);
    void ItemBehavior(const Entry& entry, bool clicked, const Callbacks& callbacks);
    void DrawItemContextMenu(const Entry& entry, const Callbacks& callbacks);
    void DrawCreateMenuItems(const Callbacks& callbacks);
    void DrawPopups(const Callbacks& callbacks);
    void HandleKeys(const Callbacks& callbacks);
    motor::Texture2D* GetThumbnail(const Entry& entry);

    // --- seleção ---
    bool IsSelected(const std::filesystem::path& path) const;
    void SelectOnly(const std::filesystem::path& path);
    void ClickSelect(const std::filesystem::path& path);  // Ctrl alterna, Shift seleciona o intervalo
    const Entry* FindEntry(const std::filesystem::path& path) const;

    // --- operações de arquivo ---
    void Activate(const Entry& entry, const Callbacks& callbacks);
    void BeginRename(const std::filesystem::path& path);
    void CommitRename(const Callbacks& callbacks);
    void CreateFolder();
    void CreateScene(const Callbacks& callbacks);
    void CreateMaterial(const Callbacks& callbacks);
    void CopySelection(bool cut);
    void Paste(const Callbacks& callbacks);
    void DuplicateSelection();
    // Copia (com nome único se já existir) ou move itens para `folder`. Seleciona o resultado.
    void CopyItems(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& folder);
    void MoveItems(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& folder,
                   const Callbacks& callbacks);
    void AcceptMoveDrop(const std::filesystem::path& folder, const Callbacks& callbacks);
    void FlushImports();
    void ShowError(std::string message);
    void ShowErrors(const std::vector<std::string>& errors);

    std::filesystem::path m_projectRoot;
    std::filesystem::path m_assetsDir;
    std::filesystem::path m_currentDir;
    std::filesystem::path m_currentScene;

    std::vector<Entry> m_entries;
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> m_subfolderCache;
    bool m_needsRefresh = true;
    bool m_revealCurrent = true;  // abre a árvore até a pasta atual no próximo frame
    double m_lastScanTime = 0.0;
    bool m_searchTruncated = false;

    std::vector<std::filesystem::path> m_selection;
    std::filesystem::path m_anchor;  // início do intervalo do Shift+clique

    std::vector<std::filesystem::path> m_clipboard;
    bool m_clipboardCut = false;
    unsigned long m_clipboardSequence = 0;  // da área de transferência do sistema quando copiamos

    std::string m_search;
    Filter m_filter = Filter::All;
    bool m_focusSearch = false;
    ViewOptions m_view;

    std::map<std::filesystem::path, Thumbnail> m_thumbnails;
    int m_thumbnailBudget = 0;  // miniaturas geradas por frame (decodificar imagem custa)

    std::vector<std::filesystem::path> m_pendingImports;
    std::filesystem::path m_hoveredFolder;  // pasta sob o mouse neste frame (alvo de arquivos soltos)

    std::filesystem::path m_renaming;
    std::string m_renameBuffer;
    bool m_renameFocus = false;

    std::vector<std::filesystem::path> m_pendingDelete;
    bool m_openDeletePopup = false;
    std::string m_error;
    bool m_openErrorPopup = false;
    bool m_focused = false;
};

} // namespace editor
