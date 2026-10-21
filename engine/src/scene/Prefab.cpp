#include "motor/scene/Prefab.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/EntityRef.h"
#include "motor/scene/SceneSerializer.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>

namespace motor {

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

const PrefabLink* LinkOf(const Entity& e) {
    const PrefabInstance* p = e.Get<PrefabInstance>();
    return p ? &p->link : nullptr;
}

PrefabLink* LinkOf(Entity& e) {
    PrefabInstance* p = e.Get<PrefabInstance>();
    return p ? &p->link : nullptr;
}

const PrefabLink* NestedOf(const Entity& e) {
    const PrefabNested* p = e.Get<PrefabNested>();
    return p ? &p->link : nullptr;
}

PrefabLink* NestedOf(Entity& e) {
    PrefabNested* p = e.Get<PrefabNested>();
    return p ? &p->link : nullptr;
}

// Qual vínculo liga a entidade ao prefab que a "instancia": na cena, o PrefabInstance; no conteúdo de um prefab,
// o PrefabNested (as entidades que vieram de outro prefab). O PrefabInstance do conteúdo é a identidade dela
// dentro do próprio prefab.
enum class LinkKind { Instance, Nested };

const PrefabLink* LinkOf(const Entity& e, LinkKind kind) { return kind == LinkKind::Instance ? LinkOf(e) : NestedOf(e); }
PrefabLink* LinkOf(Entity& e, LinkKind kind) { return kind == LinkKind::Instance ? LinkOf(e) : NestedOf(e); }

void SetNested(Entity& e, PrefabLink link) {
    if (PrefabLink* existing = NestedOf(e)) {
        *existing = std::move(link);
        return;
    }
    e.Add<PrefabNested>().link = std::move(link);
}

// Põe o vínculo na entidade. Componente novo vai para o início (aparece no topo do Inspector).
void SetLink(Entity& e, PrefabLink link) {
    if (PrefabLink* existing = LinkOf(e)) {
        *existing = std::move(link);
        return;
    }
    const ComponentType& type = *ComponentRegistry::TypeOf<PrefabInstance>();
    e.components.insert(e.components.begin(), ComponentPtr(type));
    MarkSceneStructureChanged();
    static_cast<PrefabInstance*>(e.components.front().Data())->link = std::move(link);
}

// Componentes que o sync e os overrides ignoram: os vínculos e os de tipo desconhecido.
bool Skipped(const ComponentType& type) {
    return &type == ComponentRegistry::TypeOf<PrefabInstance>() || &type == ComponentRegistry::TypeOf<PrefabNested>() ||
           &type == &ComponentRegistry::MissingType();
}

// Pares (original, cópia) de entidades copiadas: referências (EntityRef) que apontam para um original passam a
// apontar para a cópia. `dropOutside`: as que apontam para fora da cópia viram "nenhuma" (num prefab, um id de
// fora não quer dizer nada).
using CopyPairs = std::vector<std::pair<EntityId, EntityId>>;

EntityId MapCopied(const CopyPairs& copies, EntityId id, bool dropOutside) {
    for (const auto& [from, to] : copies) {
        if (from == id) return to;
    }
    return dropOutside ? kNullEntity : id;
}

void RemapCopied(Scene& scene, const CopyPairs& copies, bool dropOutside) {
    for (const auto& [from, to] : copies) {
        if (Entity* e = scene.Find(to)) RemapEntityRefs(*e, [&](EntityId id) { return MapCopied(copies, id, dropOutside); });
    }
}

bool IsEntityRef(const FieldInfo& f) {
    return f.type == FieldType::Custom && f.custom && f.custom->name == kEntityRefTypeName;
}

// Copia a entidade e os descendentes de `from` para `to`, debaixo de `parent`. `fn` ajusta cada cópia.
EntityId CopyTree(const Scene& from, EntityId id, Scene& to, EntityId parent,
                  const std::function<void(const Entity& source, Entity& copy)>& fn) {
    const Entity* src = from.Find(id);
    if (!src) return kNullEntity;
    Entity& dst = to.CreateEntity(src->name, parent);
    dst.active = src->active;
    dst.transform = src->transform;
    dst.components = src->components;
    MarkSceneStructureChanged();
    if (fn) fn(*src, dst);
    const EntityId dstId = dst.id;
    for (EntityId child : from.Children(id)) CopyTree(from, child, to, dstId, fn);
    return dstId;
}

bool FieldEqual(const FieldInfo& f, const void* a, const void* b) {
    switch (f.type) {
        case FieldType::Bool: return f.As<bool>(a) == f.As<bool>(b);
        case FieldType::Int: return f.As<int>(a) == f.As<int>(b);
        case FieldType::Float: return f.As<float>(a) == f.As<float>(b);
        case FieldType::Vec2: return f.As<glm::vec2>(a) == f.As<glm::vec2>(b);
        case FieldType::Vec3: return f.As<glm::vec3>(a) == f.As<glm::vec3>(b);
        case FieldType::Vec4: return f.As<glm::vec4>(a) == f.As<glm::vec4>(b);
        case FieldType::String: return f.As<std::string>(a) == f.As<std::string>(b);
        case FieldType::Mesh: return f.As<std::shared_ptr<Mesh>>(a) == f.As<std::shared_ptr<Mesh>>(b);
        case FieldType::Texture: return f.As<std::shared_ptr<Texture2D>>(a) == f.As<std::shared_ptr<Texture2D>>(b);
        case FieldType::Custom: return f.custom->equal(f.Get(a), f.Get(b));
    }
    return true;
}

void CopyField(const FieldInfo& f, const void* src, void* dst, AssetLibrary& assets) {
    switch (f.type) {
        case FieldType::Bool: f.As<bool>(dst) = f.As<bool>(src); break;
        case FieldType::Int: f.As<int>(dst) = f.As<int>(src); break;
        case FieldType::Float: f.As<float>(dst) = f.As<float>(src); break;
        case FieldType::Vec2: f.As<glm::vec2>(dst) = f.As<glm::vec2>(src); break;
        case FieldType::Vec3: f.As<glm::vec3>(dst) = f.As<glm::vec3>(src); break;
        case FieldType::Vec4: f.As<glm::vec4>(dst) = f.As<glm::vec4>(src); break;
        case FieldType::String: f.As<std::string>(dst) = f.As<std::string>(src); break;
        case FieldType::Mesh: f.As<std::shared_ptr<Mesh>>(dst) = f.As<std::shared_ptr<Mesh>>(src); break;
        case FieldType::Texture:
            f.As<std::shared_ptr<Texture2D>>(dst) = f.As<std::shared_ptr<Texture2D>>(src);
            break;
        // Tipo definido fora da engine: só sabemos copiar pelo JSON dele (o mesmo caminho do arquivo da cena).
        case FieldType::Custom: f.custom->fromJson(f.custom->toJson(f.Get(src), assets), f.Get(dst), assets, nullptr); break;
    }
}

// Entidades da instância cuja raiz é `root`: source -> id. Desce pelos filhos sem entrar noutra instância
// do mesmo prefab; entidades sem vínculo (adicionadas na instância) não entram.
std::map<uint32_t, EntityId> Members(const Scene& scene, EntityId root, const Prefab& prefab,
                                     LinkKind kind = LinkKind::Instance) {
    std::map<uint32_t, EntityId> members;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        const Entity* e = scene.Find(id);
        if (!e) return;
        const PrefabLink* link = LinkOf(*e, kind);
        const bool same = link && link->guid == prefab.guid;
        if (id != root && same && link->source == prefab.rootSource) return;  // outra instância
        if (same) members.emplace(link->source, id);  // source repetido (entidade duplicada): fica a primeira
        for (EntityId child : scene.Children(id)) visit(child);
    };
    visit(root);
    return members;
}

// Raízes de instância da cena, com o prefab de cada uma.
std::vector<std::pair<EntityId, Prefab*>> InstanceRoots(const Scene& scene, PrefabLibrary& library,
                                                        AssetLibrary& assets, LinkKind kind = LinkKind::Instance) {
    std::vector<std::pair<EntityId, Prefab*>> roots;
    for (const auto& e : scene.Entities()) {
        const PrefabLink* link = LinkOf(*e, kind);
        if (!link || link->guid.empty()) continue;
        Prefab* prefab = library.Get(link->guid, assets);
        if (prefab && link->source == prefab->rootSource) roots.emplace_back(e->id, prefab);
    }
    return roots;
}

// Valores do prefab nas propriedades sem override.
void ApplyEntity(Entity& e, const Entity& p, bool isRoot, AssetLibrary& assets, LinkKind kind = LinkKind::Instance) {
    const std::vector<std::string> overrides = LinkOf(e, kind)->overrides;  // cópia: os componentes vão mudar
    auto has = [&](const std::string& key) { return std::find(overrides.begin(), overrides.end(), key) != overrides.end(); };

    if (!has("name")) e.name = p.name;
    if (!has("active")) e.active = p.active;
    if (!isRoot) {
        if (!has("position")) e.transform.position = p.transform.position;
        if (!has("rotation")) e.transform.rotation = p.transform.rotation;
    }
    if (!has("scale")) e.transform.scale = p.transform.scale;

    for (const ComponentPtr& pc : p.components) {
        const ComponentType& type = pc.Type();
        if (Skipped(type)) continue;
        void* data = e.Get(type);
        if (!data) {
            if (!has("-" + type.name)) {  // o prefab ganhou este componente
                e.components.push_back(pc);
                MarkSceneStructureChanged();
            }
            continue;
        }
        for (const FieldInfo& f : type.fields) {
            if (!has(type.name + "." + f.key)) CopyField(f, pc.Data(), data, assets);
        }
    }
    // Componente que o prefab perdeu (e que não foi adicionado na instância).
    if (std::erase_if(e.components, [&](const ComponentPtr& c) {
            return !Skipped(c.Type()) && !p.Get(c.Type()) && !has("+" + c.Type().name);
        })) {
        MarkSceneStructureChanged();
    }
    // Na cena, a cópia do vínculo aninhado só informa de onde a entidade veio: acompanha o prefab. (No conteúdo
    // de um prefab, o PrefabNested é o vínculo com o de dentro e não se copia do de dentro.)
    if (kind == LinkKind::Instance) {
        if (const PrefabLink* nested = NestedOf(p)) {
            SetNested(e, *nested);
        } else {
            e.Remove<PrefabNested>();
        }
    }
}

// Entidade criada no sync do conteúdo de um prefab (`adopt`): veio do prefab de dentro com o PrefabInstance
// dele, que vira o PrefabNested; o PrefabInstance passa a ser o do prefab de fora.
using Adopt = std::function<void(Entity&)>;

void SyncInstance(Scene& scene, EntityId rootId, const Prefab& prefab, AssetLibrary& assets,
                  LinkKind kind = LinkKind::Instance, const Adopt& adopt = nullptr) {
    // Entidades que o prefab não tem mais.
    std::vector<EntityId> gone;
    for (const auto& [source, id] : Members(scene, rootId, prefab, kind)) {
        if (id != rootId && !prefab.FindSource(source)) gone.push_back(id);
    }
    for (EntityId id : gone) scene.DestroyEntity(id);

    std::map<uint32_t, EntityId> members = Members(scene, rootId, prefab, kind);
    members[prefab.rootSource] = rootId;
    const PrefabLink rootLink = *LinkOf(*scene.Find(rootId), kind);

    // Percorre o prefab dos pais para os filhos: entidade que falta é criada debaixo do pai correspondente.
    std::function<void(EntityId, EntityId)> visit = [&](EntityId prefabId, EntityId instanceParent) {
        const Entity& p = *prefab.scene.Find(prefabId);
        const uint32_t source = LinkOf(p)->source;
        EntityId target = kNullEntity;
        if (auto it = members.find(source); it != members.end()) {
            target = it->second;
            ApplyEntity(*scene.Find(target), p, source == prefab.rootSource, assets, kind);
        } else {
            if (rootLink.HasOverride(std::format("-#{}", source)) || instanceParent == kNullEntity) return;
            // Filhos vêm junto.
            target = CopyTree(prefab.scene, prefabId, scene, instanceParent, [&](const Entity&, Entity& copy) {
                if (adopt) adopt(copy);
            });
            for (const auto& [s, id] : Members(scene, target, prefab, kind)) members.emplace(s, id);
            return;
        }
        for (EntityId child : prefab.scene.Children(prefabId)) visit(child, target);
    };
    visit(prefab.root, kNullEntity);

    // Os valores vieram do prefab: referências entre entidades dele viram as entidades desta instância.
    CopyPairs copies;
    for (const auto& [source, id] : Members(scene, rootId, prefab, kind)) {
        LinkOf(*scene.Find(id), kind)->syncedRevision = prefab.revision;
        if (const Entity* p = prefab.FindSource(source)) copies.emplace_back(p->id, id);
    }
    for (const auto& [source, id] : Members(scene, rootId, prefab, kind)) {
        if (const Entity* p = prefab.FindSource(source)) {
            Entity& e = *scene.Find(id);
            // Só os campos que acabaram de vir do prefab (sem override); os outros já são ids da cena.
            for (const ComponentPtr& pc : p->components) {
                void* data = e.Get(pc.Type());
                if (!data || Skipped(pc.Type())) continue;
                for (const FieldInfo& f : pc.Type().fields) {
                    if (!IsEntityRef(f) || LinkOf(e, kind)->HasOverride(pc.Type().name + "." + f.key)) continue;
                    auto& ref = f.As<EntityRef>(data);
                    if (!ref.IsEmpty() && ref == f.As<EntityRef>(pc.Data())) ref.id = MapCopied(copies, ref.id, true);
                }
            }
        }
    }
}

// Overrides de uma entidade: o que ficou diferente do prefab.
// `toPrefab`: id de uma entidade da instância -> id da entidade correspondente no prefab (EntityRef).
size_t RecordEntity(Entity& e, const Entity& p, bool isRoot, const std::function<EntityId(EntityId)>& toPrefab,
                    LinkKind kind = LinkKind::Instance) {
    PrefabLink& link = *LinkOf(e, kind);
    size_t added = 0;
    auto mark = [&](std::string key) {
        if (link.AddOverride(std::move(key))) ++added;
    };
    if (e.name != p.name) mark("name");
    if (e.active != p.active) mark("active");
    if (!isRoot) {
        if (e.transform.position != p.transform.position) mark("position");
        if (e.transform.rotation != p.transform.rotation) mark("rotation");
    }
    if (e.transform.scale != p.transform.scale) mark("scale");

    for (const ComponentPtr& pc : p.components) {
        const ComponentType& type = pc.Type();
        if (Skipped(type)) continue;
        const void* data = e.Get(type);
        if (!data) {
            mark("-" + type.name);
            continue;
        }
        for (const FieldInfo& f : type.fields) {
            const bool equal = IsEntityRef(f) ? EntityRef(toPrefab(f.As<EntityRef>(data).id)) == f.As<EntityRef>(pc.Data())
                                              : FieldEqual(f, data, pc.Data());
            if (!equal) mark(type.name + "." + f.key);
        }
    }
    for (const ComponentPtr& c : e.components) {
        if (!Skipped(c.Type()) && !p.Get(c.Type())) mark("+" + c.Type().name);
    }
    return added;
}

// Copia a sub-árvore de `rootId` para uma cena de prefab, dando a cada entidade um source (`sourceOf`).
// A raiz fica na origem: a posição de cada instância é dela. Entidades de instâncias de outros prefabs (vínculo
// com um GUID diferente de `targetGuid`) ficam aninhadas: o vínculo vira o PrefabNested. `nested` recebe o
// vínculo aninhado de cada source, para pôr o mesmo nas entidades da cena.
Scene BuildPrefabScene(const Scene& scene, EntityId rootId, std::string_view targetGuid,
                       const std::function<uint32_t(const Entity&)>& sourceOf,
                       std::vector<std::pair<EntityId, uint32_t>>& order, std::map<uint32_t, PrefabLink>& nested) {
    Scene out;
    CopyPairs copies;
    CopyTree(scene, rootId, out, kNullEntity, [&](const Entity& from, Entity& to) {
        const uint32_t source = sourceOf(from);
        if (const PrefabLink* outer = LinkOf(from); outer && !outer->guid.empty() && outer->guid != targetGuid) {
            SetNested(to, *outer);  // o aninhado que ele já tinha é assunto do prefab de dentro
        }
        to.Remove(*ComponentRegistry::TypeOf<PrefabInstance>());
        SetLink(to, {{}, source, {}, 0});
        if (const PrefabLink* link = NestedOf(to)) nested[source] = *link;
        order.emplace_back(from.id, source);
        copies.emplace_back(from.id, to.id);
    });
    RemapCopied(out, copies, true);
    return out;
}

// Prefabs aninhados no conteúdo (direta ou indiretamente). Para recusar um prefab dentro dele mesmo.
bool ContainsPrefab(const Scene& content, std::string_view guid, PrefabLibrary& library, AssetLibrary& assets,
                    int depth = 0) {
    if (depth > 32) return true;  // corrente longa demais: trata como ciclo
    std::set<std::string> seen;
    for (const auto& e : content.Entities()) {
        const PrefabLink* link = NestedOf(*e);
        if (!link || link->guid.empty() || !seen.insert(link->guid).second) continue;
        if (link->guid == guid) return true;
        if (const Prefab* inner = library.Get(link->guid, assets)) {
            if (ContainsPrefab(inner->scene, guid, library, assets, depth + 1)) return true;
        }
    }
    return false;
}

// Na cena, depois de criar ou aplicar: cada entidade com o vínculo novo e a cópia do aninhado (se houver).
void LinkScene(Scene& scene, const std::vector<std::pair<EntityId, uint32_t>>& order,
               const std::map<uint32_t, PrefabLink>& nested, const Prefab& prefab) {
    for (const auto& [entity, source] : order) {
        Entity& e = *scene.Find(entity);
        SetLink(e, {prefab.guid, source, {}, prefab.revision});
        if (auto it = nested.find(source); it != nested.end()) {
            SetNested(e, it->second);
        } else {
            e.Remove<PrefabNested>();
        }
    }
}

size_t RecordOverridesOf(Scene& scene, PrefabLibrary& library, AssetLibrary& assets, LinkKind kind,
                         std::string_view ownGuid = {}) {
    size_t count = 0;
    for (const auto& [root, prefab] : InstanceRoots(scene, library, assets, kind)) {
        if (prefab->guid == ownGuid) continue;
        Entity& rootEntity = *scene.Find(root);
        // Instância que ainda não viu a revisão atual: diferenças agora são do prefab, não overrides.
        if (LinkOf(rootEntity, kind)->syncedRevision != prefab->revision) continue;
        const std::map<uint32_t, EntityId> members = Members(scene, root, *prefab, kind);
        CopyPairs toPrefab;  // (id na instância, id no prefab)
        for (const auto& [source, id] : members) {
            if (const Entity* p = prefab->FindSource(source)) toPrefab.emplace_back(id, p->id);
        }
        // Fora da instância: o id fica (difere do "nenhuma" do prefab e vira override, sem se perder).
        const auto map = [&](EntityId id) { return MapCopied(toPrefab, id, false); };
        for (const auto& [source, id] : members) {
            if (const Entity* p = prefab->FindSource(source)) {
                count += RecordEntity(*scene.Find(id), *p, source == prefab->rootSource, map, kind);
            }
        }
        // Entidades do prefab apagadas na instância.
        for (const auto& e : prefab->scene.Entities()) {
            const uint32_t source = LinkOf(*e)->source;
            if (source != prefab->rootSource && !members.contains(source)) {
                if (LinkOf(rootEntity, kind)->AddOverride(std::format("-#{}", source))) ++count;
            }
        }
    }
    return count;
}
} // namespace

// --- Prefab -------------------------------------------------------------------------

const Entity* Prefab::FindSource(uint32_t source) const {
    auto it = m_bySource.find(source);
    return it != m_bySource.end() ? scene.Find(it->second) : nullptr;
}

Entity* Prefab::FindSource(uint32_t source) {
    auto it = m_bySource.find(source);
    return it != m_bySource.end() ? scene.Find(it->second) : nullptr;
}

void Prefab::RebuildIndex() {
    m_bySource.clear();
    for (const auto& e : scene.Entities()) {
        if (const PrefabLink* link = LinkOf(*e)) m_bySource.emplace(link->source, e->id);
    }
}

// --- PrefabLibrary --------------------------------------------------------------------

Prefab* PrefabLibrary::Get(std::string_view guid, AssetLibrary& assets, std::string* error) {
    if (auto it = m_cache.find(guid); it != m_cache.end()) return it->second.prefab.get();
    if (guid.empty() || m_failed.contains(guid)) {
        SetError(error, std::format("Prefab {} não encontrado.", guid));
        return nullptr;
    }
    AssetDatabase& database = assets.Database();
    fs::path file = database.PathFor(guid);
    if (file.empty()) {
        database.Refresh();  // pode ser um arquivo que chegou depois da última varredura
        file = database.PathFor(guid);
    }
    if (file.empty() || AssetTypeFromPath(file) != AssetType::Prefab) {
        SetError(error, std::format("Nenhum prefab do projeto tem o GUID {}.", guid));
        m_failed.emplace(guid);
        return nullptr;
    }
    auto prefab = std::make_unique<Prefab>();
    prefab->guid = std::string(guid);
    if (!Load(*prefab, file, assets, error)) {
        m_failed.emplace(guid);
        return nullptr;
    }
    std::error_code ec;
    Cached& cached = m_cache[std::string(guid)];
    cached.modified = fs::last_write_time(file, ec);
    cached.prefab = std::move(prefab);
    return cached.prefab.get();
}

Prefab* PrefabLibrary::GetFile(const fs::path& file, AssetLibrary& assets, std::string* error) {
    const std::string guid = assets.Database().GuidFor(file);
    if (guid.empty()) {
        SetError(error, std::format("\"{}\" não é um prefab do projeto.", ToUtf8(file.filename())));
        return nullptr;
    }
    m_failed.erase(guid);  // pedido explícito: tenta de novo
    return Get(guid, assets, error);
}

bool PrefabLibrary::Load(Prefab& prefab, const fs::path& file, AssetLibrary& assets, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(file)));
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();

    Scene scene;
    std::string problem;
    if (!SceneSerializer::FromJson(ss.str(), scene, assets, &problem, SceneSerializer::kPrefabFormat)) {
        SetError(error, std::format("{}: {}", ToUtf8(file.filename()), problem));
        return false;
    }
    const std::vector<EntityId> roots = scene.Children(kNullEntity);
    if (roots.empty()) {
        SetError(error, std::format("{}: o prefab não tem entidades.", ToUtf8(file.filename())));
        return false;
    }
    // Uma raiz só: outras (arquivo editado à mão) viram filhas dela.
    for (size_t i = 1; i < roots.size(); ++i) scene.SetParent(roots[i], roots[0], false);

    // Cada entidade com um source único. Arquivo editado à mão pode não ter: ganha um novo.
    uint32_t next = 1;
    for (const auto& e : scene.Entities()) {
        if (const PrefabLink* link = LinkOf(*e)) next = std::max(next, link->source + 1);
    }
    std::set<uint32_t> used;
    for (const auto& e : scene.Entities()) {
        const PrefabLink* link = LinkOf(*e);
        uint32_t source = link ? link->source : 0;
        if (source == 0 || used.contains(source)) source = next++;
        used.insert(source);
        SetLink(*e, {prefab.guid, source, {}, 0});
    }

    prefab.scene = std::move(scene);
    prefab.root = roots[0];
    prefab.rootSource = LinkOf(*prefab.scene.Find(prefab.root))->source;
    prefab.revision = m_nextRevision++;
    prefab.RebuildIndex();
    log::Info("Prefab carregado: {} ({} entidades)", assets.KeyForFile(file), prefab.scene.Entities().size());
    return true;
}

void PrefabLibrary::MarkChanged(Prefab& prefab) {
    prefab.revision = m_nextRevision++;
    prefab.RebuildIndex();
}

bool PrefabLibrary::Save(Prefab& prefab, AssetLibrary& assets, std::string* error) {
    const fs::path file = assets.Database().PathFor(prefab.guid);
    if (file.empty()) {
        SetError(error, std::format("O arquivo do prefab (GUID {}) não existe mais.", prefab.guid));
        return false;
    }
    if (!WriteFile(file, prefab.scene, assets, error)) return false;
    std::error_code ec;
    if (auto it = m_cache.find(prefab.guid); it != m_cache.end()) it->second.modified = fs::last_write_time(file, ec);
    return true;
}

bool PrefabLibrary::WriteFile(const fs::path& file, const Scene& scene, AssetLibrary& assets, std::string* error) {
    if (!SceneSerializer::Save(file, scene, assets, error, {.format = SceneSerializer::kPrefabFormat})) return false;
    assets.Database().GuidFor(file);  // .meta já na criação
    return true;
}

bool PrefabLibrary::ReloadChanged(AssetLibrary& assets) {
    m_failed.clear();  // arquivos que não abriram podem ter chegado ou sido consertados
    bool changed = false;
    for (auto& [guid, cached] : m_cache) {
        const fs::path file = assets.Database().PathFor(guid);
        if (file.empty()) continue;
        std::error_code ec;
        const auto modified = fs::last_write_time(file, ec);
        if (ec || modified == cached.modified) continue;
        cached.modified = modified;
        Prefab reloaded;
        reloaded.guid = guid;
        std::string error;
        if (!Load(reloaded, file, assets, &error)) {
            log::Warn("Prefab não foi relido: {}", error);
            continue;
        }
        *cached.prefab = std::move(reloaded);  // mesmo objeto, revisão nova
        changed = true;
    }
    return changed;
}

void PrefabLibrary::Clear() {
    m_cache.clear();
    m_failed.clear();
}

void PrefabLibrary::ForEachScene(const std::function<void(Scene&)>& fn) {
    for (auto& [guid, cached] : m_cache) {
        if (cached.prefab) fn(cached.prefab->scene);
    }
}

void PrefabLibrary::ForEachPrefab(const std::function<void(Prefab&)>& fn) {
    std::vector<Prefab*> prefabs;  // a visita pode carregar outros (Get) e mexer no mapa
    for (auto& [guid, cached] : m_cache) {
        if (cached.prefab) prefabs.push_back(cached.prefab.get());
    }
    for (Prefab* prefab : prefabs) fn(*prefab);
}

// --- Operações ------------------------------------------------------------------------

namespace Prefabs {

bool IsInstanceRoot(const Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets) {
    const Entity* e = scene.Find(id);
    const PrefabLink* link = e ? LinkOf(*e) : nullptr;
    if (!link || link->guid.empty()) return false;
    const Prefab* prefab = library.Get(link->guid, assets);
    return prefab && link->source == prefab->rootSource;
}

EntityId InstanceRoot(const Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets) {
    for (const Entity* e = scene.Find(id); e; e = scene.Find(e->parent)) {
        if (IsInstanceRoot(scene, e->id, library, assets)) return e->id;
    }
    return kNullEntity;
}

EntityId Instantiate(const Prefab& prefab, Scene& scene, EntityId parent) {
    CopyPairs copies;
    const EntityId root = CopyTree(prefab.scene, prefab.root, scene, parent, [&](const Entity& source, Entity& copy) {
        LinkOf(copy)->syncedRevision = prefab.revision;
        copies.emplace_back(source.id, copy.id);
    });
    RemapCopied(scene, copies, true);
    return root;
}

Prefab* CreateFromEntity(Scene& scene, EntityId id, const fs::path& file, PrefabLibrary& library, AssetLibrary& assets,
                         std::string* error) {
    if (!scene.Find(id)) {
        SetError(error, "Entidade não encontrada.");
        return nullptr;
    }
    uint32_t next = 1;
    std::vector<std::pair<EntityId, uint32_t>> order;
    std::map<uint32_t, PrefabLink> nested;
    Scene content = BuildPrefabScene(scene, id, {}, [&](const Entity&) { return next++; }, order, nested);
    content.Find(content.Children(kNullEntity).front())->transform.position = glm::vec3(0.0f);

    if (!PrefabLibrary::WriteFile(file, content, assets, error)) return nullptr;
    Prefab* prefab = library.GetFile(file, assets, error);
    if (!prefab) return nullptr;
    // O arquivo não guarda a revisão vista dos aninhados: os valores que acabaram de ser copiados já são os deles.
    for (const auto& [source, link] : nested) {
        if (Entity* e = prefab->FindSource(source)) SetNested(*e, link);
    }

    LinkScene(scene, order, nested, *prefab);
    return prefab;
}

bool UpdateNested(PrefabLibrary& library, AssetLibrary& assets) {
    bool any = false;
    for (int pass = 0; pass < 8; ++pass) {  // um passo por nível: C dentro de B dentro de A
        bool changed = false;
        library.ForEachPrefab([&](Prefab& outer) {
            bool outerChanged = false;
            // Entidade nova vinda do prefab de dentro: ganha um source livre no de fora.
            uint32_t next = 1;
            for (const auto& e : outer.scene.Entities()) next = std::max(next, LinkOf(*e)->source + 1);
            const Adopt adopt = [&](Entity& copy) {
                if (const PrefabLink* inner = LinkOf(copy)) SetNested(copy, *inner);
                SetLink(copy, {outer.guid, next++, {}, 0});
            };
            for (const auto& [root, inner] : InstanceRoots(outer.scene, library, assets, LinkKind::Nested)) {
                if (inner == &outer) continue;  // ciclo (arquivo editado à mão): não segue
                const Entity* e = outer.scene.Find(root);
                if (!e || NestedOf(*e)->syncedRevision == inner->revision) continue;
                SyncInstance(outer.scene, root, *inner, assets, LinkKind::Nested, adopt);
                outerChanged = true;
            }
            if (outerChanged) {
                library.MarkChanged(outer);  // revisão nova: as instâncias na cena seguem
                changed = true;
            }
        });
        if (!changed) break;
        any = true;
    }
    return any;
}

size_t RecordNestedOverrides(Prefab& prefab, PrefabLibrary& library, AssetLibrary& assets) {
    return RecordOverridesOf(prefab.scene, library, assets, LinkKind::Nested, prefab.guid);
}

size_t SyncInstances(Scene& scene, PrefabLibrary& library, AssetLibrary& assets, bool force) {
    UpdateNested(library, assets);
    size_t count = 0;
    for (const auto& [root, prefab] : InstanceRoots(scene, library, assets)) {
        const Entity* e = scene.Find(root);
        if (!e || (!force && LinkOf(*e)->syncedRevision == prefab->revision)) continue;  // pode ter sumido no sync de outra
        SyncInstance(scene, root, *prefab, assets);
        ++count;
    }
    return count;
}

bool NeedsSync(const Scene& scene, PrefabLibrary& library, AssetLibrary& assets) {
    UpdateNested(library, assets);  // um prefab aninhado que mudou muda o de fora
    for (const auto& [root, prefab] : InstanceRoots(scene, library, assets)) {
        if (LinkOf(*scene.Find(root))->syncedRevision != prefab->revision) return true;
    }
    return false;
}

size_t RecordOverrides(Scene& scene, PrefabLibrary& library, AssetLibrary& assets) {
    return RecordOverridesOf(scene, library, assets, LinkKind::Instance);
}

bool ApplyInstance(Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets, std::string* error) {
    const EntityId root = InstanceRoot(scene, id, library, assets);
    if (root == kNullEntity) {
        SetError(error, "A entidade não é parte de uma instância de prefab.");
        return false;
    }
    Prefab& prefab = *library.Get(LinkOf(*scene.Find(root))->guid, assets);

    // Quem já é do prefab mantém o source; o que foi adicionado na instância ganha um novo.
    uint32_t next = 1;
    for (const auto& e : prefab.scene.Entities()) next = std::max(next, LinkOf(*e)->source + 1);
    std::set<uint32_t> used;
    std::vector<std::pair<EntityId, uint32_t>> order;
    std::map<uint32_t, PrefabLink> nested;
    Scene content = BuildPrefabScene(scene, root, prefab.guid, [&](const Entity& from) {
        const PrefabLink* link = LinkOf(from);
        uint32_t source = link && link->guid == prefab.guid ? link->source : 0;
        if (source == 0 || used.contains(source)) source = next++;
        used.insert(source);
        return source;
    }, order, nested);
    if (ContainsPrefab(content, prefab.guid, library, assets)) {
        SetError(error, std::format("Não dá para aplicar: o prefab {} ficaria dentro dele mesmo (uma instância "
                                    "aninhada já o contém).", ToUtf8(assets.Database().PathFor(prefab.guid).stem())));
        return false;
    }
    // A raiz guarda a posição e a rotação do prefab: as da instância são dela.
    Entity& contentRoot = *content.Find(content.Children(kNullEntity).front());
    const Transform& oldRoot = prefab.scene.Find(prefab.root)->transform;
    contentRoot.transform.position = oldRoot.position;
    contentRoot.transform.rotation = oldRoot.rotation;
    for (const auto& e : content.Entities()) LinkOf(*e)->guid = prefab.guid;

    Prefab updated = prefab;
    updated.scene = std::move(content);
    updated.root = contentRoot.id;
    updated.rootSource = LinkOf(contentRoot)->source;
    updated.RebuildIndex();
    // O que a instância mudou nas entidades aninhadas vira override delas no prefab.
    RecordNestedOverrides(updated, library, assets);
    for (const auto& e : updated.scene.Entities()) {
        if (const PrefabLink* link = NestedOf(*e)) nested[LinkOf(*e)->source] = *link;
    }
    if (!library.Save(updated, assets, error)) return false;
    prefab = std::move(updated);
    library.MarkChanged(prefab);

    LinkScene(scene, order, nested, prefab);
    log::Info("Instância aplicada ao prefab {}", assets.KeyForFile(assets.Database().PathFor(prefab.guid)));
    return true;
}

void RevertInstance(Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets) {
    const EntityId root = InstanceRoot(scene, id, library, assets);
    if (root == kNullEntity) return;
    const Prefab& prefab = *library.Get(LinkOf(*scene.Find(root))->guid, assets);
    for (const auto& [source, entity] : Members(scene, root, prefab)) LinkOf(*scene.Find(entity))->overrides.clear();
    SyncInstance(scene, root, prefab, assets);
}

void Unpack(Scene& scene, EntityId id, PrefabLibrary& library, AssetLibrary& assets) {
    const EntityId root = InstanceRoot(scene, id, library, assets);
    if (root == kNullEntity) return;
    const Prefab& prefab = *library.Get(LinkOf(*scene.Find(root))->guid, assets);
    for (const auto& [source, entity] : Members(scene, root, prefab)) {
        Entity& e = *scene.Find(entity);
        e.Remove<PrefabInstance>();
        // Veio de um prefab aninhado: volta a ser instância dele, com o que o prefab de fora mudou como override.
        if (const PrefabLink* nestedLink = NestedOf(e)) {
            PrefabLink link = *nestedLink;
            const Prefab* inner = library.Get(link.guid, assets);
            link.syncedRevision = inner ? inner->revision : 0;
            e.Remove<PrefabNested>();
            SetLink(e, std::move(link));
        }
    }
}

} // namespace Prefabs

} // namespace motor
