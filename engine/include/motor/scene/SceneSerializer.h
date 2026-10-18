#pragma once

#include "motor/assets/AssetLoaders.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class AssetLibrary;
class Scene;
struct ComponentType;
struct FieldInfo;

// Cenas em JSON (.mscene). Malhas e texturas embutidas são referenciadas pelo nome; as de arquivo, pelo GUID
// do .meta (com o caminho junto, como plano B). Ao carregar, assets de arquivo são lidos do disco.
// As funções retornam false e preenchem `error` (se não for nulo) quando falham.
namespace SceneSerializer {

inline constexpr const char* kFileExtension = kSceneFileExtension;

// Valor de "format" no arquivo. Prefabs (.mprefab, Prefab.h) usam o mesmo formato de entidades, sem o ambiente.
inline constexpr const char* kSceneFormat = "motor-scene";
inline constexpr const char* kPrefabFormat = "motor-prefab";

struct JsonOptions {
    // Grava "path" ao lado do GUID. Desligado na comparação de "alterações não salvas": mover um arquivo
    // muda o caminho, mas não a cena.
    bool pathHints = true;
    const char* format = kSceneFormat;
};

std::string ToJson(const Scene& scene, const AssetLibrary& assets, const JsonOptions& options = {});

// `out` deve estar vazia. Assets não encontrados viram avisos no log (o componente fica sem o asset).
// `format`: o "format" exigido no arquivo (kSceneFormat ou kPrefabFormat).
bool FromJson(std::string_view json, Scene& out, AssetLibrary& assets, std::string* error = nullptr,
              const char* format = kSceneFormat);

// Um campo de componente no formato da cena (texto JSON): malhas e texturas como nome ou {guid, path},
// vetores como listas. Usado por quem lê e muda campos de fora do Inspector (o servidor MCP do editor).
std::string FieldValueToJson(const FieldInfo& field, const void* component, const AssetLibrary& assets);
// Lê o campo do texto JSON nesse mesmo formato. Valor de tipo ou formato errado: o campo fica como estava,
// devolve false e preenche `error` com o formato esperado.
bool FieldValueFromJson(const FieldInfo& field, void* component, std::string_view json, AssetLibrary& assets,
                        std::string* error = nullptr);

// Componentes de tipo desconhecido viram MissingComponent (Component.h): os dados são mantidos e gravados de
// volta iguais. As duas funções abaixo fazem a troca nos dois sentidos, para a recarga da DLL de scripts.

// Troca os componentes dos tipos escolhidos por MissingComponent com o JSON dos campos. Depois disso a cena
// não tem mais nenhum objeto desses tipos (o tipo pode ser desregistrado). Devolve quantos trocou.
size_t DetachComponents(Scene& scene, const std::function<bool(const ComponentType&)>& which,
                        const AssetLibrary& assets);
// Valor guardado que não coube no componente religado: o campo sumiu do tipo (renomeado sem
// FieldOptions::formerKey, ou apagado) ou mudou de tipo. `value` é o JSON do valor perdido.
struct FieldWarning {
    std::string type;    // tipo do componente ("Jogador")
    std::string entity;  // nome da entidade
    std::string field;   // nome do campo no arquivo
    std::string value;
    bool typeChanged = false;  // false: o campo não existe mais
};
// Religa os MissingComponent cujo tipo está registrado agora, lendo os campos do JSON guardado (os novos ficam
// com o valor padrão). Os valores que não couberam vão para `warnings`, se não for nulo. Devolve quantos religou.
size_t ResolveMissingComponents(Scene& scene, AssetLibrary& assets, std::vector<FieldWarning>* warnings = nullptr);

// Grava num .tmp e renomeia no fim. `options.format` = kPrefabFormat grava um prefab.
bool Save(const std::filesystem::path& path, const Scene& scene, const AssetLibrary& assets,
          std::string* error = nullptr, const JsonOptions& options = {});
bool Load(const std::filesystem::path& path, Scene& out, AssetLibrary& assets, std::string* error = nullptr);

// Salvamento automático / recuperação (<projectRoot>/.motor/recuperacao/): gravado pelo editor ao dar Play e a
// cada N minutos com alterações não salvas; apagado ao salvar, descartar ou fechar limpo.
struct RecoveryInfo {
    std::filesystem::path sceneFile;     // <projectRoot>/.motor/recuperacao/cena.mscene
    std::filesystem::path originalPath;  // arquivo original da cena (absoluto; vazio se era "Sem título")
    std::string reason;                  // "play" ou "autosave"
    int64_t savedAt = 0;                 // segundos desde 1970 (UTC)
};

bool SaveRecovery(const std::filesystem::path& projectRoot, const Scene& scene, const AssetLibrary& assets,
                  const std::filesystem::path& originalScenePath, std::string_view reason = "autosave",
                  std::string* error = nullptr);
std::optional<RecoveryInfo> CheckRecovery(const std::filesystem::path& projectRoot);
void ClearRecovery(const std::filesystem::path& projectRoot);

} // namespace SceneSerializer

} // namespace motor
