// Editor de UI: documento aberto, desfazer, gravação, prévia e arquivos mudados fora. O desenho da janela fica em
// UiDocEditorDraw.cpp.
#include "uidoc/UiDocEditor.h"

#include "uidoc/UiDocReload.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Log.h>
#include <motor/core/Paths.h>
#include <motor/renderer/Framebuffer.h>
#include <motor/ui/UiComponents.h>
#include <motor/ui/UiDocument.h>
#include <motor/ui/UiRenderer.h>

#include <algorithm>
#include <format>
#include <unordered_map>
#include <utility>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

namespace {
const glm::vec4 kPreviewBackground{0.16f, 0.17f, 0.19f, 1.0f};  // atrás do documento (a cena do jogo, na prévia)

void CollectPaths(const std::vector<UiElement>& list, UiPath& path, std::unordered_map<const UiElement*, UiPath>& out) {
    for (size_t i = 0; i < list.size(); ++i) {
        path.push_back(static_cast<int>(i));
        out[&list[i]] = path;
        CollectPaths(list[i].children, path, out);
        path.pop_back();
    }
}
} // namespace

const std::vector<UiDocEditor::Resolution>& UiDocEditor::Resolutions() {
    static const std::vector<Resolution> kList = {
        {"16:9 (1920x1080)", 1920, 1080},
        {"16:9 HD (1280x720)", 1280, 720},
        {"21:9 (2560x1080)", 2560, 1080},
        {"4:3 (1440x1080)", 1440, 1080},
        {"16:10 (1728x1080)", 1728, 1080},
        {"Retrato 9:16 (1080x1920)", 1080, 1920},
    };
    return kList;
}

UiDocEditor::UiDocEditor(Host host) : m_host(std::move(host)) {
    // A cena da prévia: um fundo que cobre a tela (limpa o quadro anterior) e o documento por cima.
    Entity& background = m_scene.CreateEntity("Fundo");
    UiRect& rect = background.Add<UiRect>();
    rect.anchor = UiAnchor::Stretch;
    rect.size = glm::vec2(0.0f);
    background.Add<UiImage>().color = kPreviewBackground;
    Entity& document = m_scene.CreateEntity("Documento");
    document.Add<UiDocument>().order = 1;
    m_documentEntity = document.id;
    m_history.Reset(m_state);
    m_savedVersion = m_history.Version();
}

UiDocEditor::~UiDocEditor() = default;

std::string UiDocEditor::Key() const { return m_file.empty() ? std::string() : m_host.ctx.assets.KeyForFile(m_file); }

bool UiDocEditor::HasUnsavedChanges() const {
    return IsOpen() && (m_history.Version() != m_savedVersion || !m_state.SameContent(m_history.Current()));
}

void UiDocEditor::LoadSheets(UiEditState& state) {
    state.sheets.clear();
    for (const UiFont& ref : state.doc.styles) {
        UiEditSheet sheet;
        sheet.key = ref.path.empty() ? ref.guid : ref.path;
        const fs::path file = UiAssetFile(ref.guid, ref.path, m_host.ctx.assets);
        std::string error;
        if (!file.empty() && LoadUiStyleSheet(file, sheet.sheet, &error)) {
            sheet.file = file;
        } else {
            sheet.sheet = {};
        }
        state.sheets.push_back(std::move(sheet));
    }
}

bool UiDocEditor::Open(const fs::path& file, std::string* error) {
    UiEditState state;
    if (!LoadUiDocument(file, state.doc, error)) return false;
    ResolveUiDocumentAssets(state.doc, m_host.ctx.assets);
    LoadSheets(state);
    ApplyUiEditSheets(state);
    // Reabrindo o mesmo arquivo (mudou no disco): a seleção continua se o elemento ainda existir.
    if (file.lexically_normal() == m_file && UiElementAt(state.doc.elements, m_state.selected)) state.selected = m_state.selected;
    m_file = file.lexically_normal();
    m_state = std::move(state);
    m_history.Reset(m_state);
    m_saved = m_state;
    m_savedVersion = m_history.Version();
    m_changedOnDisk = false;
    m_resizeHandles = 0;
    m_selectorBuffers.clear();
    m_classesFor = {{-1}};
    m_previewDirty = true;
    m_visible = true;
    RecordFileTimes();
    log::Info("Editor de UI: {} aberto", Key().empty() ? ToUtf8(m_file) : Key());
    return true;
}

void UiDocEditor::Close() {
    m_file.clear();
    m_state = {};
    m_saved = {};
    m_history.Reset(m_state);
    m_savedVersion = m_history.Version();
    m_fileTimes.clear();
    m_changedOnDisk = false;
    m_resizeHandles = 0;
    m_selectorBuffers.clear();
    m_previewDirty = true;
}

bool UiDocEditor::Save(std::string* error) {
    if (!IsOpen()) {
        if (error) *error = "nenhum documento aberto";
        return false;
    }
    Commit();
    if (!SaveUiDocument(m_file, m_state.doc, error)) return false;
    for (size_t i = 0; i < m_state.sheets.size(); ++i) {
        const UiEditSheet& sheet = m_state.sheets[i];
        if (sheet.file.empty()) continue;
        if (i < m_saved.sheets.size() && m_saved.sheets[i] == sheet) continue;
        if (!SaveUiStyleSheet(sheet.file, sheet.sheet, error)) return false;
        log::Info("Editor de UI: folha {} gravada", sheet.key);
    }
    m_saved = m_state;
    m_savedVersion = m_history.Version();
    m_changedOnDisk = false;
    RecordFileTimes();
    log::Info("Editor de UI: {} gravado", Key());
    if (m_host.filesSaved) m_host.filesSaved();
    return true;
}

void UiDocEditor::Changed(bool sheets, bool assets) {
    if (assets) {
        ResolveUiDocumentAssets(m_state.doc, m_host.ctx.assets);  // imagens e fontes (relê as folhas do disco)
        sheets = true;
    }
    if (sheets) {
        ApplyUiEditSheets(m_state);  // as folhas editadas no lugar das do disco
    } else {
        ComputeUiStyles(m_state.doc);
    }
    m_previewDirty = true;
}

void UiDocEditor::Commit() {
    if (IsOpen()) m_history.Commit(m_state);
}

bool UiDocEditor::Undo() {
    if (!m_history.Undo(m_state)) return false;
    m_resizeHandles = 0;
    m_selectorBuffers.clear();
    m_classesFor = {{-1}};
    Changed(true);
    return true;
}

bool UiDocEditor::Redo() {
    if (!m_history.Redo(m_state)) return false;
    m_resizeHandles = 0;
    m_selectorBuffers.clear();
    m_classesFor = {{-1}};
    Changed(true);
    return true;
}

void UiDocEditor::Select(const UiPath& path) {
    m_state.selected = UiElementAt(m_state.doc.elements, path) ? path : UiPath{};
}

UiPath UiDocEditor::AddElement(UiElementType type, const UiPath& parent, int index) {
    if (!IsOpen()) return {};
    UiElement element = NewPaletteElement(type, m_state.doc.elements);
    UiPath path = InsertUiElement(m_state.doc.elements, parent, index, std::move(element));
    if (path.empty()) return path;
    m_state.selected = path;
    Changed();
    Commit();
    return path;
}

UiPath UiDocEditor::MoveElement(const UiPath& from, const UiPath& parent, int index) {
    UiPath path = MoveUiElement(m_state.doc.elements, from, parent, index);
    if (path.empty()) return path;
    m_state.selected = path;
    Changed();
    Commit();
    return path;
}

bool UiDocEditor::DeleteElement(const UiPath& path) {
    if (!RemoveUiElement(m_state.doc.elements, path)) return false;
    if (IsUiPathPrefix(path, m_state.selected)) m_state.selected.clear();
    Changed();
    Commit();
    return true;
}

UiPath UiDocEditor::DuplicateElement(const UiPath& path) {
    UiPath copy = DuplicateUiElement(m_state.doc.elements, path);
    if (copy.empty()) return copy;
    m_state.selected = copy;
    Changed();
    Commit();
    return copy;
}

bool UiDocEditor::ResizeElement(const UiPath& path, int handles, glm::vec2 delta) {
    UiElement* element = UiElementAt(m_state.doc.elements, path);
    UiBox box;
    if (!element || handles == 0 || !BoxOf(path, box)) return false;
    const float scale = UiScale(glm::vec2(m_resolution));
    const UiBox from{box.min / scale, box.max / scale};
    UiBox to = from;
    const glm::vec2 d = delta / scale;
    if (handles & kLeft) to.min.x += d.x;
    if (handles & kRight) to.max.x += d.x;
    if (handles & kTop) to.min.y += d.y;
    if (handles & kBottom) to.max.y += d.y;
    ResizeUiElement(*element, from, to);
    Changed();
    Commit();
    return true;
}

void UiDocEditor::SetResolution(int width, int height) {
    m_resolution = {std::clamp(width, 64, 8192), std::clamp(height, 64, 8192)};
}

void UiDocEditor::RefreshPreview() {
    Entity* entity = m_scene.Find(m_documentEntity);
    if (!entity) return;
    UiDocument& component = *entity->Get<UiDocument>();
    component.document.path = Key();
    // Uma cópia nova a cada mudança: a árvore viva do componente (UiDocumentInstance) é refeita dela.
    component.document.data = IsOpen() ? std::make_shared<const UiDocumentData>(m_state.doc) : nullptr;
    m_previewDirty = false;
}

void UiDocEditor::UpdateLayoutPaths() {
    m_layoutPaths.assign(m_layout.size(), {});
    const Entity* entity = m_scene.Find(m_documentEntity);
    const UiDocumentData* tree = entity ? entity->Get<UiDocument>()->Tree() : nullptr;
    if (!tree) return;
    std::unordered_map<const UiElement*, UiPath> paths;
    UiPath path;
    CollectPaths(tree->elements, path, paths);
    for (size_t i = 0; i < m_layout.size(); ++i) {
        if (const auto it = paths.find(m_layout[i].element); it != paths.end()) m_layoutPaths[i] = it->second;
    }
}

bool UiDocEditor::RenderPreview() {
    if (!IsOpen()) return false;
    if (!m_framebuffer) m_framebuffer = std::make_unique<Framebuffer>(m_resolution.x, m_resolution.y, 1);
    if (!m_ui) m_ui = std::make_unique<UiRenderer>();
    m_framebuffer->Resize(m_resolution.x, m_resolution.y);
    if (m_previewDirty) RefreshPreview();
    m_ui->Render(m_scene, m_host.ctx.assets, m_resolution.x, m_resolution.y, m_framebuffer.get());
    m_layout = m_ui->Canvas().Layout();
    UpdateLayoutPaths();
    return true;
}

std::optional<UiPath> UiDocEditor::HitTest(glm::vec2 point) const {
    const UiNode* node = UiHitTestNode(m_scene, m_layout, point, UiHitFilter::Any);
    if (!node) return std::nullopt;
    const size_t i = static_cast<size_t>(node - m_layout.data());
    if (i >= m_layoutPaths.size() || m_layoutPaths[i].empty()) return std::nullopt;
    return m_layoutPaths[i];
}

bool UiDocEditor::BoxOf(const UiPath& path, UiBox& out) const {
    if (path.empty()) return false;
    for (size_t i = 0; i < m_layoutPaths.size(); ++i) {
        if (m_layoutPaths[i] == path) {
            out = m_layout[i].box;
            return true;
        }
    }
    return false;
}

void UiDocEditor::RecordFileTimes() {
    m_fileTimes.clear();
    std::vector<fs::path> files{m_file};
    for (const UiEditSheet& s : m_state.sheets) {
        if (!s.file.empty()) files.push_back(s.file);
    }
    for (const fs::path& f : files) {
        std::error_code ec;
        const auto t = fs::last_write_time(f, ec);
        if (!ec) m_fileTimes[f] = t;
    }
}

void UiDocEditor::CheckFilesOnDisk() {
    m_nextFileCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    bool changed = false;
    for (auto& [file, time] : m_fileTimes) {
        std::error_code ec;
        const auto t = fs::last_write_time(file, ec);
        if (!ec && t != time) {
            time = t;
            changed = true;
        }
    }
    if (!changed) return;
    if (HasUnsavedChanges()) {
        m_changedOnDisk = true;  // a janela avisa e oferece reler (descartando)
        return;
    }
    std::string error;
    const fs::path file = m_file;
    if (!Open(file, &error)) {
        log::Warn("Editor de UI: {} mudou fora e não foi relido: {}", ToUtf8(file.filename()), error);
        RecordFileTimes();
        return;
    }
    log::Info("Editor de UI: {} mudou fora do editor e foi relido", Key());
}

void UiDocEditor::RequestOpen(const fs::path& file) {
    Show();
    if (IsOpen() && file.lexically_normal() == m_file) return;
    AskToSave(Pending::Open, file);
}

bool UiDocEditor::ConfirmQuit(std::function<void()> then) {
    if (!HasUnsavedChanges()) return true;
    m_visible = true;
    AskToSave(Pending::Quit, {}, std::move(then));
    return false;
}

void UiDocEditor::AskToSave(Pending action, fs::path file, std::function<void()> then) {
    m_pending = action;
    m_pendingFile = std::move(file);
    m_pendingThen = std::move(then);
    if (HasUnsavedChanges()) {
        m_openSavePopup = true;
    } else {
        FinishPending(false);
    }
}

void UiDocEditor::FinishPending(bool save) {
    if (save) {
        std::string error;
        if (!Save(&error)) {
            m_error = "Não foi possível gravar: " + error;
            m_pending = Pending::None;
            m_pendingThen = {};
            return;
        }
    }
    const Pending action = std::exchange(m_pending, Pending::None);
    switch (action) {
        case Pending::None: break;
        case Pending::Close:
            Close();
            m_visible = false;
            break;
        case Pending::Open: {
            std::string error;
            if (!Open(m_pendingFile, &error)) m_error = std::format("Não abriu {}: {}", ToUtf8(m_pendingFile.filename()), error);
            break;
        }
        case Pending::Quit:
            Close();
            if (auto then = std::exchange(m_pendingThen, {})) then();
            break;
    }
    m_pendingThen = {};
}

} // namespace editor
