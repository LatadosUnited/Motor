#pragma once

#include "motor/assets/AssetLoaders.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace motor {

class AssetLibrary;
class Scene;

// Cenas em JSON (.mscene). Malhas e texturas embutidas são referenciadas pelo nome; as de arquivo, pelo GUID
// do .meta (com o caminho junto, como plano B). Ao carregar, assets de arquivo são lidos do disco.
// As funções retornam false e preenchem `error` (se não for nulo) quando falham.
namespace SceneSerializer {

inline constexpr const char* kFileExtension = kSceneFileExtension;

struct JsonOptions {
    // Grava "path" ao lado do GUID. Desligado na comparação de "alterações não salvas": mover um arquivo
    // muda o caminho, mas não a cena.
    bool pathHints = true;
};

std::string ToJson(const Scene& scene, const AssetLibrary& assets, const JsonOptions& options = {});

// `out` deve estar vazia. Assets não encontrados viram avisos no log (o componente fica sem o asset).
bool FromJson(std::string_view json, Scene& out, AssetLibrary& assets, std::string* error = nullptr);

bool Save(const std::filesystem::path& path, const Scene& scene, const AssetLibrary& assets,
          std::string* error = nullptr);
bool Load(const std::filesystem::path& path, Scene& out, AssetLibrary& assets, std::string* error = nullptr);

} // namespace SceneSerializer

} // namespace motor
