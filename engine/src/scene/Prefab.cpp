#include "motor/scene/Prefab.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
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

// Põe o vínculo na entidade. Componente novo vai para o início (aparece no topo do Inspector).
void SetLink(Entity& e, PrefabLink link) {
    if (PrefabLink* existing = LinkOf(e)) {
        *existing = std::move(link);
        return;
    }
    const ComponentType& type = *ComponentRegistry::TypeOf<PrefabInstance>();
    e.components.insert(e.components.begin(), ComponentPtr(type));
    static_cast<PrefabInstance*>(e.components.front().Data())->link = std::move(link);
}

// Componentes que o sync e os overrides ignoram: o próprio vínculo e os de tipo desconhecido.
bool Skipped(const ComponentType& type) {
    return &type == ComponentRegistry::TypeOf<PrefabInstance>() || &type == &ComponentRegistry::MissingType();
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
std::map<uint32_t, EntityId> Members(const Scene& scene, EntityId root, const Prefab& prefab) {
    std::map<uint32_t, EntityId> members;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        const Entity* e = scene.Find(id);
        if (!e) return;
        const PrefabLink* link = LinkOf(*e);
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
                                                        AssetLibrary& assets) {
    std::vector<std::pair<EntityId, Prefab*>> roots;
    for (const auto& e : scene.Entities()) {
        const PrefabLink* link = LinkOf(*e);
        if (!link || link->guid.empty()) continue;
        Prefab* prefab = library.Get(link->guid, assets);
        if (prefab && link->source == prefab->rootSource) roots.emplace_back(e->id, prefab);
    }
    return roots;
}

// Valores do prefab nas propriedades sem override.
void ApplyEntity(Entity& e, const Entity& p, bool isRoot, AssetLibrary& assets) {
    const std::vector<std::string> overrides = LinkOf(e)->overrides;  // cópia: os componentes vão mudar
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
            if (!has("-" + type.name)) e.components.push_back(pc);  // o prefab ganhou este componente
            continue;
        }
        for (const FieldInfo& f : type.fields) {
            if (!has(type.name + "." + f.key)) CopyField(f, pc.Data(), data, assets);
        }
    }
    // Componente que o prefab perdeu (e que não foi adicionado na instância).
    std::erase_if(e.components, [&](const ComponentPtr& c) {
        return !Skipped(c.Type()) && !p.Get(c.Type()) && !has("+" + c.Type().name);
    });
}

void SyncInstance(Scene& scene, EntityId rootId, const Prefab& prefab, AssetLibrary& assets) {
    // Entidades que o prefab não tem mais.
    std::vector<EntityId> gone;
    for (const auto& [source, id] : Members(scene, rootId, prefab)) {
        if (id != rootId && !prefab.FindSource(source)) gone.push_back(id);
    }
    for (EntityId id : gone) scene.DestroyEntity(id);

    std::map<uint32_t, EntityId> members = Members(scene, rootId, prefab);
    members[prefab.rootSource] = rootId;
    const PrefabLink rootLink = *LinkOf(*scene.Find(rootId));

    // Percorre o prefab dos pais para os filhos: entidade que falta é criada debaixo do pai correspondente.
    std::function<void(EntityId, EntityId)> visit = [&](EntityId prefabId, EntityId instanceParent) {
        const Entity& p = *prefab.scene.Find(prefabId);
        const uint32_t source = LinkOf(p)->source;
        EntityId target = kNullEntity;
        if (auto it = members.find(source); it != members.end()) {
            target = it->second;
            ApplyEntity(*scene.Find(target), p, source == prefab.rootSource, assets);
        } else {
            if (rootLink.HasOverride(std::format("-#{}", source)) || instanceParent == kNullEntity) return;
            target = CopyTree(prefab.scene, prefabId, scene, instanceParent, nullptr);  // filhos vêm junto
            for (const auto& [s, id] : Members(scene, target, prefab)) members.emplace(s, id);
            return;
        }
        for (EntityId child : prefab.scene.Children(prefabId)) visit(child, target);
    };
    visit(prefab.root, kNullEntity);

    for (const auto& [source, id] : Members(scene, rootId, prefab)) LinkOf(*scene.Find(id))->syncedRevision = prefab.revision;
}

// Overrides de uma entidade: o que ficou diferente do prefab.
size_t RecordEntity(Entity& e, const Entity& p, bool isRoot) {
    PrefabLink& link = *LinkOf(e);
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
            if (!FieldEqual(f, data, pc.Data())) mark(type.name + "." + f.key);
        }
    }
    for (const ComponentPtr& c : e.components) {
        if (!Skipped(c.Type()) && !p.Get(c.Type())) mark("+" + c.Type().name);
    }
    return added;
}

// Copia a sub-árvore de `rootId` para uma cena de prefab, dando a cada entidade um source (`sourceOf`).
// A raiz fica na origem: a posição de cada instância é dela.
Scene BuildPrefabScene(const Scene& scene, EntityId rootId, const std::function<uint32_t(const Entity&)>& sourceOf,
                       std::vector<std::pair<EntityId, uint32_t>>& order) {
    Scene out;
    CopyTree(scene, rootId, out, kNullEntity, [&](const Entity& from, Entity& to) {
        const uint32_t source = sourceOf(from);
        to.Remove(*ComponentRegistry::TypeOf<PrefabInstance>());
        SetLink(to, {{}, source, {}, 0});
        order.emplace_back(from.id, source);
    });
    return out;
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
    return CopyTree(prefab.scene, prefab.root, scene, parent,
                    [&](const Entity&, Entity& copy) { LinkOf(copy)->syncedRevision = prefab.revision; });
}

Prefab* CreateFromEntity(Scene& scene, EntityId id, const fs::path& file, PrefabLibrary& library, AssetLibrary& assets,
                         std::string* error) {
    if (!scene.Find(id)) {
        SetError(error, "Entidade não encontrada.");
        return nullptr;
    }
    uint32_t next = 1;
    std::vector<std::pair<EntityId, uint32_t>> order;
    Scene content = BuildPrefabScene(scene, id, [&](const Entity&) { return next++; }, order);
    content.Find(content.Children(kNullEntity).front())->transform.position = glm::vec3(0.0f);

    if (!PrefabLibrary::WriteFile(file, content, assets, error)) return nullptr;
    Prefab* prefab = library.GetFile(file, assets, error);
    if (!prefab) return nullptr;

    for (const auto& [entity, source] : order) SetLink(*scene.Find(entity), {prefab->guid, source, {}, prefab->revision});
    return prefab;
}

size_t SyncInstances(Scene& scene, PrefabLibrary& library, AssetLibrary& assets, bool force) {
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
    for (const auto& [root, prefab] : InstanceRoots(scene, library, assets)) {
        if (LinkOf(*scene.Find(root))->syncedRevision != prefab->revision) return true;
    }
    return false;
}

size_t RecordOverrides(Scene& scene, PrefabLibrary& library, AssetLibrary& assets) {
    size_t count = 0;
    for (const auto& [root, prefab] : InstanceRoots(scene, library, assets)) {
        Entity& rootEntity = *scene.Find(root);
        // Instância que ainda não viu a revisão atual: diferenças agora são do prefab, não overrides.
        if (LinkOf(rootEntity)->syncedRevision != prefab->revision) continue;
        const std::map<uint32_t, EntityId> members = Members(scene, root, *prefab);
        for (const auto& [source, id] : members) {
            if (const Entity* p = prefab->FindSource(source)) {
                count += RecordEntity(*scene.Find(id), *p, source == prefab->rootSource);
            }
        }
        // Entidades do prefab apagadas na instância.
        for (const auto& e : prefab->scene.Entities()) {
            const uint32_t source = LinkOf(*e)->source;
            if (source != prefab->rootSource && !members.contains(source)) {
                if (LinkOf(rootEntity)->AddOverride(std::format("-#{}", source))) ++count;
            }
        }
    }
    return count;
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
    Scene content = BuildPrefabScene(scene, root, [&](const Entity& from) {
        const PrefabLink* link = LinkOf(from);
        uint32_t source = link && link->guid == prefab.guid ? link->source : 0;
        if (source == 0 || used.contains(source)) source = next++;
        used.insert(source);
        return source;
    }, order);
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
    if (!library.Save(updated, assets, error)) return false;
    prefab = std::move(updated);
    library.MarkChanged(prefab);

    for (const auto& [entity, source] : order) SetLink(*scene.Find(entity), {prefab.guid, source, {}, prefab.revision});
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
    for (const auto& [source, entity] : Members(scene, root, prefab)) scene.Find(entity)->Remove<PrefabInstance>();
}

} // namespace Prefabs

} // namespace motor
