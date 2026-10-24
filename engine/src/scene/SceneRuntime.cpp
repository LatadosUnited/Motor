#include "motor/scene/SceneRuntime.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/core/FrameStats.h"
#include "motor/scene/GameHost.h"
#include "motor/scene/Prefab.h"
#include "motor/scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <random>
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
    // componentes durante o laço. As entidades criadas aqui só entram no próximo passo. Cópia da lista: ela é
    // refeita se um script mexer na estrutura da cena durante o laço.
    RefreshScriptHolders();
    const std::vector<EntityId> ids = m_scriptHolders;
    const uint64_t version = SceneStructureVersion();
    const EntityId lastExisting = m_holdersMaxId;  // ids crescem: maiores que este nasceram durante o laço

    auto visit = [&](EntityId id) {
        for (size_t i = 0;; ++i) {
            Entity* e = m_scene.Find(id);
            if (!e || i >= e->components.size() || !m_scene.IsActiveInHierarchy(id)) break;
            ComponentPtr& c = e->components[i];
            if (!c.Type().IsScript()) continue;
            Script& s = *c.Type().asScript(c.Data());
            if (m_faultedScripts.contains(&s)) continue;
            fn(s, id, c.Type().name);
        }
    };
    for (EntityId id : ids) visit(id);

    // Script adicionado durante o laço a uma entidade que já existia e não tinha script: entra ainda nesta passada,
    // como quando o laço percorria a cena inteira (ex.: um OnStart que põe um script em outra entidade).
    if (SceneStructureVersion() == version) return;
    RefreshScriptHolders();
    const std::vector<EntityId> now = m_scriptHolders;
    const std::unordered_set<EntityId> visited(ids.begin(), ids.end());
    for (EntityId id : now) {
        if (id <= lastExisting && !visited.contains(id)) visit(id);
    }
}

void SceneRuntime::RefreshScriptHolders() {
    const auto& entities = m_scene.Entities();
    const uint64_t version = SceneStructureVersion();
    if (version == m_holdersVersion && entities.size() == m_holdersEntityCount &&
        entities.data() == m_holdersEntityData) {
        return;
    }
    MOTOR_PROFILE_SCOPE("Refazer lista de scripts");
    m_scriptHolders.clear();
    m_holdersMaxId = kNullEntity;
    for (const auto& e : entities) {
        m_holdersMaxId = std::max(m_holdersMaxId, e->id);
        for (const ComponentPtr& c : e->components) {
            if (c.Type().IsScript()) {
                m_scriptHolders.push_back(e->id);
                break;
            }
        }
    }
    m_holdersVersion = version;
    m_holdersEntityCount = entities.size();
    m_holdersEntityData = entities.data();
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
        MOTOR_PROFILE_STAT_NAMED(slot.info.name);
        InvokeSystem(slot, hookName, [&]() { (slot.system.get()->*hook)(*this, dt); });
    }
}

const std::vector<std::string>& SceneRuntime::InputContexts() {
    if (!m_inputContextsSet) {
        m_inputContextsSet = true;
        const std::string start = m_host ? m_host->Actions().startContext : InputActions::Defaults().startContext;
        m_inputContexts = start.empty() ? std::vector<std::string>{} : std::vector<std::string>{start};
    }
    return m_inputContexts;
}

void SceneRuntime::SetInputContexts(std::vector<std::string> contexts) {
    m_inputContexts = std::move(contexts);
    m_inputContextsSet = true;
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
    m_timers.clear();
    m_inputContexts.clear();
    m_inputContextsSet = false;
    m_pointerOverUi = m_uiHasMouse = false;
    m_actionTracker.Clear();
    m_random.seed(std::random_device{}());
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
    {
        // Depois da UI dizer se pegou o mouse (sistema antes dos scripts), antes dos scripts lerem.
        static const InputActions defaults = InputActions::Defaults();
        InputFilter filter;
        filter.contexts = &InputContexts();
        filter.uiHasMouse = m_uiHasMouse;
        m_actionTracker.Update(m_host ? m_host->Actions() : defaults, filter, m_inputEnabled, dt);
    }
    RunTimers();
    {
        MOTOR_PROFILE_STAT("Scripts OnUpdate");
        ForEachScript([this, dt](Script& s, EntityId id, std::string_view scriptName) {
            if (!s.m_started) return;
            Bind(s, id);
            InvokeScript(s, id, scriptName, "OnUpdate", [&]() { s.OnUpdate(dt); });
        });
    }
    RunSystems(false, &RuntimeSystem::OnUpdate, dt);
    ++m_time.frame;
    MOTOR_PROFILE_STAT("Destruir entidades");
    FlushDestroyed();
}

void SceneRuntime::FixedUpdate(float dt) {
    if (!m_running) return;
    m_time.fixedDelta = dt;
    StartPending();
    RunSystems(true, &RuntimeSystem::OnFixedUpdate, dt);
    {
        MOTOR_PROFILE_STAT("Scripts OnFixedUpdate");
        ForEachScript([this, dt](Script& s, EntityId id, std::string_view scriptName) {
            if (!s.m_started) return;
            Bind(s, id);
            InvokeScript(s, id, scriptName, "OnFixedUpdate", [&]() { s.OnFixedUpdate(dt); });
        });
    }
    RunSystems(false, &RuntimeSystem::OnFixedUpdate, dt);
    MOTOR_PROFILE_STAT("Destruir entidades");
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
    m_timers.clear();
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

const ComponentType* SceneRuntime::ScriptTypeOf(EntityId id, const Script* script) {
    Entity* e = m_scene.Find(id);
    if (!e) return nullptr;
    for (ComponentPtr& c : e->components) {
        if (c.Type().IsScript() && c.Type().asScript(c.Data()) == script) return &c.Type();
    }
    return nullptr;
}

TimerId SceneRuntime::AddTimer(Script& owner, EntityId entity, float seconds, bool repeat, std::function<void()> fn) {
    if (!fn) return 0;
    const float interval = std::max(seconds, 0.0f);
    const TimerId id = m_nextTimer++;
    m_timers.push_back({id, &owner, entity, m_time.elapsed + interval, interval, repeat, std::move(fn)});
    return id;
}

void SceneRuntime::CancelTimer(TimerId id) {
    std::erase_if(m_timers, [&](const Timer& t) { return t.id == id; });
}

void SceneRuntime::CancelTimers(const Script& owner) {
    std::erase_if(m_timers, [&](const Timer& t) { return t.owner == &owner; });
}

void SceneRuntime::RunTimers() {
    // Só os que já venceram no começo: um temporizador criado ou adiado aqui espera o próximo frame (assim
    // Every(0) dispara uma vez por frame, e não para sempre).
    std::vector<TimerId> due;
    for (const Timer& t : m_timers) {
        if (t.due <= m_time.elapsed) due.push_back(t.id);
    }
    for (TimerId id : due) {
        auto it = std::find_if(m_timers.begin(), m_timers.end(), [&](const Timer& t) { return t.id == id; });
        if (it == m_timers.end()) continue;  // cancelado por outro temporizador
        const ComponentType* type = ScriptTypeOf(it->entity, it->owner);
        if (!type) {  // a entidade ou o script sumiram
            m_timers.erase(it);
            continue;
        }
        Script& owner = *it->owner;
        const EntityId entity = it->entity;
        // Entidade desativada ou script com erro: espera (dispara quando voltar).
        if (!owner.m_started || m_faultedScripts.contains(&owner) || !m_scene.IsActiveInHierarchy(entity)) continue;
        std::function<void()> fn = it->fn;  // cópia: o callback pode mexer na lista
        if (it->repeat) {
            it->due += it->interval;
            if (it->due <= m_time.elapsed) it->due = m_time.elapsed + it->interval;  // atrasado: não acumula
        } else {
            m_timers.erase(it);
        }
        Bind(owner, entity);
        InvokeScript(owner, entity, type->name, "temporizador", fn);
    }
}

void SceneRuntime::Send(EntityId target, const Message& message) {
    CallEntityScripts(target, "OnMessage", [&](Script& s) { s.OnMessage(message); });
}

void SceneRuntime::Broadcast(const Message& message) {
    RefreshScriptHolders();
    const std::vector<EntityId> ids = m_scriptHolders;  // só quem tem script recebe; cópia: OnMessage pode mexer na cena
    for (EntityId id : ids) Send(id, message);
}

float SceneRuntime::Random(float min, float max) {
    if (min > max) std::swap(min, max);
    if (min == max) return min;
    return std::uniform_real_distribution<float>(min, max)(m_random);
}

int SceneRuntime::RandomInt(int min, int max) {
    if (min > max) std::swap(min, max);
    return std::uniform_int_distribution<int>(min, max)(m_random);
}

void SceneRuntime::ForgetScripts() {
    m_timers.clear();
    m_faultedScripts.clear();
}

void SceneRuntime::ForEachPrefabScene(const std::function<void(Scene&)>& fn) {
    if (m_prefabs) m_prefabs->ForEachScene(fn);
}

void SceneRuntime::Destroy(EntityId id) {
    if (m_scene.Find(id)) m_toDestroy.push_back(id);
}

void SceneRuntime::FlushDestroyed() {
    // Em lote: OnDestroy de todas as entidades pedidas (cada raiz seguida dos descendentes), depois uma passada só
    // pela cena para tirá-las (destruir uma por uma percorria a cena a cada entidade: ~8 ms para 200 por quadro no
    // spawn200). OnDestroy pode pedir outras destruições: entram na rodada seguinte, até esvaziar.
    while (!m_toDestroy.empty()) {
        const std::vector<EntityId> pending = std::exchange(m_toDestroy, {});
        for (EntityId id : m_scene.Subtrees(pending)) {
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
        m_scene.DestroyEntities(pending);
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

Entity* Script::GetEntity(EntityId id) {
    return m_runtime && id != kNullEntity ? m_runtime->GetScene().Find(id) : nullptr;
}

TimerId Script::After(float seconds, std::function<void()> fn) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->AddTimer(*this, m_entity, seconds, false, std::move(fn));
}

TimerId Script::Every(float seconds, std::function<void()> fn) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->AddTimer(*this, m_entity, seconds, true, std::move(fn));
}

void Script::CancelTimer(TimerId id) {
    if (m_runtime) m_runtime->CancelTimer(id);
}

void Script::CancelTimers() {
    if (m_runtime) m_runtime->CancelTimers(*this);
}

void Script::Send(EntityId target, std::string_view text, float value) {
    if (m_runtime) m_runtime->Send(target, {text, m_entity, value});
}

void Script::Broadcast(std::string_view text, float value) {
    if (m_runtime) m_runtime->Broadcast({text, m_entity, value});
}

float Script::Random(float min, float max) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->Random(min, max);
}

int Script::RandomInt(int min, int max) {
    MOTOR_ASSERT(m_runtime, "Script usado fora do Play");
    return m_runtime->RandomInt(min, max);
}

bool Script::Chance(float probability) {
    return Random(0.0f, 1.0f) < probability;
}

void Script::SetRandomSeed(uint32_t seed) {
    if (m_runtime) m_runtime->SetRandomSeed(seed);
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
bool Script::IsMouseDown(MouseButton button) const {
    if (button == MouseButton::Left && m_runtime && m_runtime->UiHasMouse()) return false;  // clique na interface
    return InputEnabled() && Input::IsMouseDown(button);
}
bool Script::IsPointerOverUi() const { return m_runtime && m_runtime->PointerOverUi(); }
glm::vec2 Script::MouseDelta() const { return InputEnabled() ? Input::MouseDelta() : glm::vec2(0.0f); }
void Script::SetCursorMode(CursorMode mode) {
    if (m_runtime) m_runtime->SetCursorMode(mode);
}
CursorMode Script::GetCursorMode() const { return m_runtime ? m_runtime->GetCursorMode() : CursorMode::Normal; }

GameOptions Script::GetOptions() const {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return host ? host->Options() : GameOptions{};
}
void Script::ApplyOptions(const GameOptions& options) {
    GameOptions clamped = options;
    clamped.Clamp();
    if (GameHost* host = m_runtime ? m_runtime->Host() : nullptr) host->ApplyOptions(clamped);
}
std::vector<Resolution> Script::Resolutions() const {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return host ? host->Resolutions() : std::vector<Resolution>{};
}
namespace {
// Sem quem rode o jogo (testes): saves só na memória.
SaveStore& FallbackSaves() {
    static SaveStore store;
    return store;
}
} // namespace

const InputActions& Script::Actions() const {
    static const InputActions defaults = InputActions::Defaults();  // sem quem rode o jogo (testes)
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return host ? host->Actions() : defaults;
}
InputFilter Script::ActionFilter(int device) const {
    InputFilter filter(device);
    if (m_runtime) {
        filter.contexts = &m_runtime->InputContexts();
        filter.uiHasMouse = m_runtime->UiHasMouse();
    }
    return filter;
}
bool Script::IsActionDown(std::string_view action, int device) const {
    return InputEnabled() && Actions().IsDown(action, ActionFilter(device));
}
bool Script::WasActionPressed(std::string_view action, int device) const {
    return InputEnabled() && Actions().WasPressed(action, ActionFilter(device));
}
bool Script::WasActionReleased(std::string_view action, int device) const {
    return InputEnabled() && Actions().WasReleased(action, ActionFilter(device));
}
float Script::GetAxis(std::string_view action, int device) const {
    return InputEnabled() ? Actions().Value(action, ActionFilter(device)) : 0.0f;
}
glm::vec2 Script::GetVector(std::string_view action, int device) const {
    return InputEnabled() ? Actions().Vector(action, ActionFilter(device)) : glm::vec2(0.0f);
}
namespace {
bool SameContext(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}
} // namespace
float Script::GetActionHeldTime(std::string_view action, int device) const {
    return InputEnabled() ? m_runtime->ActionTracker().HeldTime(action, device) : 0.0f;
}
bool Script::WasActionHeld(std::string_view action, float seconds, int device) const {
    return InputEnabled() && m_runtime->ActionTracker().WasHeld(action, seconds, device);
}
bool Script::WasActionTapped(std::string_view action, float maxSeconds, int device) const {
    return InputEnabled() && m_runtime->ActionTracker().WasTapped(action, maxSeconds, device);
}
bool Script::WasActionDoubleTapped(std::string_view action, float interval, int device) const {
    return InputEnabled() && m_runtime->ActionTracker().WasDoubleTapped(action, interval, device);
}
bool Script::ConsumeBufferedAction(std::string_view action, float seconds, int device) {
    return InputEnabled() && m_runtime->ActionTracker().ConsumeBuffered(action, seconds, device);
}
void Script::SetInputContext(std::string_view context) {
    if (!m_runtime) return;
    m_runtime->SetInputContexts(context.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(context)});
}
void Script::EnableInputContext(std::string_view context, bool enabled) {
    if (!m_runtime || context.empty()) return;
    std::vector<std::string> contexts = m_runtime->InputContexts();
    std::erase_if(contexts, [&](const std::string& c) { return SameContext(c, context); });
    if (enabled) contexts.emplace_back(context);
    m_runtime->SetInputContexts(std::move(contexts));
}
bool Script::IsInputContextActive(std::string_view context) const {
    if (!m_runtime) return true;
    const auto& contexts = m_runtime->InputContexts();
    return std::any_of(contexts.begin(), contexts.end(), [&](const std::string& c) { return SameContext(c, context); });
}
const std::string& Script::TextInput() const {
    static const std::string none;
    return InputEnabled() ? Input::TextInput() : none;
}
bool Script::IsGamepadConnected(int pad) const {
    return pad == kAnyDevice ? Input::IsGamepadConnected() : Input::IsGamepadConnected(pad);
}
int Script::GamepadCount() const { return Input::GamepadCount(); }
// Ligar e desligar não dependem do foco: o jogo precisa saber mesmo com o editor em outro painel.
bool Script::WasGamepadConnected(int pad) const { return Input::WasGamepadConnected(pad); }
bool Script::WasGamepadDisconnected(int pad) const { return Input::WasGamepadDisconnected(pad); }
bool Script::IsGamepadButtonDown(GamepadButton button, int pad) const {
    return InputEnabled() && Input::IsGamepadButtonDown(button, pad);
}
bool Script::WasGamepadButtonPressed(GamepadButton button, int pad) const {
    return InputEnabled() && Input::WasGamepadButtonPressed(button, pad);
}
float Script::GamepadAxisValue(GamepadAxis axis, int pad) const {
    return InputEnabled() ? Input::GamepadAxisValue(axis, pad) : 0.0f;
}
InputDevice Script::LastInputDevice() const { return Input::LastDevice(); }

SaveData& Script::SaveSlot(std::string_view slot) {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return (host ? host->Saves() : FallbackSaves()).Slot(slot);
}
bool Script::HasSave(std::string_view slot) const {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return (host ? host->Saves() : FallbackSaves()).Exists(slot);
}
void Script::DeleteSave(std::string_view slot) {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    (host ? host->Saves() : FallbackSaves()).Delete(slot);
}
std::filesystem::path Script::SaveFolder() const {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return host ? host->Saves().Folder() : std::filesystem::path();
}

std::string Script::GameVersion() const {
    GameHost* host = m_runtime ? m_runtime->Host() : nullptr;
    return host ? host->GameVersion() : std::string();
}
void Script::QuitGame() {
    if (GameHost* host = m_runtime ? m_runtime->Host() : nullptr) host->QuitGame();
}

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
