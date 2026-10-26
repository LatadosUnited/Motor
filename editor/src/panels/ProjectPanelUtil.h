#pragma once

// Peças internas do painel Projeto (ProjectPanel*.cpp): ícones, caminhos e textos. Não é usado fora do painel.

#include <motor/assets/AssetDatabase.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/common/Text.h>

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace editor::project_panel {

inline constexpr double kRescanInterval = 1.0;  // segundos; pega mudanças feitas fora do editor
inline constexpr size_t kMaxSearchResults = 1000;
inline constexpr int kThumbnailPixels = 128;    // lado máximo da miniatura guardada na GPU

enum class Icon { Folder, Scene, Texture, Mesh, Material, Prefab, File };

Icon IconFor(bool isDirectory, motor::AssetType type);
ImU32 Alpha(ImU32 color, float alpha);  // mesma cor com a opacidade multiplicada
void DrawIcon(ImDrawList* dl, ImVec2 p, float s, Icon icon, float alpha = 1.0f);

using motor::Lower;  // motor/common/Text.h
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b);
std::string FormatSize(uintmax_t bytes);
const char* FilterName(int filter);
// Corta o texto com "..." para caber em `width` (sem partir caracteres UTF-8 ao meio).
std::string Ellipsize(const std::string& text, float width);

std::filesystem::path UniquePath(const std::filesystem::path& dir, const std::string& base, const std::string& ext);
// Destino para copiar `source` dentro de `folder` sem sobrescrever nada: "nome", "nome 2", "nome 3"...
std::filesystem::path UniqueDestination(const std::filesystem::path& source, const std::filesystem::path& folder);
// Depois de renomear/mover um arquivo, leva o .meta junto para o asset manter o GUID.
// (Pastas não precisam: os .meta de dentro vão com elas.)
void MoveMetaAlong(const std::filesystem::path& from, const std::filesystem::path& to);

// Reduz a imagem (média de blocos) para caber num quadrado de `maxSide`.
motor::ImageData Downscale(const motor::ImageData& src, int maxSide);

} // namespace editor::project_panel
