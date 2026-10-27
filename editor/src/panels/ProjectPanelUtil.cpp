#include "panels/ProjectPanelUtil.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace editor::project_panel {

namespace fs = std::filesystem;
using motor::AssetType;
using motor::PathFromUtf8;
using motor::ToUtf8;

Icon IconFor(bool isDirectory, AssetType type) {
    if (isDirectory) return Icon::Folder;
    switch (type) {
        case AssetType::Scene: return Icon::Scene;
        case AssetType::Texture: return Icon::Texture;
        case AssetType::Mesh: return Icon::Mesh;
        case AssetType::Material: return Icon::Material;
        case AssetType::Prefab: return Icon::Prefab;
        case AssetType::Font: break;  // fontes da UI: ícone de arquivo
        case AssetType::Model: return Icon::Mesh;
        case AssetType::EnvironmentMap: return Icon::Texture;  // céu HDR
        case AssetType::Audio: break;  // clipes de som: ícone de arquivo
        case AssetType::NavMesh: break;  // navmesh gerado: ícone de arquivo
        case AssetType::Terrain: return Icon::Mesh;
        case AssetType::Unknown: break;
    }
    return Icon::File;
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

void DrawIcon(ImDrawList* dl, ImVec2 p, float s, Icon icon, float alpha) {
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
namespace {
// O companheiro de `from` com o nome de `to`: o começo do nome (o nome do asset sem a extensão) é trocado.
fs::path CompanionFor(const fs::path& companion, const fs::path& from, const fs::path& to) {
    const std::string name = ToUtf8(companion.filename()), stem = ToUtf8(from.stem());
    const std::string rest = name.starts_with(stem) ? name.substr(stem.size()) : name;
    return to.parent_path() / PathFromUtf8(ToUtf8(to.stem()) + rest);
}
} // namespace

void MoveMetaAlong(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::is_directory(to, ec)) return;
    // `from` já foi movido: os companheiros são procurados ao lado de onde ele estava.
    for (const fs::path& companion : motor::AssetCompanions(from)) {
        fs::rename(companion, CompanionFor(companion, from, to), ec);
        if (ec) motor::log::Warn("Não foi possível mover {}: {}", ToUtf8(companion), ec.message());
    }
    const fs::path meta = motor::AssetDatabase::MetaPathFor(from);
    if (!fs::exists(meta, ec)) return;
    fs::rename(meta, motor::AssetDatabase::MetaPathFor(to), ec);
    if (ec) motor::log::Warn("Não foi possível mover {}: {}", ToUtf8(meta), ec.message());
}

void CopyCompanions(const fs::path& from, const fs::path& to, std::error_code& ec) {
    for (const fs::path& companion : motor::AssetCompanions(from)) {
        fs::copy(companion, CompanionFor(companion, from, to), fs::copy_options::recursive, ec);
        if (ec) return;
    }
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

} // namespace editor::project_panel
