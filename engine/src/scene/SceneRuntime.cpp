#include "motor/scene/SceneRuntime.h"

#include "motor/scene/Scene.h"

#include <algorithm>
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

void SceneRuntime::ForEachScript(const std::function<void(Script&, EntityId)>& fn) {
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
            if (c.Type().IsScript()) fn(*c.Type().asScript(c.Data()), id);
        }
    }
}

void SceneRuntime::StartPending() {
    ForEachScript([this](Script& s, EntityId id) {
        if (s.m_started) return;
        Bind(s, id);
        s.m_started = true;  // antes do OnStart: o que ele fizer não o chama de novo
        s.OnStart();
    });
}

void SceneRuntime::RunSystems(bool beforeScripts, void (RuntimeSystem::*hook)(SceneRuntime&, float), float dt) {
    // m_systems não muda durante o Play (não chame Stop() de dentro de um gancho).
    for (SystemSlot& slot : m_systems) {
        if ((slot.info.order < 0) == beforeScripts) (slot.system.get()->*hook)(*this, dt);
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
    // Instâncias novas a cada Start: o estado dos sistemas é deste runtime e deste Play.
    for (RuntimeSystemInfo& info : RegisteredRuntimeSystems()) {
        std::unique_ptr<RuntimeSystem> system = info.create();
        m_systems.push_back({std::move(info), std::move(system)});
    }
    // Todos os sistemas existem antes do primeiro OnStart: um pode achar o outro com GetSystem.
    for (SystemSlot& slot : m_systems) slot.system->OnStart(*this);
    StartPending();
    FlushDestroyed();
}

void SceneRuntime::Update(float dt) {
    if (!m_running) return;
    m_time.delta = dt;
    m_time.elapsed += dt;
    StartPending();
    RunSystems(true, &RuntimeSystem::OnUpdate, dt);
    ForEachScript([this, dt](Script& s, EntityId id) {
        if (!s.m_started) return;
        Bind(s, id);
        s.OnUpdate(dt);
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
    ForEachScript([this, dt](Script& s, EntityId id) {
        if (!s.m_started) return;
        Bind(s, id);
        s.OnFixedUpdate(dt);
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
            if (!s.m_started) continue;
            Bind(s, id);
            s.m_started = false;
            s.OnDestroy();
        }
    }
    // Os sistemas param depois dos scripts (que ainda podem usá-los no OnDestroy), na ordem inversa.
    for (size_t i = m_systems.size(); i-- > 0;) m_systems[i].system->OnStop(*this);
    m_systems.clear();
    m_running = false;
    m_toDestroy.clear();
}

Entity* SceneRuntime::FindEntity(std::string_view name) {
    return m_scene.FindByName(name);
}

Entity& SceneRuntime::CreateEntity(std::string name, EntityId parent) {
    return m_scene.CreateEntity(std::move(name), parent);
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
                    if (!s.m_started) continue;
                    Bind(s, id);
                    s.m_started = false;
                    s.OnDestroy();
                }
            }
            m_scene.DestroyEntity(root);
        }
    }
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

void Script::Destroy(EntityId id) {
    if (m_runtime) m_runtime->Destroy(id);
}

void Script::DestroySelf() {
    Destroy(m_entity);
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

void Script::WriteLog(const std::string& message) {
    const Entity* e = m_runtime ? m_runtime->GetScene().Find(m_entity) : nullptr;
    log::Info("[{}] {}", e ? e->name : std::string("?"), message);
}

} // namespace motor
