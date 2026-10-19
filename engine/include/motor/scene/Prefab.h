#pragma once

#include "motor/scene/PrefabInstance.h"
#include "motor/scene/PrefabNested.h"
#include "motor/scene/Scene.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace motor {

class AssetLibrary;

// Prefab (.mprefab): uma entidade com filhos salva como asset, no mesmo formato de entidades da cena
// ("format": "motor-prefab"). Cada entidade do arquivo tem um PrefabInstance cujo `source` é o seu id estável
// dentro do prefab; uma instância na cena é uma cópia dessas entidades, com o vínculo junto.
//
// Como as instâncias seguem o prefab (Prefabs::SyncInstances): toda propriedade que não é override recebe o
// valor do prefab. Overrides são registrados comparando a instância com o prefab depois de cada edição
// (Prefabs::RecordOverrides): o que ficou diferente foi mudado na instância. Por isso a ordem, a cada passo
// do editor, é: primeiro sincronizar (se o prefab mudou), depois registrar overrides.
//
// Prefab dentro de prefab: entidades do prefab que vieram de outro prefab têm também um PrefabNested (ver
// PrefabNested.h). Prefabs::UpdateNested leva as mudanças do de dentro para o de fora, antes do sync da cena.
struct Prefab {
    std::string guid;
    Scene scene;                 // as entidades do prefab
    EntityId root = kNullEntity;
    uint32_t rootSource = 0;
    uint64_t revision = 0;       // muda a cada alteração; instâncias com outra revisão precisam de sync

    const Entity* FindSource(uint32_t source) const;
    Entity* FindSource(uint32_t source);
    void RebuildIndex();         // depois de mudar as entidades de `scene`

private:
    std::unordered_map<uint32_t, EntityId> m_bySource;
};

// Cache dos prefabs do projeto, por GUID (como o AssetLibrary faz com malhas e texturas).
class PrefabLibrary {
public:
    // nullptr (e `error`) se o GUID não for de um .mprefab do projeto ou o arquivo não abrir.
    Prefab* Get(std::string_view guid, AssetLibrary& assets, std::string* error = nullptr);
    Prefab* GetFile(const std::filesystem::path& file, AssetLibrary& assets, std::string* error = nullptr);

    // Mudou `prefab.scene` na memória: nova revisão (as instâncias seguem no próximo sync). Não grava.
    void MarkChanged(Prefab& prefab);
    // Grava o prefab no arquivo dele.
    bool Save(Prefab& prefab, AssetLibrary& assets, std::string* error = nullptr);

    // Relê os prefabs em cache cujo arquivo mudou no disco e tenta de novo os que não abriram. true se algum mudou.
    bool ReloadChanged(AssetLibrary& assets);
    void Clear();  // troca de projeto
    // Visita a cena de cada prefab em cache (usado na recarga da DLL de scripts).
    void ForEachScene(const std::function<void(Scene&)>& fn);
    // Visita cada prefab em cache (pode carregar outros durante a visita; esses ficam para a próxima).
    void ForEachPrefab(const std::function<void(Prefab&)>& fn);

    // Grava as entidades num .mprefab (sem guardar o vínculo com o arquivo).
    static bool WriteFile(const std::filesystem::path& file, const Scene& scene, AssetLibrary& assets,
                          std::string* error = nullptr);

private:
    bool Load(Prefab& prefab, const std::filesystem::path& file, AssetLibrary& assets, std::string* error);

    struct Cached {
        std::unique_ptr<Prefab> prefab;  // endereço estável: recarregar troca o conteúdo, não o objeto
        std::filesystem::file_time_type modified{};
    };
    std::map<std::string, Cached, std::less<>> m_cache;
    std::set<std::string, std::less<>> m_failed;  // não abriram: só tenta de novo no ReloadChanged
    uint64_t m_nextRevision = 1;
};

namespace Prefabs {

// Entidade da instância que é a raiz (a própria, ou um ancestral). kNullEntity se não for de instância.
EntityId InstanceRoot(const Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets);
// Raiz da instância, se `id` for uma (e o prefab dela abrir).
bool IsInstanceRoot(const Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets);

// Copia o prefab para a cena (debaixo de `parent`); devolve a raiz da instância nova.
EntityId Instantiate(const Prefab& prefab, Scene& scene, EntityId parent = kNullEntity);

// Grava a entidade (com os filhos) como um prefab novo em `file` e transforma a entidade numa instância
// dele. Instâncias de outros prefabs dentro dela ficam aninhadas no novo (PrefabNested) e continuam seguindo
// os prefabs delas. Devolve o prefab criado (nullptr e `error` se falhar).
Prefab* CreateFromEntity(Scene& scene, EntityId id, const std::filesystem::path& file, PrefabLibrary& library,
                         AssetLibrary& assets, std::string* error = nullptr);

// Prefab dentro de prefab: o conteúdo de cada prefab em cache segue os prefabs aninhados nele que mudaram (menos
// o que ele mudou neles), e quem mudou ganha revisão nova. Repete até nada mudar (B dentro de A dentro de C).
// Chamado por SyncInstances e NeedsSync. Devolve true se algum prefab mudou.
bool UpdateNested(PrefabLibrary& library, AssetLibrary& assets);
// O que foi mudado no conteúdo de `prefab` nas entidades de prefabs aninhados vira override delas (depois de
// editar o prefab direto; senão o próximo UpdateNested desfaria a edição).
size_t RecordNestedOverrides(Prefab& prefab, PrefabLibrary& library, AssetLibrary& assets);

// Leva os valores do prefab às instâncias que ainda não viram a revisão atual dele (todas, com `force`).
// Entidades que o prefab ganhou aparecem; as que ele perdeu somem. Devolve quantas instâncias atualizou.
size_t SyncInstances(Scene& scene, PrefabLibrary& library, AssetLibrary& assets, bool force = false);
bool NeedsSync(const Scene& scene, PrefabLibrary& library, AssetLibrary& assets);

// Diferenças entre as instâncias e os prefabs viram overrides. Devolve quantos overrides novos.
size_t RecordOverrides(Scene& scene, PrefabLibrary& library, AssetLibrary& assets);

// Grava a instância (a que `id` pertence) no prefab: os overrides viram o novo prefab e a instância fica
// sem overrides. As outras instâncias seguem no próximo sync. Instâncias de outros prefabs postas dentro dela
// ficam aninhadas; recusa (false e `error`) se isso pusesse o prefab dentro dele mesmo.
bool ApplyInstance(Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets,
                   std::string* error = nullptr);
// Descarta os overrides da instância: no próximo sync ela volta a ser igual ao prefab.
void RevertInstance(Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets);
// Desfaz o vínculo: as entidades ficam como estão, mas não seguem mais o prefab. As que vieram de um prefab
// aninhado voltam a ser instâncias dele.
void Unpack(Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets);

} // namespace Prefabs

} // namespace motor
