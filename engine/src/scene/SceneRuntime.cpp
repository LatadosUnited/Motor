#include "motor/scene/SceneRuntime.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Prefab.h"
#include "motor/scene/Scene.h"

#include <algorithm>
#include <exception>
#include <format>
#include <utility>

namespace motor {

// --- Registro dos sistemas ----------------------------------------------------------

namespace {
// Estática local: os registradores rodam na inicialização estática, em qualquer ordem entre os .cpp.
std::vector<RuntimeSystemInfo>& SystemRegistry() {
    static std::vector<RuntimeSystemInfo> systems;
    return systems;
}
} // namespace

void RegisterRuntimeSystem(RuntimeSystemInfo info) {
    MOTOR_ASSERT(info.cppType && info.create, "RegisterRuntimeSystem: tipo e fábrica são obrigatórios");
    UnregisterRuntimeSystem(*info.cppType);  // registrar de novo o mesmo tipo substitui
    SystemRegistry().push_back(std::move(info));
}

void UnregisterRuntimeSystem(const std::type_info& cppType) {
    std::erase_if(SystemRegistry(), [&](const RuntimeSystemInfo& s) { return *s.cppType == cppType; });
}

std::vector<RuntimeSystemInfo> RegisteredRuntimeSystems() {
    std::vector<RuntimeSystemInfo> systems = SystemRegistry();
    std::sort(systems.begin(), systems.end(), [](const RuntimeSystemInfo& a, const RuntimeSystemInfo& b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return systems;
}

// --- SceneRuntime -------------------------------------------------------------------

SceneRuntime::SceneRuntime(Scene& scene) : m_scene(scene) {}

SceneRuntime::~SceneRuntime() {
    Stop();
}

void SceneRuntime::Bind(Script& script, EntityId id) {
    // Refeito a cada chamada: barato, e vale mesmo para scripts copiados (cópia começa sem dono).
    script.m_runtime = this;
    script.m_entity = id;
}

void SceneRuntime::ReportScriptError(EntityId id, std::string_view scriptName, std::string_view callback,
                                     std::string_view what) {
    const Entity* e = m_scene.Find(id);
    const std::string_view entityName = e ? std::string_view(e->name) : std::string_view("?");
    m_lastError = std::format("Erro no script {}::{} da entidade '{}' (#{}): {}", scriptName, callback,
                              entityName, id, what);
    m_hasNewError = true;
    ++m_errorCount;
    log::Error("{}", m_lastError);
}

void SceneRuntime::ReportSystemError(std::string_view systemName, std::string_view callback,
                                     std::string_view what) {
    m_lastError = std::format("Erro no sistema {}::{}: {}", systemName, callback, what);
    m_hasNewError = true;
    ++m_errorCount;
    log::Error("{}", m_lastError);
}

void SceneRuntime::InvokeScript(Script& script, EntityId id, std::string_view scriptName,
                                std::string_view callback, const std::function<void()>& fn) {
    try {
        fn();
    } catch (const std::exception& ex) {
        m_faultedScripts.insert(&script);
        ReportScriptError(id, scriptName, callback, ex.what());
    } catch (...) {
        m_faultedScripts.insert(&script);
        ReportScriptError(id, scriptName, callback, "exceção desconhecida");
    }
}

void SceneRuntime::InvokeSystem(SystemSlot& slot, std::string_view callback,
                                const std::function<void()>& fn) {
    try {
        fn();
    } catch (const std::exception& ex) {
        slot.faulted = true;
        ReportSystemError(slot.info.name, callback, ex.what());
    } catch (...) {
        slot.faulted = true;
        ReportSystemError(slot.info.name, callback, "exceção desconhecida");
    }
}

void SceneRuntime::ForEachScript(const std::function<void(Script&, EntityId, std::string_view)>& fn) {
    // Por id e por índice: os scripts podem criar entidades (o vetor da cena cresce) ou adicionar
    // componentes durante o laço. As entidades criadas aqui só entram no próximo passo.
    std::vector<EntityId> ids;
    ids.reserve(m_scene.Entities().size());
    for (const auto& e : m_scene.Entities()) ids.push_back(e->id);

    for (EntityId id : ids) {
        for (size_t i = 0;; ++i) {
            Entity* e = m_scene.Find(id);
            if (!e || i >= e->components.size() || !m_scene.IsActiveInHierarchy(id)) break;
            ComponentPtr& c = e->components[i];
            if (!c.Type().IsScript()) continue;
            Script& s = *c.Type().asScript(c.Data());
            if (m_faultedScripts.contains(&s)) continue;
            fn(s, id, c.Type().name);
        }
    }
}

void SceneRuntime::CallEntityScripts(EntityId id, std::string_view callback,
                                     const std::function<void(Script&)>& fn) {
    for (size_t i = 0;; ++i) {
        Entity* e = m_scene.Find(id);
        if (!e || i >= e->components.size() || !m_scene.IsActiveInHierarchy(id)) break;
        ComponentPtr& c = e->components[i];
        if (!c.Type().IsScript()) continue;
        Script& s = *c.Type().asScript(c.Data());
        if (!s.m_started || m_faultedScripts.contains(&s)) continue;
        Bind(s, id);
        InvokeScript(s, id, c.Type().name, callback, [&]() { fn(s); });
    }
}

void SceneRuntime::StartPending() {
    ForEachScript([this](Script& s, EntityId id, std::string_view scriptName) {
        if (s.m_started) return;
        Bind(s, id);
        s.m_started = true;  // antes do OnStart: o que ele fizer não o chama de novo
        InvokeScript(s, id, scriptName, "OnStart", [&]() { s.OnStart(); });
    });
}

void SceneRuntime::RunSystems(bool beforeScripts, void (RuntimeSystem::*hook)(SceneRuntime&, float), float dt) {
    // m_systems não muda durante o Play (não chame Stop() de dentro de um gancho).
    const std::string_view hookName = (hook == &RuntimeSystem::OnFixedUpdate) ? "OnFixedUpdate" : "OnUpdate";
    for (SystemSlot& slot : m_systems) {
        if (slot.faulted || (slot.info.order < 0) != beforeScripts) continue;
        InvokeSystem(slot, hookName, [&]() { (slot.system.get()->*hook)(*this, dt); });
    }
}

RuntimeSystem* SceneRuntime::FindSystem(const std::type_info& type) {
    for (SystemSlot& slot : m_systems) {
        if (*slot.info.cppType == type) return slot.system.get();
    }
    return nullptr;
}

void SceneRuntime::Start() {
    if (m_running) return;
    m_running = true;
    m_time = {};
    m_faultedScripts.clear();
    m_lastError.clear();
    m_hasNewError = false;
    m_errorCount = 0;
    // Instâncias novas a cada Start: o estado dos sistemas é deste runtime e deste Play.
    for (RuntimeSystemInfo& info : RegisteredRuntimeSystems()) {
        try {
            std::unique_ptr<RuntimeSystem> system = info.create();
            if (system) m_systems.push_back({std::move(info), std::move(system), false});
        } catch (const std::exception& ex) {
            ReportSystemError(info.name, "create", ex.what());
        } catch (...) {
            ReportSystemError(info.name, "create", "exceção desconhecida");
        }
    }
    // Todos os sistemas existem antes do primeiro OnStart: um pode achar o outro com GetSystem.
    for (SystemSlot& slot : m_systems) {
        if (!slot.faulted) InvokeSystem(slot, "OnStart", [&]() { slot.system->OnStart(*this); });
    }
    StartPending();
    FlushDestroyed();
}

void SceneRuntime::Update(float dt) {
    if (!m_running) return;
    m_time.delta = dt;
    m_time.elapsed += dt;
    StartPending();
    RunSystems(true, &RuntimeSystem::OnUpdate, dt);
    ForEachScript([this, dt](Script& s, EntityId id, std::string_view scriptName) {
        if (!s.m_started) return;
        Bind(s, id);
        InvokeScript(s, id, scriptName, "OnUpdate", [&]() { s.OnUpdate(dt); });
    });
    RunSystems(false, &RuntimeSystem::OnUpdate, dt);
    ++m_time.frame;
    FlushDestroyed();
}

void SceneRuntime::FixedUpdate(float dt) {
    if (!m_running) return;
    m_time.fixedDelta = dt;
    StartPending();
    RunSystems(true, &RuntimeSystem::OnFixedUpdate, dt);
    ForEachScript([this, dt](Script& s, EntityId id, std::string_view scriptName) {
        if (!s.m_started) return;
        Bind(s, id);
        InvokeScript(s, id, scriptName, "OnFixedUpdate", [&]() { s.OnFixedUpdate(dt); });
    });
    RunSystems(false, &RuntimeSystem::OnFixedUpdate, dt);
    FlushDestroyed();
}

void SceneRuntime::Stop() {
    if (!m_running) return;
    // Todos que iniciaram recebem OnDestroy, inclusive os de entidades desativadas durante o jogo.
    std::vector<EntityId> ids;
    for (const auto& e : m_scene.Entities()) ids.push_back(e->id);
    for (EntityId id : ids) {
        for (size_t i = 0;; ++i) {
            Entity* e = m_scene.Find(id);
            if (!e || i >= e->components.size()) break;
            ComponentPtr& c = e->components[i];
            if (!c.Type().IsScript()) continue;
            Script& s = *c.Type().asScript(c.Data());
            const bool faulted = m_faultedScripts.erase(&s) > 0;
            if (!s.m_started) continue;
            s.m_started = false;
            if (faulted) continue;
            Bind(s, id);
            InvokeScript(s, id, c.Type().name, "OnDestroy", [&]() { s.OnDestroy(); });
        }
    }
    // Os sistemas param depois dos scripts (que ainda podem usá-los no OnDestroy), na ordem inversa.
    for (size_t i = m_systems.size(); i-- > 0;) {
        SystemSlot& slot = m_systems[i];
        if (!slot.faulted) InvokeSystem(slot, "OnStop", [&]() { slot.system->OnStop(*this); });
    }
    m_systems.clear();
    m_faultedScripts.clear();
    m_prefabs.reset();
    m_fallbackAssets.reset();
    m_running = false;
    m_toDestroy.clear();
}

Entity* SceneRuntime::FindEntity(std::string_view name) {
    return m_scene.FindByName(name);
}

Entity& SceneRuntime::CreateEntity(std::string name, EntityId parent) {
    return m_scene.CreateEntity(std::move(name), parent);
}

EntityId SceneRuntime::Instantiate(const PrefabRef& ref) {
    if (ref.IsEmpty() && ref.file.empty()) {
        log::Warn("Instantiate: prefab vazio");
        return kNullEntity;
    }
    if (!m_prefabs) m_prefabs = std::make_unique<PrefabLibrary>();
    AssetLibrary* assets = m_assets;
    if (!assets) {
        if (!m_fallbackAssets) m_fallbackAssets = std::make_unique<AssetLibrary>();
        assets = m_fallbackAssets.get();
        if (assets->Root().empty() && !ref.file.empty()) {
            for (std::filesystem::path p = ref.file.parent_path(); !p.empty() && p != p.parent_path();
                 p = p.parent_path()) {
                if (p.filename() == "Assets") {
                    assets->SetRoot(p.parent_path());
                    break;
                }
            }
            if (assets->Root().empty()) assets->SetRoot(ref.file.parent_path());
        }
    }
    std::string error;
    Prefab* prefab = nullptr;
    if (!ref.guid.empty()) prefab = m_prefabs->Get(ref.guid, *assets, &error);
    if (!prefab && !ref.file.empty()) prefab = m_prefabs->GetFile(ref.file, *assets, &error);
    if (!prefab && !ref.path.empty()) {
        const std::filesystem::path file = assets->FileForKey(ref.path);
        if (!file.empty()) prefab = m_prefabs->GetFile(file, *assets, &error);
    }
    if (!prefab) {
        log::Warn("Instantiate(\"{}\"): {}", ref.path.empty() ? ref.guid : ref.path,
                  error.empty() ? "prefab não encontrado" : error);
        return kNullEntity;
    }
    return Prefabs::Instantiate(*prefab, m_scene);
}

EntityId SceneRuntime::Instantiate(const PrefabRef& ref, const glm::vec3& position) {
    const EntityId id = Instantiate(ref);
    if (Entity* e = m_scene.Find(id)) e->transform.position = position;
    return id;
}

void SceneRuntime::Destroy(EntityId id) {
    if (m_scene.Find(id)) m_toDestroy.push_back(id);
}

void SceneRuntime::FlushDestroyed() {
    // OnDestroy pode pedir outras destruições: repete até esvaziar.
    while (!m_toDestroy.empty()) {
        const std::vector<EntityId> pending = std::exchange(m_toDestroy, {});
        for (EntityId root : pending) {
            if (!m_scene.Find(root)) continue;  // pedida duas vezes, ou já foi junto com o pai

            std::vector<EntityId> doomed{root};
            for (const auto& e : m_scene.Entities()) {
                if (m_scene.IsDescendantOf(e->id, root)) doomed.push_back(e->id);
            }
            for (EntityId id : doomed) {
                for (size_t i = 0;; ++i) {
                    Entity* e = m_scene.Find(id);
                    if (!e || i >= e->components.size()) break;
                    ComponentPtr& c = e->components[i];
                    if (!c.Type().IsScript()) continue;
                    Script& s = *c.Type().asScript(c.Data());
                    const bool faulted = m_faultedScripts.erase(&s) > 0;
                    if (!s.m_started) continue;
                    s.m_started = false;
                    if (faulted) continue;
                    Bind(s, id);
                    InvokeScript(s, id, c.Type().name, "OnDestroy", [&]() { s.OnDestroy(); });
                }
            }
            m_scene.DestroyEntity(root);
        }
    }
}

std::filesystem::path FindSceneFile(const AssetLibrary& assets, std::string_view scene) {
    if (scene.empty() || assets.Root().empty()) return {};
    std::string key(scene);
    const std::string extension = kSceneFileExtension;
    if (!key.ends_with(extension)) key += extension;
    if (AssetLibrary::IsFileKey(key)) {
        const std::filesystem::path file = assets.FileForKey(key);
        std::error_code ec;
        if (!file.empty() && std::filesystem::is_regular_file(file, ec)) return file;
    }
    // Só o nome: a primeira cena do projeto com esse nome de arquivo (ScanFiles vem em ordem alfabética).
    const std::string name = ToUtf8(PathFromUtf8(key).filename());
    for (const std::string& candidate : assets.ScanFiles(AssetType::Scene)) {
        if (ToUtf8(PathFromUtf8(candidate).filename()) == name) return assets.FileForKey(candidate);
    }
    return {};
}

// --- Script -----------------------------------------------------------------------

Entity& Script::GetEntity() {
    Entity* e = m_runtime ? m_runtime->GetScene().Find(m_entity) : nullptr;
    MOTOR_ASSERT(e, "Script usado fora do Play ou depois de a entidade ser destruída");
    return *e;
}

Transform& Script::GetTransform() {
    return GetEntity().transform;
}

Scene& Script::GetScene() {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->GetScene();
}

Entity* Script::FindEntity(std::string_view name) {
    return GetScene().FindByName(name);
}

Entity& Script::CreateEntity(std::string name, EntityId parent) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->CreateEntity(std::move(name), parent);
}

Entity* Script::Instantiate(const PrefabRef& prefab) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->GetScene().Find(m_runtime->Instantiate(prefab));
}

Entity* Script::Instantiate(const PrefabRef& prefab, const glm::vec3& position) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->GetScene().Find(m_runtime->Instantiate(prefab, position));
}

void Script::Destroy(EntityId id) {
    if (m_runtime) m_runtime->Destroy(id);
}

void Script::DestroySelf() {
    Destroy(m_entity);
}

void Script::LoadScene(std::string_view scene) {
    if (m_runtime) m_runtime->RequestSceneLoad(std::string(scene));
}

const GameTime& Script::Time() const {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->Time();
}

RuntimeSystem* Script::FindSystem(const std::type_info& type) {
    return m_runtime ? m_runtime->FindSystem(type) : nullptr;
}

bool Script::InputEnabled() const {
    return m_runtime && m_runtime->InputEnabled();
}

bool Script::IsKeyDown(Key key) const { return InputEnabled() && Input::IsKeyDown(key); }
bool Script::WasKeyPressed(Key key) const { return InputEnabled() && Input::WasKeyPressed(key); }
bool Script::IsMouseDown(MouseButton button) const { return InputEnabled() && Input::IsMouseDown(button); }
glm::vec2 Script::MouseDelta() const { return InputEnabled() ? Input::MouseDelta() : glm::vec2(0.0f); }

} // namespace motor

template <>
struct std::formatter<motor::detail::LogArg> {
    std::string_view spec;

    constexpr auto parse(std::format_parse_context& ctx) {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != '}') ++it;
        spec = std::string_view(ctx.begin(), static_cast<size_t>(it - ctx.begin()));
        return it;
    }

    auto format(const motor::detail::LogArg& arg, std::format_context& ctx) const {
        std::string subFmt = "{:";
        subFmt.append(spec);
        subFmt.push_back('}');
        std::string s;
        switch (arg.kind) {
            case motor::detail::LogArg::Kind::Int: s = std::vformat(subFmt, std::make_format_args(arg.i)); break;
            case motor::detail::LogArg::Kind::UInt: s = std::vformat(subFmt, std::make_format_args(arg.u)); break;
            case motor::detail::LogArg::Kind::Double: s = std::vformat(subFmt, std::make_format_args(arg.d)); break;
            case motor::detail::LogArg::Kind::Bool: s = std::vformat(subFmt, std::make_format_args(arg.b)); break;
            case motor::detail::LogArg::Kind::String: s = std::vformat(subFmt, std::make_format_args(arg.s)); break;
        }
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

namespace motor {

void Script::WriteLog(std::string_view fmt, const detail::LogArg* args, size_t count) {
    const Entity* e = m_runtime ? m_runtime->GetScene().Find(m_entity) : nullptr;
    const std::string_view entityName = e ? std::string_view(e->name) : std::string_view("?");
    if (count == 0 || !args) {
        log::Info("[{}] {}", entityName, fmt);
        return;
    }
    try {
        const detail::LogArg empty(std::string_view{});
        auto at = [&](size_t i) -> const detail::LogArg& { return i < count ? args[i] : empty; };
        const std::string message = std::vformat(
            fmt, std::make_format_args(at(0), at(1), at(2), at(3), at(4), at(5), at(6), at(7)));
        log::Info("[{}] {}", entityName, message);
    } catch (const std::format_error&) {
        log::Info("[{}] {}", entityName, fmt);
    }
}

} // namespace motor
