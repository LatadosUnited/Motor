#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace motor {

class AssetLibrary;
class Scene;
struct ComponentType;

// Cenas em JSON (.mscene). Malhas e texturas embutidas são referenciadas pelo nome; as de arquivo, pelo GUID
// do .meta (com o caminho junto, como plano B). Ao carregar, assets de arquivo são lidos do disco.
// As funções retornam false e preenchem `error` (se não for nulo) quando falham.
namespace SceneSerializer {

inline constexpr const char* kFileExtension = ".mscene";

struct JsonOptions {
    // Grava "path" ao lado do GUID. Desligado na comparação de "alterações não salvas": mover um arquivo
    // muda o caminho, mas não a cena.
    bool pathHints = true;
};

std::string ToJson(const Scene& scene, const AssetLibrary& assets, const JsonOptions& options = {});

// `out` deve estar vazia. Assets não encontrados viram avisos no log (o componente fica sem o asset).
bool FromJson(std::string_view json, Scene& out, AssetLibrary& assets, std::string* error = nullptr);

// Componentes de tipo desconhecido viram MissingComponent (Component.h): os dados são mantidos e gravados de
// volta iguais. As duas funções abaixo fazem a troca nos dois sentidos, para a recarga da DLL de scripts.

// Troca os componentes dos tipos escolhidos por MissingComponent com o JSON dos campos. Depois disso a cena
// não tem mais nenhum objeto desses tipos (o tipo pode ser desregistrado). Devolve quantos trocou.
size_t DetachComponents(Scene& scene, const std::function<bool(const ComponentType&)>& which,
                        const AssetLibrary& assets);
// Religa os MissingComponent cujo tipo está registrado agora, lendo os campos do JSON guardado (campos que
// sumiram do tipo são ignorados; os novos ficam com o valor padrão). Devolve quantos religou.
size_t ResolveMissingComponents(Scene& scene, AssetLibrary& assets);

bool Save(const std::filesystem::path& path, const Scene& scene, const AssetLibrary& assets,
          std::string* error = nullptr);
bool Load(const std::filesystem::path& path, Scene& out, AssetLibrary& assets, std::string* error = nullptr);

} // namespace SceneSerializer

} // namespace motor
