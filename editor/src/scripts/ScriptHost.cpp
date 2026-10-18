#include "scripts/ScriptHost.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/ScriptModule.h>

#include <algorithm>
#include <exception>
#include <format>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace editor {

namespace fs = std::filesystem;
using motor::ToUtf8;

namespace {
#ifdef _WIN32
std::string LastError() {
    const DWORD code = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message = buffer ? ToUtf8(fs::path(buffer)) : std::format("erro {}", code);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) message.pop_back();
    return message;
}

void* OpenLibrary(const fs::path& path, std::string& error) {
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) error = LastError();
    return module;
}
void CloseLibrary(void* module) { FreeLibrary(static_cast<HMODULE>(module)); }
void* Symbol(void* module, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
}
#else
void* OpenLibrary(const fs::path&, std::string& error) {
    error = "carregar scripts só é suportado no Windows por enquanto";
    return nullptr;
}
void CloseLibrary(void*) {}
void* Symbol(void*, const char*) { return nullptr; }
#endif

// Apaga as cópias que sobraram (de recargas desta ou de outras sessões). As que estão em uso por outro
// editor aberto não podem ser apagadas, e ficam.
void RemoveStaleCopies(const fs::path& loadDir, const fs::path& keep) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(loadDir, ec)) {
        if (entry.path().extension() != ".dll" || entry.path() == keep) continue;
        std::error_code removeError;
        fs::remove(entry.path(), removeError);
    }
}
} // namespace

std::vector<std::string> DescribeLostFields(const std::vector<motor::SceneSerializer::FieldWarning>& lost) {
    // Um aviso por campo de cada tipo (as fotos do desfazer repetem as mesmas entidades).
    struct Group {
        const motor::SceneSerializer::FieldWarning* first;
        std::unordered_set<std::string> entities;
    };
    std::vector<Group> groups;
    for (const auto& w : lost) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) {
            return g.first->type == w.type && g.first->field == w.field && g.first->typeChanged == w.typeChanged;
        });
        if (it == groups.end()) {
            groups.push_back({&w, {}});
            it = groups.end() - 1;
        }
        it->entities.insert(w.entity);
    }
    std::vector<std::string> messages;
    for (const Group& g : groups) {
        const auto& w = *g.first;
        const std::string where = g.entities.size() == 1 ? std::format("entidade '{}'", w.entity)
                                                         : std::format("{} entidades, ex.: '{}'", g.entities.size(), w.entity);
        if (w.typeChanged) {
            messages.push_back(std::format("{}.{} mudou de tipo: o valor salvo ({}) não serve mais e ficou o padrão ({}).",
                                           w.type, w.field, w.value, where));
        } else {
            messages.push_back(std::format("{}.{} não existe mais: o valor salvo ({}) foi descartado ({}). Se o campo foi "
                                           "renomeado, ponha {{.formerKey = \"{}\"}} no .Field novo (bloco MOTOR_SCRIPT).",
                                           w.type, w.field, w.value, where, w.field));
        }
    }
    return messages;
}

ScriptHost::Result ScriptHost::Load(const fs::path& dll, const fs::path& loadDir, const ForEachScene& scenes,
                                    motor::AssetLibrary& assets) {
    Result result;
    std::error_code ec;
    if (!fs::is_regular_file(dll, ec)) {
        result.error = std::format("DLL de scripts não encontrada: '{}'.", ToUtf8(dll));
        return result;
    }

    // 1. Carrega uma cópia: a DLL original continua livre para a próxima compilação.
    fs::create_directories(loadDir, ec);
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = 0;
#endif
    const fs::path copy = loadDir / std::format("{}-{}-{}.dll", ToUtf8(dll.stem()), pid, ++m_counter);
    fs::copy_file(dll, copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.error = std::format("Não foi possível copiar a DLL de scripts: {}", ec.message());
        return result;
    }
    std::string error;
    void* module = OpenLibrary(copy, error);
    if (!module) {
        result.error = std::format("Não foi possível carregar a DLL de scripts: {}", error);
        fs::remove(copy, ec);
        return result;
    }

    // 2. Confere a interface antes de mexer em qualquer coisa: se falhar, os scripts antigos continuam.
    const auto version = reinterpret_cast<motor::ScriptApiVersionFn>(Symbol(module, motor::kScriptApiVersionSymbol));
    const auto registerFn = reinterpret_cast<motor::RegisterScriptsFn>(Symbol(module, motor::kRegisterScriptsSymbol));
    if (!version || !registerFn) {
        result.error = "A DLL não tem as funções de registro de scripts do Motor (MotorRegisterScripts).";
    } else if (version() != motor::kScriptApiVersion) {
        result.error = std::format("A DLL de scripts foi compilada para outra versão do Motor (API {}, esperado {}). "
                                   "Recompile os scripts.", version(), motor::kScriptApiVersion);
    }
    if (!result.error.empty()) {
        CloseLibrary(module);
        fs::remove(copy, ec);
        return result;
    }

    // 3. Tira os scripts antigos da cena (os dados ficam no JSON) e descarrega a DLL antiga.
    result.detached = [&] {
        size_t n = 0;
        const std::unordered_set<const motor::ComponentType*> old(m_types.begin(), m_types.end());
        scenes([&](motor::Scene& scene) {
            n += motor::SceneSerializer::DetachComponents(
                scene, [&](const motor::ComponentType& t) { return old.contains(&t); }, assets);
        });
        return n;
    }();
    UnloadCurrent();

    // 4. Registra os tipos novos. Nome repetido (com a engine ou entre scripts) é recusado.
    motor::ComponentRegistry& registry = motor::ComponentRegistry::Instance();
    std::unordered_set<const motor::ComponentType*> existing;
    for (const auto& t : registry.Types()) existing.insert(t.get());
    try {
        registerFn(registry);
    } catch (const std::exception& e) {
        motor::log::Error("Scripts: erro ao registrar: {}", e.what());
    }
    std::vector<const motor::ComponentType*> added;
    std::vector<const motor::ComponentType*> rejected;
    for (const auto& t : registry.Types()) {
        if (existing.contains(t.get())) continue;
        const bool duplicate = std::any_of(registry.Types().begin(), registry.Types().end(), [&](const auto& other) {
            return other.get() != t.get() && other->name == t->name &&
                   (existing.contains(other.get()) || std::find(added.begin(), added.end(), other.get()) != added.end());
        });
        if (duplicate) {
            motor::log::Error("Scripts: já existe um componente chamado '{}'; o script com esse nome foi ignorado", t->name);
            rejected.push_back(t.get());
        } else {
            added.push_back(t.get());
        }
    }
    for (const motor::ComponentType* t : rejected) registry.Unregister(t);

    m_module = module;
    m_loadedCopy = copy;
    m_types = added;
    m_typeNames.clear();
    for (const motor::ComponentType* t : added) m_typeNames.push_back(t->name);
    // A ordem de registro segue a inicialização estática dos .cpp, que não é garantida: lista em ordem alfabética.
    std::sort(m_typeNames.begin(), m_typeNames.end());
    RemoveStaleCopies(loadDir, copy);

    // 5. Religa os componentes guardados (de antes da recarga, ou lidos da cena antes de a DLL existir).
    std::vector<motor::SceneSerializer::FieldWarning> lost;
    scenes([&](motor::Scene& scene) { result.resolved += motor::SceneSerializer::ResolveMissingComponents(scene, assets, &lost); });
    result.warnings = DescribeLostFields(lost);
    for (const std::string& warning : result.warnings) motor::log::Warn("Scripts: {}", warning);

    result.ok = true;
    result.types = m_typeNames;
    std::string names;
    for (const std::string& n : m_typeNames) names += (names.empty() ? "" : ", ") + n;
    motor::log::Info("Scripts carregados: {} tipo(s) [{}]; {} componente(s) religado(s)", m_typeNames.size(), names,
                     result.resolved);
    return result;
}

void ScriptHost::Unload(const ForEachScene& scenes, const motor::AssetLibrary& assets) {
    if (!m_module) return;
    const std::unordered_set<const motor::ComponentType*> old(m_types.begin(), m_types.end());
    scenes([&](motor::Scene& scene) {
        motor::SceneSerializer::DetachComponents(scene, [&](const motor::ComponentType& t) { return old.contains(&t); },
                                                 assets);
    });
    UnloadCurrent();
}

void ScriptHost::UnloadCurrent() {
    if (!m_module) return;
    motor::ComponentRegistry& registry = motor::ComponentRegistry::Instance();
    for (const motor::ComponentType* t : m_types) registry.Unregister(t);
    m_types.clear();
    m_typeNames.clear();
    CloseLibrary(m_module);
    m_module = nullptr;
    std::error_code ec;
    fs::remove(m_loadedCopy, ec);
    m_loadedCopy.clear();
}

} // namespace editor
