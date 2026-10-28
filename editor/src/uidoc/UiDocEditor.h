#pragma once

#include "EditorContext.h"
#include "uidoc/UiDocEdit.h"

#include <motor/scene/Scene.h>
#include <motor/ui/UiLayout.h>

#include <glm/vec2.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace motor {
class Framebuffer;
class UiRenderer;
} // namespace motor

namespace editor {

// Janela "Editor de UI" (o papel do UI Builder): um documento .mui aberto com a árvore dos elementos, a paleta, a
// prévia desenhada pelo mesmo UiRenderer do jogo numa resolução escolhida, as propriedades do elemento selecionado e as
// regras das folhas .mstyle do documento. Tem desfazer próprio (o documento não é dado da cena; ver UiEditHistory) e
// grava o .mui e as folhas mudadas. Abre por Janela > Editor de UI ou com duplo clique num .mui no painel Projeto.
// A lógica sem janela fica em UiDocEdit.h; o desenho com o ImGui em UiDocEditorDraw.cpp.
class UiDocEditor {
public:
    struct Host {
        EditorContext& ctx;
        std::function<void()> filesSaved;  // gravou o .mui ou uma folha: as entidades da cena releem já
    };

    struct Resolution {
        const char* label;
        int width;
        int height;
    };
    static const std::vector<Resolution>& Resolutions();  // 16:9, 21:9, 4:3, retrato...

    enum Handle : int { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };  // lados que a alça move (bits)

    explicit UiDocEditor(Host host);
    ~UiDocEditor();
    UiDocEditor(const UiDocEditor&) = delete;
    UiDocEditor& operator=(const UiDocEditor&) = delete;

    // --- Janela ---
    void Draw();              // a cada quadro (só desenha com a janela aberta)
    void Show() { m_visible = true; m_focusRequest = true; }
    bool IsVisible() const { return m_visible; }
    bool IsFocused() const { return m_focused; }
    // Abre outro arquivo, perguntando antes se há alterações não salvas.
    void RequestOpen(const std::filesystem::path& file);
    // Fechar o editor: true se pode fechar agora; senão pergunta (Salvar/Descartar/Cancelar) e chama `then` depois.
    bool ConfirmQuit(std::function<void()> then);

    // --- Documento (a janela e o MCP) ---
    bool Open(const std::filesystem::path& file, std::string* error = nullptr);  // descarta o que não foi salvo
    void Close();
    bool Save(std::string* error = nullptr);
    bool IsOpen() const { return !m_file.empty(); }
    bool HasUnsavedChanges() const;
    const std::filesystem::path& File() const { return m_file; }
    std::string Key() const;  // "Assets/UI/Menu.mui"

    const UiEditState& State() const { return m_state; }
    UiEditState& Edit() { return m_state; }  // mudar direto; depois Changed()
    void Changed(bool sheets = false, bool assets = false);  // refaz a prévia (sheets: as folhas; assets: imagens e fontes)
    void Commit();          // o que mudou vira um passo de desfazer
    bool Undo();
    bool Redo();
    bool CanUndo() const { return m_history.CanUndo() || !m_state.SameContent(m_history.Current()); }
    bool CanRedo() const { return m_history.CanRedo(); }

    void Select(const UiPath& path);
    UiPath AddElement(motor::UiElementType type, const UiPath& parent, int index = -1);
    UiPath MoveElement(const UiPath& from, const UiPath& parent, int index);
    bool DeleteElement(const UiPath& path);
    UiPath DuplicateElement(const UiPath& path);
    // Arrasta as alças do elemento (`handles`: bits de Handle) por `delta` pixels da prévia, como na janela.
    bool ResizeElement(const UiPath& path, int handles, glm::vec2 delta);

    // --- Prévia ---
    void SetResolution(int width, int height);
    glm::ivec2 GetResolution() const { return m_resolution; }
    bool RenderPreview();  // desenha agora (exige o contexto OpenGL); false sem documento
    const motor::Framebuffer* Preview() const { return m_framebuffer.get(); }
    std::optional<UiPath> HitTest(glm::vec2 point) const;  // pixels da prévia; o elemento de cima
    bool BoxOf(const UiPath& path, motor::UiBox& out) const;  // caixa na prévia (pixels), do último desenho

private:
    enum class Pending { None, Close, Open, Quit };

    void LoadSheets(UiEditState& state);
    void RefreshPreview();
    void UpdateLayoutPaths();
    void CheckFilesOnDisk();
    void RecordFileTimes();
    void AskToSave(Pending action, std::filesystem::path file = {}, std::function<void()> then = {});
    void FinishPending(bool save);

    // UiDocEditorDraw.cpp
    void DrawToolbar();
    void DrawPalette();
    void DrawTree();
    void DrawTreeNode(motor::UiElement& element, UiPath& path);
    void DrawTreeDropTarget(const UiPath& path, bool isOpenWithChildren);
    void DrawPreview();
    void DrawPreviewHandles(const glm::vec2& origin, float zoom);
    void DrawProperties();
    void DrawStyles();
    void DrawSavePopup();
    void HandleShortcuts();

    Host m_host;
    std::filesystem::path m_file;
    UiEditState m_state;
    UiEditHistory m_history;
    uint64_t m_savedVersion = 0;
    UiEditState m_saved;  // como está no disco (para saber quais folhas gravar)

    // Prévia: uma cena só com o fundo e uma entidade com o documento (o mesmo caminho do jogo).
    motor::Scene m_scene;
    motor::EntityId m_documentEntity = 0;
    std::unique_ptr<motor::Framebuffer> m_framebuffer;
    std::unique_ptr<motor::UiRenderer> m_ui;
    std::vector<motor::UiNode> m_layout;  // do último desenho, pixels da prévia
    std::vector<UiPath> m_layoutPaths;    // caminho do elemento de cada nó (vazio: não é do documento)
    glm::ivec2 m_resolution{1920, 1080};
    int m_zoomChoice = 0;  // 0 = ajustar à janela; senão a porcentagem
    bool m_previewDirty = true;

    // Arraste de uma alça.
    UiPath m_resizePath;
    int m_resizeHandles = 0;
    motor::UiElement m_resizeStart;
    motor::UiBox m_resizeBox;  // pixels da referência
    glm::vec2 m_resizeMouse{0.0f};

    // Arquivos no disco: mudaram fora?
    std::map<std::filesystem::path, std::filesystem::file_time_type> m_fileTimes;
    std::chrono::steady_clock::time_point m_nextFileCheck{};
    bool m_changedOnDisk = false;  // com alterações não salvas: avisa em vez de reler

    // Janela.
    bool m_visible = false;
    bool m_focused = false;
    bool m_focusRequest = false;
    bool m_showDeclaredOnly = false;
    std::string m_classesBuffer;     // campo das classes enquanto se digita
    UiPath m_classesFor;
    std::map<std::pair<size_t, size_t>, std::string> m_selectorBuffers;  // (folha, regra) -> texto em edição
    std::string m_selectorError;
    std::string m_error;
    std::vector<std::function<void()>> m_deferred;  // mudanças na árvore pedidas enquanto ela é desenhada

    Pending m_pending = Pending::None;
    std::filesystem::path m_pendingFile;
    std::function<void()> m_pendingThen;
    bool m_openSavePopup = false;
};

} // namespace editor
