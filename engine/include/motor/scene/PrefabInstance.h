#pragma once

#include "motor/scene/Component.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Referência a um prefab (.mprefab do projeto) num campo de componente ou script: aparece no Inspector como
// um seletor de prefabs (que aceita arrastar do painel Projeto) e pode ser instanciado durante o jogo com
// Script::Instantiate(prefab) ou Script::Instantiate(prefab, posição).
struct PrefabRef {
    std::string guid;
    std::string path;  // relativo ao projeto ("Assets/Prefabs/Bala.mprefab")
    // Arquivo no disco, resolvido pela AssetLibrary ao ler a cena (ou ao escolher no editor). Não entra na
    // comparação de igualdade nem no JSON da cena.
    std::filesystem::path file;

    bool IsEmpty() const { return guid.empty() && path.empty(); }
    bool operator==(const PrefabRef& other) const { return guid == other.guid && path == other.path; }
};

template <> struct FieldTraits<PrefabRef> {
    static CustomFieldType Describe();
};

// Vínculo de uma entidade com a entidade correspondente de um prefab (.mprefab, ver Prefab.h).
//
// Toda entidade de uma instância tem um: `source` diz qual entidade do prefab ela é (id estável dentro do
// arquivo do prefab). A raiz da instância é a entidade cujo `source` é o da raiz do prefab.
//
// Overrides: as propriedades mudadas na instância deixam de seguir o prefab. Cada uma é um texto:
//   "name", "active", "position", "rotation", "scale"   propriedades da entidade
//   "MeshRenderer.color"                                  campo de componente ("Tipo.campo")
//   "-MeshRenderer" / "+Girar"                            componente removido / adicionado na instância
//   "-#3" (só na raiz)                                    entidade 3 do prefab apagada na instância
// Posição e rotação da raiz são sempre da instância (cada cópia fica num lugar) e não entram na lista.
struct PrefabLink {
    std::string guid;                    // GUID do .mprefab
    uint32_t source = 0;                 // entidade dentro do prefab
    std::vector<std::string> overrides;
    // Revisão do prefab aplicada por último (Prefabs::SyncInstances). Não é gravada nem comparada:
    // 0 = ainda não sincronizada (cena recém-aberta).
    uint64_t syncedRevision = 0;

    bool HasOverride(std::string_view key) const;
    bool AddOverride(std::string key);  // false se já tinha
};

struct PrefabInstance {
    PrefabLink link;
};

// Na cena: { "guid": "...", "path": "Assets/...", "source": 3, "overrides": ["MeshRenderer.color"] }.
template <> struct FieldTraits<PrefabLink> {
    static CustomFieldType Describe();
};

} // namespace motor
