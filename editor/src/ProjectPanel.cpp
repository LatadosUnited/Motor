#include "ProjectPanel.h"

#include "EditorContext.h"
#include "FileDialog.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/core/Log.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>
#include <motor/renderer/Texture.h>
#include <motor/scene/SceneSerializer.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <format>

namespace editor {

namespace fs = std::filesystem;
using motor::AssetType;
using motor::IsInside;
using motor::PathFromUtf8;
using motor::ToUtf8;

namespace {
constexpr double kRescanInterval = 1.0;  // segundos; pega mudanças feitas fora do editor
constexpr size_t kMaxSearchResults = 1000;
constexpr int kThumbnailPixels = 128;    // lado máximo da miniatura guardada na GPU

enum class Icon { Folder, Scene, Texture, Mesh, Material, Prefab, File };

Icon IconFor(bool isDirectory, AssetType type) {
    if (isDirectory) return Icon::Folder;
    switch (type) {
        case AssetType::Scene: return Icon::Scene;
        case AssetType::Texture: return Icon::Texture;
        case AssetType::Mesh: return Icon::Mesh;
        case AssetType::Material: return Icon::Material;
        case AssetType::Prefab: return Icon::Prefab;
        case AssetType::Font: break;  // fontes da UI: ícone de arquivo
        case AssetType::Unknown: break;
    }
    return Icon::File;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool SamePath(const fs::path& a, const fs::path& b) {
    return !a.empty() && a.lexically_normal() == b.lexically_normal();
}

std::string FormatSize(uintmax_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.0f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

ImU32 Alpha(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

void DrawIcon(ImDrawList* dl, ImVec2 p, float s, Icon icon, float alpha = 1.0f) {
    auto col = [alpha](int r, int g, int b, int a = 255) { return Alpha(IM_COL32(r, g, b, a), alpha); };
    switch (icon) {
        case Icon::Folder: {
            const ImU32 c = col(222, 176, 72);
            dl->AddRectFilled({p.x, p.y + s * 0.12f}, {p.x + s * 0.45f, p.y + s * 0.32f}, c, s * 0.08f);
            dl->AddRectFilled({p.x, p.y + s * 0.25f}, {p.x + s, p.y + s * 0.88f}, c, s * 0.1f);
            break;
        }
        case Icon::Scene: {
            dl->AddRectFilled({p.x + s * 0.06f, p.y + s * 0.1f}, {p.x + s * 0.94f, p.y + s * 0.9f}, col(86, 156, 232),
                              s * 0.14f);
            dl->AddTriangleFilled({p.x + s * 0.2f, p.y + s * 0.76f}, {p.x + s * 0.45f, p.y + s * 0.34f},
                                  {p.x + s * 0.7f, p.y + s * 0.76f}, col(255, 255, 255, 210));
            break;
        }
        case Icon::Texture: {
            dl->AddRectFilled({p.x + s * 0.06f, p.y + s * 0.12f}, {p.x + s * 0.94f, p.y + s * 0.88f}, col(70, 150, 110),
                              s * 0.1f);
            dl->AddTriangleFilled({p.x + s * 0.14f, p.y + s * 0.8f}, {p.x + s * 0.4f, p.y + s * 0.42f},
                                  {p.x + s * 0.62f, p.y + s * 0.8f}, col(230, 240, 230));
            dl->AddTriangleFilled({p.x + s * 0.48f, p.y + s * 0.8f}, {p.x + s * 0.66f, p.y + s * 0.56f},
                                  {p.x + s * 0.86f, p.y + s * 0.8f}, col(190, 215, 195));
            dl->AddCircleFilled({p.x + s * 0.72f, p.y + s * 0.3f}, s * 0.09f, col(250, 220, 110));
            break;
        }
        case Icon::Mesh: {
            // Cubo em perspectiva isométrica: três faces com tons diferentes.
            const ImVec2 top{p.x + s * 0.5f, p.y + s * 0.08f}, left{p.x + s * 0.1f, p.y + s * 0.3f},
                right{p.x + s * 0.9f, p.y + s * 0.3f}, center{p.x + s * 0.5f, p.y + s * 0.52f},
                bottomLeft{p.x + s * 0.1f, p.y + s * 0.72f}, bottomRight{p.x + s * 0.9f, p.y + s * 0.72f},
                bottom{p.x + s * 0.5f, p.y + s * 0.94f};
            dl->AddQuadFilled(top, right, center, left, col(200, 150, 235));
            dl->AddQuadFilled(left, center, bottom, bottomLeft, col(150, 100, 200));
            dl->AddQuadFilled(center, right, bottomRight, bottom, col(120, 75, 170));
            break;
        }
        case Icon::Material: {
            // Esfera com brilho: o "material" de quase todo editor.
            const ImVec2 c{p.x + s * 0.5f, p.y + s * 0.5f};
            dl->AddCircleFilled(c, s * 0.42f, col(214, 92, 120));
            dl->AddCircleFilled({c.x + s * 0.06f, c.y + s * 0.06f}, s * 0.3f, col(180, 60, 92));
            dl->AddCircleFilled({c.x - s * 0.15f, c.y - s * 0.15f}, s * 0.1f, col(255, 225, 235));
            break;
        }
        case Icon::Prefab: {
            // Cubo como o da malha, em azul (mesma cor das instâncias na Hierarquia).
            const ImVec2 top{p.x + s * 0.5f, p.y + s * 0.08f}, left{p.x + s * 0.1f, p.y + s * 0.3f},
                right{p.x + s * 0.9f, p.y + s * 0.3f}, center{p.x + s * 0.5f, p.y + s * 0.52f},
                bottomLeft{p.x + s * 0.1f, p.y + s * 0.72f}, bottomRight{p.x + s * 0.9f, p.y + s * 0.72f},
                bottom{p.x + s * 0.5f, p.y + s * 0.94f};
            dl->AddQuadFilled(top, right, center, left, col(140, 200, 255));
            dl->AddQuadFilled(left, center, bottom, bottomLeft, col(80, 150, 230));
            dl->AddQuadFilled(center, right, bottomRight, bottom, col(55, 115, 200));
            break;
        }
        case Icon::File: {
            dl->AddRectFilled({p.x + s * 0.18f, p.y + s * 0.06f}, {p.x + s * 0.82f, p.y + s * 0.94f}, col(160, 165, 175),
                              s * 0.08f);
            dl->AddTriangleFilled({p.x + s * 0.6f, p.y + s * 0.06f}, {p.x + s * 0.82f, p.y + s * 0.06f},
                                  {p.x + s * 0.82f, p.y + s * 0.28f}, col(110, 115, 125));
            break;
        }
    }
}

fs::path UniquePath(const fs::path& dir, const std::string& base, const std::string& ext) {
    std::error_code ec;
    fs::path p = dir / PathFromUtf8(base + ext);
    for (int i = 2; fs::exists(p, ec); ++i) p = dir / PathFromUtf8(std::format("{} {}{}", base, i, ext));
    return p;
}

// Destino para copiar `source` dentro de `folder` sem sobrescrever nada: "nome", "nome 2", "nome 3"...
fs::path UniqueDestination(const fs::path& source, const fs::path& folder) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) return UniquePath(folder, ToUtf8(source.filename()), "");
    return UniquePath(folder, ToUtf8(source.stem()), ToUtf8(source.extension()));
}

// Depois de renomear/mover um arquivo, leva o .meta junto para o asset manter o GUID.
// (Pastas não precisam: os .meta de dentro vão com elas.)
void MoveMetaAlong(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    const fs::path meta = motor::AssetDatabase::MetaPathFor(from);
    if (fs::is_directory(to, ec) || !fs::exists(meta, ec)) return;
    fs::rename(meta, motor::AssetDatabase::MetaPathFor(to), ec);
    if (ec) motor::log::Warn("Não foi possível mover {}: {}", ToUtf8(meta), ec.message());
}

// Corta o texto com "..." para caber em `width` (sem partir caracteres UTF-8 ao meio).
std::string Ellipsize(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string cut = text;
    while (!cut.empty()) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
        if (ImGui::CalcTextSize((cut + "...").c_str()).x <= width) break;
    }
    return cut + "...";
}

// Reduz a imagem (média de blocos) para caber num quadrado de `maxSide`.
motor::ImageData Downscale(const motor::ImageData& src, int maxSide) {
    const int largest = std::max(src.width, src.height);
    if (largest <= maxSide) return src;
    motor::ImageData dst;
    dst.width = std::max(1, src.width * maxSide / largest);
    dst.height = std::max(1, src.height * maxSide / largest);
    dst.pixels.resize(static_cast<size_t>(dst.width * dst.height * 4));
    for (int y = 0; y < dst.height; ++y) {
        const int y0 = y * src.height / dst.height, y1 = std::max(y0 + 1, (y + 1) * src.height / dst.height);
        for (int x = 0; x < dst.width; ++x) {
            const int x0 = x * src.width / dst.width, x1 = std::max(x0 + 1, (x + 1) * src.width / dst.width);
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = &src.pixels[static_cast<size_t>((sy * src.width + sx) * 4)];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
            }
            const auto count = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            uint8_t* out = &dst.pixels[static_cast<size_t>((y * dst.width + x) * 4)];
            for (int c = 0; c < 4; ++c) out[c] = static_cast<uint8_t>(sum[c] / count);
        }
    }
    return dst;
}

const char* FilterName(int filter) {
    static const char* names[] = {"Todos", "Cenas", "Texturas", "Malhas", "Materiais", "Prefabs"};
    return names[filter];
}
} // namespace

// ---------------------------------------------------------------------------

void ProjectPanel::Draw(const motor::Project* project, const fs::path& currentScene, const Callbacks& callbacks) {
    m_focused = false;
    m_hoveredFolder.clear();
    m_thumbnailBudget = 1;

    if (!project) m_projectRoot.clear();
    else if (!SamePath(project->RootDir(), m_projectRoot)) ResetFor(project);

    if (!ImGui::Begin("Projeto")) {
        ImGui::End();
        FlushImports();
        return;
    }
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (!project) {
        ImGui::Spacing();
        ImGui::TextDisabled("Nenhum projeto aberto.");
        ImGui::Spacing();
        if (ImGui::Button("Criar projeto...")) callbacks.createProject();
        ImGui::SameLine();
        if (ImGui::Button("Abrir projeto...")) callbacks.openProject();
        FlushImports();
        DrawPopups(callbacks);
        ImGui::End();
        return;
    }

    m_currentScene = currentScene;
    if (m_needsRefresh || ImGui::GetTime() - m_lastScanTime > kRescanInterval) Rescan();

    DrawToolbar(callbacks);
    ImGui::Separator();

    const ImGuiTableFlags layoutFlags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##project_layout", 2, layoutFlags)) {
        ImGui::TableSetupColumn("pastas", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("conteudo", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##tree")) {
            DrawFolderTree(m_assetsDir, callbacks);
            m_revealCurrent = false;
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        // Ctrl+roda muda o tamanho dos ícones na grade em vez de rolar.
        const bool zooming = m_view.grid && ImGui::GetIO().KeyCtrl;
        if (ImGui::BeginChild("##contents", ImVec2(0, 0), ImGuiChildFlags_None,
                              zooming ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None)) {
            if (zooming && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                m_view.iconSize = std::clamp(m_view.iconSize + ImGui::GetIO().MouseWheel * 8.0f, 40.0f, 160.0f);
            }
            DrawContents(callbacks);
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }

    HandleKeys(callbacks);
    FlushImports();
    DrawPopups(callbacks);
    ImGui::End();
}

void ProjectPanel::QueueImport(std::vector<fs::path> files) {
    m_pendingImports.insert(m_pendingImports.end(), files.begin(), files.end());
}

void ProjectPanel::FlushImports() {
    if (m_pendingImports.empty()) return;
    const std::vector<fs::path> files = std::exchange(m_pendingImports, {});
    if (m_projectRoot.empty()) {
        ShowError("Abra ou crie um projeto para importar arquivos.");
        return;
    }
    const fs::path target = m_hoveredFolder.empty() ? m_currentDir : m_hoveredFolder;
    CopyItems(files, target);
}

void ProjectPanel::ResetFor(const motor::Project* project) {
    m_projectRoot = project->RootDir();
    m_assetsDir = project->AssetsDir().lexically_normal();
    m_currentDir = m_assetsDir;
    m_selection.clear();
    m_anchor.clear();
    m_clipboard.clear();
    m_renaming.clear();
    m_pendingDelete.clear();
    m_search.clear();
    m_thumbnails.clear();
    m_needsRefresh = true;
    m_revealCurrent = true;
}

bool ProjectPanel::PassesFilter(const Entry& e) const {
    switch (m_filter) {
        case Filter::Scenes: return !e.isDirectory && e.type == AssetType::Scene;
        case Filter::Textures: return !e.isDirectory && e.type == AssetType::Texture;
        case Filter::Meshes: return !e.isDirectory && e.type == AssetType::Mesh;
        case Filter::Materials: return !e.isDirectory && e.type == AssetType::Material;
        case Filter::Prefabs: return !e.isDirectory && e.type == AssetType::Prefab;
        case Filter::All: break;
    }
    return true;
}

void ProjectPanel::Rescan() {
    m_needsRefresh = false;
    m_lastScanTime = ImGui::GetTime();
    m_subfolderCache.clear();
    m_searchTruncated = false;

    std::error_code ec;
    if (!fs::is_directory(m_currentDir, ec) || !IsInside(m_currentDir, m_assetsDir)) m_currentDir = m_assetsDir;

    auto makeEntry = [&](const fs::directory_entry& de, Entry& e) {
        e.path = de.path();
        const std::string filename = ToUtf8(e.path.filename());
        if (filename.empty() || filename[0] == '.') return false;  // ocultos
        e.isDirectory = de.is_directory(ec);
        if (e.isDirectory) {
            e.name = filename;
        } else {
            e.extension = ToUtf8(e.path.extension());
            // Temporário do salvamento atômico e .meta (o GUID do asset) não aparecem: o .meta acompanha o arquivo.
            if (e.extension == ".tmp" || motor::AssetDatabase::IsMetaFile(e.path)) return false;
            e.name = ToUtf8(e.path.stem());
            e.type = motor::AssetTypeFromPath(e.path);
            e.size = de.file_size(ec);
            e.modified = de.last_write_time(ec);
        }
        return true;
    };

    m_entries.clear();
    if (m_search.empty()) {
        for (const auto& de : fs::directory_iterator(m_currentDir, fs::directory_options::skip_permission_denied, ec)) {
            Entry e;
            if (!makeEntry(de, e)) continue;
            if (e.isDirectory || PassesFilter(e)) m_entries.push_back(std::move(e));
        }
    } else {
        // Busca em todo o Assets, pelo nome (sem diferenciar maiúsculas).
        const std::string needle = Lower(m_search);
        for (auto it = fs::recursive_directory_iterator(m_assetsDir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            Entry e;
            if (!makeEntry(*it, e)) {
                if (it->is_directory(ec)) it.disable_recursion_pending();  // não entra em pastas ocultas
                continue;
            }
            if (Lower(ToUtf8(e.path.filename())).find(needle) == std::string::npos || !PassesFilter(e)) continue;
            const fs::path parent = e.path.parent_path().lexically_relative(m_assetsDir);
            e.location = parent == "." ? std::string() : motor::ToGenericUtf8(parent);
            m_entries.push_back(std::move(e));
            if (m_entries.size() >= kMaxSearchResults) {
                m_searchTruncated = true;
                break;
            }
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        const std::string la = Lower(a.name), lb = Lower(b.name);
        if (la != lb) return la < lb;
        return a.extension != b.extension ? a.extension < b.extension : a.location < b.location;
    });

    std::erase_if(m_selection, [&](const fs::path& p) { return !fs::exists(p, ec); });
}

const std::vector<fs::path>& ProjectPanel::Subfolders(const fs::path& dir) {
    auto it = m_subfolderCache.find(dir);
    if (it != m_subfolderCache.end()) return it->second;

    std::vector<fs::path> result;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        const std::string name = ToUtf8(de.path().filename());
        if (de.is_directory(ec) && !name.empty() && name[0] != '.') result.push_back(de.path());
    }
    std::sort(result.begin(), result.end(), [](const fs::path& a, const fs::path& b) {
        return Lower(ToUtf8(a.filename())) < Lower(ToUtf8(b.filename()));
    });
    return m_subfolderCache.emplace(dir, std::move(result)).first->second;
}

void ProjectPanel::SetCurrentDir(const fs::path& dir) {
    if (!IsInside(dir, m_assetsDir)) return;
    m_currentDir = dir.lexically_normal();
    m_search.clear();  // navegar sai da busca
    m_selection.clear();
    m_renaming.clear();
    m_needsRefresh = true;
    m_revealCurrent = true;
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------

void ProjectPanel::DrawToolbar(const Callbacks& callbacks) {
    const bool atRoot = SamePath(m_currentDir, m_assetsDir);
    ImGui::BeginDisabled(atRoot && m_search.empty());
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) SetCurrentDir(atRoot ? m_currentDir : m_currentDir.parent_path());
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Pasta acima (Backspace)");

    // Trilha de navegação: Assets > Scenes > ... (cada parte é clicável e aceita arquivos arrastados).
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    auto crumb = [&](const std::string& label, const fs::path& target, int id) {
        ImGui::PushID(id);
        if (ImGui::Button(label.c_str())) SetCurrentDir(target);
        AcceptMoveDrop(target, callbacks);
        ImGui::PopID();
    };
    ImGui::SameLine();
    if (!m_search.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Busca em %s", motor::Project::kAssetsFolder);
    } else {
        crumb(motor::Project::kAssetsFolder, m_assetsDir, 0);
        fs::path accum = m_assetsDir;
        int id = 1;
        for (const fs::path& part : m_currentDir.lexically_relative(m_assetsDir)) {
            if (part == ".") continue;
            accum /= part;
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0f, 2.0f);
            crumb(ToUtf8(part), accum, id++);
        }
    }
    ImGui::PopStyleColor();

    // À direita: busca, filtro de tipo, modo de exibição e "+ Criar".
    const ImGuiStyle& style = ImGui::GetStyle();
    const float searchWidth = ImGui::GetFontSize() * 11.0f;
    const float filterWidth = ImGui::GetFontSize() * 6.5f;
    const float buttonsWidth = ImGui::CalcTextSize("Ver").x + ImGui::CalcTextSize("+ Criar").x + style.FramePadding.x * 4.0f;
    const float rightWidth = searchWidth + filterWidth + buttonsWidth + style.ItemSpacing.x * 3.0f;
    ImGui::SameLine();
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > rightWidth) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - rightWidth);

    if (m_focusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_focusSearch = false;
    }
    ImGui::SetNextItemWidth(searchWidth);
    if (ImGui::InputTextWithHint("##search", "Buscar (Ctrl+F)", &m_search, ImGuiInputTextFlags_EscapeClearsAll)) {
        m_selection.clear();
        Refresh();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(filterWidth);
    if (ImGui::BeginCombo("##filter", FilterName(static_cast<int>(m_filter)))) {
        for (int f = 0; f <= static_cast<int>(Filter::Prefabs); ++f) {
            if (ImGui::Selectable(FilterName(f), static_cast<int>(m_filter) == f)) {
                m_filter = static_cast<Filter>(f);
                Refresh();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Mostrar só um tipo de arquivo");

    ImGui::SameLine();
    if (ImGui::Button("Ver")) ImGui::OpenPopup("##project_view");
    if (ImGui::BeginPopup("##project_view")) {
        if (ImGui::RadioButton("Lista", !m_view.grid)) m_view.grid = false;
        if (ImGui::RadioButton("Grade", m_view.grid)) m_view.grid = true;
        ImGui::BeginDisabled(!m_view.grid);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("Tamanho", &m_view.iconSize, 40.0f, 160.0f, "%.0f px");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Ctrl + roda do mouse também muda o tamanho.");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("+ Criar")) ImGui::OpenPopup("##project_create");
    if (ImGui::BeginPopup("##project_create")) {
        DrawCreateMenuItems(callbacks);
        ImGui::EndPopup();
    }
}

void ProjectPanel::DrawFolderTree(const fs::path& dir, const Callbacks& callbacks) {
    const std::vector<fs::path> subfolders = Subfolders(dir);  // cópia: o cache pode mudar durante a recursão

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (subfolders.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (SamePath(dir, m_currentDir) && m_search.empty()) flags |= ImGuiTreeNodeFlags_Selected;
    if (SamePath(dir, m_assetsDir)) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    // Ao navegar pela lista, abre o caminho até a pasta atual.
    if (m_revealCurrent && !SamePath(dir, m_currentDir) && IsInside(m_currentDir, dir)) ImGui::SetNextItemOpen(true);

    const std::string name = SamePath(dir, m_assetsDir) ? motor::Project::kAssetsFolder : ToUtf8(dir.filename());
    ImGui::PushID(ToUtf8(dir).c_str());
    // Espaços no rótulo reservam lugar para o ícone desenhado por cima.
    const bool open = ImGui::TreeNodeEx("##folder", flags, "      %s", name.c_str());
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const float iconSize = ImGui::GetFontSize() * 0.9f;
    DrawIcon(ImGui::GetWindowDrawList(),
             ImVec2(itemMin.x + ImGui::GetTreeNodeToLabelSpacing(),
                    itemMin.y + (ImGui::GetItemRectSize().y - iconSize) * 0.5f),
             iconSize, Icon::Folder);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) SetCurrentDir(dir);
    if (ImGui::IsItemHovered()) m_hoveredFolder = dir;
    AcceptMoveDrop(dir, callbacks);
    ImGui::PopID();

    if (open) {
        for (const fs::path& sub : subfolders) DrawFolderTree(sub, callbacks);
        ImGui::TreePop();
    }
}

void ProjectPanel::DrawContents(const Callbacks& callbacks) {
    const bool searching = !m_search.empty();
    if (searching) {
        ImGui::TextDisabled(m_searchTruncated ? "Mais de %zu resultados (mostrando os primeiros)" : "%zu resultado(s)",
                            m_entries.size());
    } else if (m_entries.empty()) {
        ImGui::TextDisabled(m_filter == Filter::All ? "Pasta vazia. Clique com o botão direito para criar algo, ou "
                                                      "arraste arquivos do Explorer para cá."
                                                    : "Nenhum arquivo desse tipo nesta pasta.");
    }

    if (m_view.grid) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float cellWidth = m_view.iconSize + style.FramePadding.x * 2.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (cellWidth + style.ItemSpacing.x)));
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (i % static_cast<size_t>(columns) != 0) ImGui::SameLine();
            DrawGridCell(m_entries[i], cellWidth, callbacks);
        }
    } else {
        const int columnCount = searching ? 4 : 3;
        if (ImGui::BeginTable("##entries", columnCount, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
            if (searching) ImGui::TableSetupColumn("Pasta", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Arquivo .xxxx").x);
            ImGui::TableSetupColumn("Tamanho", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.0 MB").x);
            for (const Entry& e : m_entries) DrawListRow(e, callbacks);
            ImGui::EndTable();
        }
    }

    // Área vazia: clique limpa a seleção; botão direito cria itens; aceita itens arrastados (move para a pasta atual).
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##contents_background", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 30.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) m_selection.clear();
    if (!searching) AcceptMoveDrop(m_currentDir, callbacks);
    if (ImGui::BeginPopupContextItem("##contents_ctx")) {
        DrawCreateMenuItems(callbacks);
        ImGui::Separator();
        if (ImGui::MenuItem("Colar", "Ctrl+V")) Paste(callbacks);
        if (ImGui::MenuItem("Selecionar tudo", "Ctrl+A")) {
            m_selection.clear();
            for (const Entry& e : m_entries) m_selection.push_back(e.path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Mostrar no Explorer")) Shell::RevealInExplorer(m_currentDir);
        if (ImGui::MenuItem("Atualizar")) Refresh();
        ImGui::EndPopup();
    }
}

void ProjectPanel::DrawEntryIcon(const Entry& e, float x, float y, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool cut = m_clipboardCut && std::any_of(m_clipboard.begin(), m_clipboard.end(),
                                                   [&](const fs::path& p) { return SamePath(p, e.path); });
    const float alpha = cut ? 0.45f : 1.0f;

    if (motor::Texture2D* thumb = GetThumbnail(e)) {
        // Miniatura com a proporção da imagem, centralizada no quadrado.
        const float aspect = static_cast<float>(thumb->Width()) / static_cast<float>(thumb->Height());
        const ImVec2 extent = aspect >= 1.0f ? ImVec2(size, size / aspect) : ImVec2(size * aspect, size);
        const ImVec2 p0{x + (size - extent.x) * 0.5f, y + (size - extent.y) * 0.5f};
        const auto id = static_cast<ImTextureID>(static_cast<uintptr_t>(thumb->NativeHandle()));
        dl->AddRectFilled(p0, ImVec2(p0.x + extent.x, p0.y + extent.y), IM_COL32(40, 40, 46, 255));
        dl->AddImage(ImTextureRef(id), p0, ImVec2(p0.x + extent.x, p0.y + extent.y), ImVec2(0, 1), ImVec2(1, 0),
                     Alpha(IM_COL32_WHITE, alpha));
        return;
    }
    DrawIcon(dl, ImVec2(x, y), size, IconFor(e.isDirectory, e.type), alpha);
}

void ProjectPanel::DrawRenameField(float width, const Callbacks& callbacks) {
    ImGui::SetNextItemWidth(width);
    if (m_renameFocus) {
        ImGui::SetKeyboardFocusHere();
        m_renameFocus = false;
    }
    const bool commit = ImGui::InputText("##rename", &m_renameBuffer,
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (commit) {
        CommitRename(callbacks);
    } else if (ImGui::IsItemDeactivated()) {
        m_renaming.clear();  // Esc ou clique fora cancela
    }
}

void ProjectPanel::DrawListRow(const Entry& e, const Callbacks& callbacks) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(ToUtf8(e.path).c_str());

    const float rowHeight = ImGui::GetFrameHeight();
    const float iconSize = ImGui::GetFontSize();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconY = start.y + (rowHeight - iconSize) * 0.5f;
    const float textX = start.x + iconSize + 8.0f;

    if (SamePath(m_renaming, e.path)) {
        DrawEntryIcon(e, start.x + 2.0f, iconY, iconSize);
        ImGui::SetCursorScreenPos(ImVec2(textX, start.y));
        DrawRenameField(-FLT_MIN, callbacks);
    } else {
        const bool clicked = ImGui::Selectable("##item", IsSelected(e.path),
                                               ImGuiSelectableFlags_AllowDoubleClick |
                                                   ImGuiSelectableFlags_SpanAllColumns,
                                               ImVec2(0.0f, rowHeight));
        ItemBehavior(e, clicked, callbacks);

        DrawEntryIcon(e, start.x + 2.0f, iconY, iconSize);
        const bool isOpenScene = !e.isDirectory && SamePath(m_currentScene, e.path);
        const std::string label = isOpenScene ? e.name + "  (aberta)" : e.name;
        const ImU32 color = isOpenScene ? IM_COL32(120, 190, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::GetWindowDrawList()->AddText(ImVec2(textX, start.y + (rowHeight - ImGui::GetFontSize()) * 0.5f), color,
                                            label.c_str());
    }

    if (!m_search.empty()) {
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", e.location.empty() ? motor::Project::kAssetsFolder : e.location.c_str());
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (e.isDirectory) {
        ImGui::TextDisabled("Pasta");
    } else if (e.type != AssetType::Unknown) {
        ImGui::TextDisabled("%s", motor::AssetTypeName(e.type));
    } else {
        ImGui::TextDisabled("Arquivo %s", e.extension.c_str());
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (!e.isDirectory) ImGui::TextDisabled("%s", FormatSize(e.size).c_str());

    ImGui::PopID();
}

void ProjectPanel::DrawGridCell(const Entry& e, float cellWidth, const Callbacks& callbacks) {
    ImGui::PushID(ToUtf8(e.path).c_str());
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconSize = m_view.iconSize;
    const float labelHeight = ImGui::GetTextLineHeight();
    const float cellHeight = style.FramePadding.y * 3.0f + iconSize + labelHeight;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconX = start.x + (cellWidth - iconSize) * 0.5f;
    const float iconY = start.y + style.FramePadding.y;
    const float labelY = iconY + iconSize + style.FramePadding.y;

    if (SamePath(m_renaming, e.path)) {
        ImGui::BeginGroup();
        DrawEntryIcon(e, iconX, iconY, iconSize);
        ImGui::Dummy(ImVec2(cellWidth, labelY - start.y - style.ItemSpacing.y));
        DrawRenameField(cellWidth, callbacks);
        ImGui::EndGroup();
    } else {
        const bool clicked = ImGui::Selectable("##cell", IsSelected(e.path), ImGuiSelectableFlags_AllowDoubleClick,
                                               ImVec2(cellWidth, cellHeight));
        ItemBehavior(e, clicked, callbacks);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s%s\n%s", e.name.c_str(), e.extension.c_str(),
                              e.isDirectory ? "Pasta" : motor::AssetTypeName(e.type));
        }

        DrawEntryIcon(e, iconX, iconY, iconSize);
        const bool isOpenScene = !e.isDirectory && SamePath(m_currentScene, e.path);
        const std::string label = Ellipsize(e.name, cellWidth - 4.0f);
        const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(start.x + (cellWidth - labelWidth) * 0.5f, labelY),
            isOpenScene ? IM_COL32(120, 190, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    }
    ImGui::PopID();
}

void ProjectPanel::ItemBehavior(const Entry& e, bool clicked, const Callbacks& callbacks) {
    if (clicked) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            SelectOnly(e.path);
            Activate(e, callbacks);
            return;  // Activate pode trocar de pasta e invalidar a lista
        }
        ClickSelect(e.path);
        if (callbacks.selectAsset && m_selection.size() == 1) callbacks.selectAsset(e.path);
    }
    if (e.isDirectory && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) m_hoveredFolder = e.path;

    if (ImGui::BeginDragDropSource()) {
        // Arrastar um item selecionado leva a seleção inteira.
        const std::vector<fs::path> dragged = IsSelected(e.path) ? m_selection : std::vector<fs::path>{e.path};
        SetPathDragPayload(dragged);
        if (dragged.size() == 1) {
            ImGui::TextUnformatted(e.name.c_str());
        } else {
            ImGui::Text("%zu itens", dragged.size());
        }
        ImGui::EndDragDropSource();
    }
    if (e.isDirectory) AcceptMoveDrop(e.path, callbacks);

    DrawItemContextMenu(e, callbacks);
}

void ProjectPanel::DrawItemContextMenu(const Entry& e, const Callbacks& callbacks) {
    if (!ImGui::BeginPopupContextItem("##item_ctx")) return;
    if (!IsSelected(e.path)) SelectOnly(e.path);
    const bool single = m_selection.size() == 1;

    if (single) {
        if (e.isDirectory && ImGui::MenuItem("Abrir pasta", "Enter")) Activate(e, callbacks);
        if (e.type == AssetType::Scene && ImGui::MenuItem("Abrir cena", "Enter")) Activate(e, callbacks);
        if (e.type == AssetType::Mesh && callbacks.addMeshToScene && ImGui::MenuItem("Adicionar à cena")) {
            callbacks.addMeshToScene(e.path);
        }
        if (e.type == AssetType::Texture && callbacks.applyTexture &&
            ImGui::MenuItem("Aplicar à entidade selecionada")) {
            callbacks.applyTexture(e.path);
        }
        if (e.type == AssetType::Prefab && callbacks.addPrefabToScene && ImGui::MenuItem("Adicionar à cena")) {
            callbacks.addPrefabToScene(e.path);
        }
        const bool inspectable = e.type == AssetType::Material || e.type == AssetType::Prefab;
        if (inspectable && callbacks.selectAsset && ImGui::MenuItem("Editar no Inspector", "Enter")) {
            callbacks.selectAsset(e.path);
        }
        if (!e.isDirectory && e.type != AssetType::Scene && !inspectable &&
            ImGui::MenuItem("Abrir com programa padrão", "Enter")) {
            Shell::OpenWithDefaultApp(e.path);
        }
        if (!m_search.empty() && ImGui::MenuItem("Mostrar na pasta")) {
            const fs::path path = e.path;
            SetCurrentDir(path.parent_path());
            SelectOnly(path);
        }
        ImGui::Separator();
    }
    if (ImGui::MenuItem("Recortar", "Ctrl+X")) CopySelection(true);
    if (ImGui::MenuItem("Copiar", "Ctrl+C")) CopySelection(false);
    if (ImGui::MenuItem("Colar", "Ctrl+V")) Paste(callbacks);
    if (ImGui::MenuItem("Duplicar", "Ctrl+D")) DuplicateSelection();
    ImGui::Separator();
    if (single && ImGui::MenuItem("Renomear", "F2")) BeginRename(e.path);
    if (ImGui::MenuItem(single ? "Excluir" : "Excluir selecionados", "Del")) {
        m_pendingDelete = m_selection;
        m_openDeletePopup = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Mostrar no Explorer")) Shell::RevealInExplorer(e.path);
    ImGui::EndPopup();
}

void ProjectPanel::DrawCreateMenuItems(const Callbacks& callbacks) {
    if (ImGui::MenuItem("Nova pasta")) CreateFolder();
    if (ImGui::MenuItem("Nova cena")) CreateScene(callbacks);
    if (ImGui::MenuItem("Novo material")) CreateMaterial(callbacks);
    ImGui::TextDisabled("Prefab: arraste uma entidade da Hierarquia para uma pasta");
}

void ProjectPanel::DrawPopups(const Callbacks& callbacks) {
    const char* deleteTitle = "Excluir##projeto";
    const char* errorTitle = "Erro##projeto";
    if (m_openDeletePopup) {
        ImGui::OpenPopup(deleteTitle);
        m_openDeletePopup = false;
    }
    if (m_openErrorPopup) {
        ImGui::OpenPopup(errorTitle);
        m_openErrorPopup = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 buttonSize{110.0f, 0.0f};

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(deleteTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::error_code ec;
        const bool anyDir = std::any_of(m_pendingDelete.begin(), m_pendingDelete.end(),
                                        [&](const fs::path& p) { return fs::is_directory(p, ec); });
        if (m_pendingDelete.size() == 1) {
            ImGui::Text("Enviar \"%s\" para a Lixeira?", ToUtf8(m_pendingDelete.front().filename()).c_str());
        } else {
            ImGui::Text("Enviar %zu itens para a Lixeira?", m_pendingDelete.size());
        }
        if (anyDir) ImGui::TextDisabled("Pastas são movidas com todo o conteúdo.");
        ImGui::Spacing();

        if (ImGui::Button("Excluir", buttonSize)) {
            std::vector<std::string> errors;
            for (const fs::path& path : m_pendingDelete) {
                std::string error;
                if (!Shell::MoveToTrash(path, &error)) {
                    errors.push_back(std::move(error));
                    continue;
                }
                motor::log::Info("Enviado para a Lixeira: {}", ToUtf8(path));
                if (const fs::path meta = motor::AssetDatabase::MetaPathFor(path); fs::exists(meta, ec)) {
                    Shell::MoveToTrash(meta);
                }
                if (IsInside(m_currentDir, path)) m_currentDir = path.parent_path();
                if (callbacks.pathDeleted) callbacks.pathDeleted(path);
            }
            m_selection.clear();
            m_pendingDelete.clear();
            ShowErrors(errors);
            Refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_pendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(errorTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(m_error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProjectPanel::HandleKeys(const Callbacks& callbacks) {
    if (!m_focused || !m_renaming.empty() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_focusSearch = true;
    if (io.WantTextInput) return;  // digitando na busca

    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            m_selection.clear();
            for (const Entry& e : m_entries) m_selection.push_back(e.path);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste(callbacks);
        if (m_selection.empty()) return;
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection(false);
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) CopySelection(true);
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelection();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && m_search.empty() && !SamePath(m_currentDir, m_assetsDir)) {
        SetCurrentDir(m_currentDir.parent_path());
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_selection.clear();
    if (m_selection.empty()) return;

    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) BeginRename(m_selection.front());
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        m_pendingDelete = m_selection;
        m_openDeletePopup = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        if (const Entry* e = FindEntry(m_selection.front())) {
            const Entry copy = *e;  // Activate pode reconstruir a lista
            Activate(copy, callbacks);
        }
    }
}

motor::Texture2D* ProjectPanel::GetThumbnail(const Entry& e) {
    if (e.isDirectory || e.type != AssetType::Texture) return nullptr;

    auto it = m_thumbnails.find(e.path);
    if (it != m_thumbnails.end() && it->second.modified == e.modified) return it->second.texture.get();
    if (m_thumbnailBudget <= 0) return it != m_thumbnails.end() ? it->second.texture.get() : nullptr;
    --m_thumbnailBudget;

    // Miniatura em espaço linear (RGBA8): o ImGui desenha os bytes como estão, sem conversão sRGB.
    Thumbnail thumb;
    thumb.modified = e.modified;
    motor::ImageData image;
    std::string error;
    if (motor::LoadImageFile(e.path, image, &error)) {
        const motor::ImageData small = Downscale(image, kThumbnailPixels);
        thumb.texture = std::make_shared<motor::Texture2D>(small.width, small.height, small.pixels.data(),
                                                           motor::TextureFilter::Linear,
                                                           motor::TextureColorSpace::Linear);
    } else {
        motor::log::Warn("Miniatura: {}", error);
    }
    return (m_thumbnails[e.path] = std::move(thumb)).texture.get();
}

// ---------------------------------------------------------------------------
// Seleção
// ---------------------------------------------------------------------------

bool ProjectPanel::IsSelected(const fs::path& path) const {
    return std::any_of(m_selection.begin(), m_selection.end(), [&](const fs::path& p) { return SamePath(p, path); });
}

void ProjectPanel::SelectOnly(const fs::path& path) {
    m_selection = {path};
    m_anchor = path;
}

void ProjectPanel::ClickSelect(const fs::path& path) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && !m_anchor.empty()) {
        auto indexOf = [&](const fs::path& p) {
            for (size_t i = 0; i < m_entries.size(); ++i) {
                if (SamePath(m_entries[i].path, p)) return static_cast<ptrdiff_t>(i);
            }
            return ptrdiff_t{-1};
        };
        const ptrdiff_t a = indexOf(m_anchor), b = indexOf(path);
        if (a >= 0 && b >= 0) {
            if (!io.KeyCtrl) m_selection.clear();
            for (ptrdiff_t i = std::min(a, b); i <= std::max(a, b); ++i) {
                const fs::path& p = m_entries[static_cast<size_t>(i)].path;
                if (!IsSelected(p)) m_selection.push_back(p);
            }
            return;
        }
    }
    if (io.KeyCtrl) {
        if (IsSelected(path)) {
            std::erase_if(m_selection, [&](const fs::path& p) { return SamePath(p, path); });
        } else {
            m_selection.push_back(path);
        }
        m_anchor = path;
        return;
    }
    SelectOnly(path);
}

const ProjectPanel::Entry* ProjectPanel::FindEntry(const fs::path& path) const {
    for (const Entry& e : m_entries) {
        if (SamePath(e.path, path)) return &e;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Operações
// ---------------------------------------------------------------------------

void ProjectPanel::Activate(const Entry& entry, const Callbacks& callbacks) {
    if (entry.isDirectory) {
        SetCurrentDir(entry.path);
    } else if (entry.type == AssetType::Scene) {
        callbacks.openScene(entry.path);
    } else if ((entry.type == AssetType::Material || entry.type == AssetType::Prefab) && callbacks.selectAsset) {
        callbacks.selectAsset(entry.path);
    } else {
        Shell::OpenWithDefaultApp(entry.path);
    }
}

void ProjectPanel::BeginRename(const fs::path& path) {
    std::error_code ec;
    m_renaming = path;
    SelectOnly(path);
    m_renameBuffer = ToUtf8(fs::is_directory(path, ec) ? path.filename() : path.stem());
    m_renameFocus = true;
}

void ProjectPanel::CommitRename(const Callbacks& callbacks) {
    const fs::path from = m_renaming;
    m_renaming.clear();

    if (std::string problem = motor::Project::ValidateName(m_renameBuffer); !problem.empty()) {
        ShowError(std::move(problem));
        return;
    }

    std::error_code ec;
    const bool isDir = fs::is_directory(from, ec);
    // A extensão é preservada: o usuário edita só o nome.
    const fs::path to = from.parent_path() / PathFromUtf8(m_renameBuffer + (isDir ? "" : ToUtf8(from.extension())));
    if (to == from) return;
    // No Windows, mudar só maiúsculas/minúsculas aponta para o mesmo arquivo: é permitido.
    if (fs::exists(to, ec) && !fs::equivalent(from, to, ec)) {
        ShowError(std::format("Já existe \"{}\" nesta pasta.", ToUtf8(to.filename())));
        return;
    }

    fs::rename(from, to, ec);
    if (ec) {
        ShowError(std::format("Não foi possível renomear: {}", ec.message()));
        return;
    }
    MoveMetaAlong(from, to);
    SelectOnly(to);
    m_currentDir = motor::RebasePath(m_currentDir, from, to);
    if (callbacks.pathMoved) callbacks.pathMoved(from, to);
    Refresh();
}

void ProjectPanel::CreateFolder() {
    const fs::path path = UniquePath(m_currentDir, "Nova pasta", "");
    std::error_code ec;
    if (!fs::create_directory(path, ec)) {
        ShowError(std::format("Não foi possível criar a pasta: {}", ec.message()));
        return;
    }
    m_search.clear();
    Refresh();
    BeginRename(path);
}

void ProjectPanel::CreateScene(const Callbacks& callbacks) {
    const fs::path path = UniquePath(m_currentDir, "Nova cena", motor::SceneSerializer::kFileExtension);
    if (!callbacks.createSceneFile || !callbacks.createSceneFile(path)) return;
    m_search.clear();
    Refresh();
    BeginRename(path);
}

void ProjectPanel::CreateMaterial(const Callbacks& callbacks) {
    const fs::path path = UniquePath(m_currentDir, "Novo material", motor::kMaterialFileExtension);
    if (!callbacks.createMaterialFile || !callbacks.createMaterialFile(path)) return;
    m_search.clear();
    Refresh();
    BeginRename(path);
    if (callbacks.selectAsset) callbacks.selectAsset(path);
}

void ProjectPanel::CopySelection(bool cut) {
    if (m_selection.empty()) return;
    m_clipboard = m_selection;
    m_clipboardCut = cut;
    // Também vai para a área de transferência do Windows, para colar no Explorer.
    Clipboard::SetFiles(m_clipboard, cut);
    m_clipboardSequence = Clipboard::SequenceNumber();
}

void ProjectPanel::Paste(const Callbacks& callbacks) {
    // Se a área de transferência mudou depois do nosso Ctrl+C/X, quem manda é ela (ex.: arquivos copiados no Explorer).
    if (Clipboard::SequenceNumber() != m_clipboardSequence) {
        const std::vector<fs::path> external = Clipboard::GetFiles();
        if (!external.empty()) {
            CopyItems(external, m_currentDir);
            return;
        }
    }
    if (m_clipboard.empty()) return;

    if (m_clipboardCut) {
        MoveItems(m_clipboard, m_currentDir, callbacks);
        m_clipboard.clear();  // recortar vale para uma colagem só
        m_clipboardCut = false;
    } else {
        CopyItems(m_clipboard, m_currentDir);
    }
}

void ProjectPanel::DuplicateSelection() {
    std::vector<fs::path> created;
    std::vector<std::string> errors;
    for (const fs::path& source : m_selection) {
        const fs::path to = UniqueDestination(source, source.parent_path());
        std::error_code ec;
        fs::copy(source, to, fs::copy_options::recursive, ec);
        if (ec) {
            errors.push_back(std::format("Não foi possível duplicar \"{}\": {}", ToUtf8(source.filename()), ec.message()));
        } else {
            created.push_back(to);
        }
    }
    if (!created.empty()) m_selection = created;
    ShowErrors(errors);
    Refresh();
}

void ProjectPanel::CopyItems(const std::vector<fs::path>& sources, const fs::path& folder) {
    std::vector<fs::path> created;
    std::vector<std::string> errors;
    std::error_code ec;
    for (const fs::path& source : sources) {
        const std::string name = ToUtf8(source.filename());
        if (!fs::exists(source, ec)) {
            errors.push_back(std::format("\"{}\" não existe mais.", name));
            continue;
        }
        if (fs::is_directory(source, ec) && IsInside(folder, source)) {
            errors.push_back(std::format("Não é possível copiar \"{}\" para dentro dela mesma.", name));
            continue;
        }
        const fs::path to = UniqueDestination(source, folder);
        fs::copy(source, to, fs::copy_options::recursive, ec);
        if (ec) {
            errors.push_back(std::format("Não foi possível copiar \"{}\": {}", name, ec.message()));
            continue;
        }
        motor::log::Info("Copiado: {} -> {}", ToUtf8(source), ToUtf8(to));
        created.push_back(to);
    }
    if (!created.empty()) {
        if (!SamePath(folder, m_currentDir)) SetCurrentDir(folder);
        m_selection = created;
    }
    ShowErrors(errors);
    Refresh();
}

void ProjectPanel::MoveItems(const std::vector<fs::path>& sources, const fs::path& folder, const Callbacks& callbacks) {
    std::vector<fs::path> moved;
    std::vector<std::string> errors;
    std::error_code ec;
    for (const fs::path& source : sources) {
        const std::string name = ToUtf8(source.filename());
        if (SamePath(source, folder) || IsInside(folder, source)) {
            errors.push_back(std::format("Não é possível mover \"{}\" para dentro dela mesma.", name));
            continue;
        }
        const fs::path to = folder / source.filename();
        if (SamePath(to, source)) continue;  // já está nessa pasta
        if (fs::exists(to, ec)) {
            errors.push_back(std::format("Já existe \"{}\" em \"{}\".", name, ToUtf8(folder.filename())));
            continue;
        }
        fs::rename(source, to, ec);
        if (ec) {
            errors.push_back(std::format("Não foi possível mover \"{}\": {}", name, ec.message()));
            continue;
        }
        MoveMetaAlong(source, to);
        m_currentDir = motor::RebasePath(m_currentDir, source, to);
        if (callbacks.pathMoved) callbacks.pathMoved(source, to);
        moved.push_back(to);
    }
    if (!moved.empty()) m_selection = moved;
    ShowErrors(errors);
    Refresh();
}

void ProjectPanel::AcceptMoveDrop(const fs::path& folder, const Callbacks& callbacks) {
    const std::vector<fs::path> paths = AcceptAnyPathDrop();
    if (!paths.empty()) MoveItems(paths, folder, callbacks);
    // Entidade arrastada da Hierarquia: vira um prefab nesta pasta.
    if (callbacks.createPrefabFromEntity && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
            callbacks.createPrefabFromEntity(*static_cast<const uint32_t*>(payload->Data), folder);
            Refresh();
        }
        ImGui::EndDragDropTarget();
    }
}

void ProjectPanel::ShowError(std::string message) {
    motor::log::Warn("Projeto: {}", message);
    m_error = std::move(message);
    m_openErrorPopup = true;
}

void ProjectPanel::ShowErrors(const std::vector<std::string>& errors) {
    if (errors.empty()) return;
    std::string message;
    const size_t shown = std::min<size_t>(errors.size(), 6);
    for (size_t i = 0; i < shown; ++i) message += (i ? "\n" : "") + errors[i];
    if (errors.size() > shown) message += std::format("\n... e mais {} erro(s).", errors.size() - shown);
    ShowError(std::move(message));
}

} // namespace editor
