#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace motor {
class AssetLibrary;
class Scene;
struct ComponentType;
} // namespace motor

namespace editor {

// DLL de scripts carregada no editor, com recarga sem fechar (hot reload).
//
// Recarregar: carrega uma cópia da DLL nova (a original fica livre para o linker reescrever), troca os
// componentes dos tipos antigos por MissingComponent com o JSON dos campos, desregistra os tipos, descarrega
// a DLL antiga, registra os tipos da nova e religa os componentes. Quem chama não pode estar no Play (os
// scripts estariam rodando) e passa todas as cenas que podem ter componentes de script (cena aberta e
// fotos do desfazer).
class ScriptHost {
public:
    using ForEachScene = std::function<void(const std::function<void(motor::Scene&)>&)>;

    struct Result {
        bool ok = false;
        std::string error;
        size_t detached = 0;  // componentes guardados antes de descarregar a DLL antiga
        size_t resolved = 0;  // componentes religados depois de registrar a nova
        std::vector<std::string> types;  // tipos registrados pela DLL nova
    };

    ScriptHost() = default;
    // Não descarrega: a cena e o registro podem ainda ter objetos da DLL. Chame Unload antes.
    ~ScriptHost() = default;
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // `loadDir`: onde ficam as cópias carregadas (apaga as que sobraram de outras sessões).
    Result Load(const std::filesystem::path& dll, const std::filesystem::path& loadDir, const ForEachScene& scenes,
                motor::AssetLibrary& assets);
    // Guarda os componentes dos scripts como MissingComponent, desregistra os tipos e descarrega a DLL.
    void Unload(const ForEachScene& scenes, const motor::AssetLibrary& assets);

    bool IsLoaded() const { return m_module != nullptr; }
    const std::vector<std::string>& Types() const { return m_typeNames; }
    const std::filesystem::path& LoadedCopy() const { return m_loadedCopy; }

private:
    void UnloadCurrent();  // desregistra os tipos e descarrega (a cena já não pode ter objetos deles)

    void* m_module = nullptr;  // HMODULE
    std::filesystem::path m_loadedCopy;
    std::vector<const motor::ComponentType*> m_types;
    std::vector<std::string> m_typeNames;
    unsigned m_counter = 0;
};

} // namespace editor
