#pragma once

#include "scripts/ScriptProject.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace editor {

// Roda um processo e repassa a saída (stdout e stderr juntos) em pedaços. Devolve o código de saída, ou -1
// se não conseguiu iniciar (com a mensagem em `error`). `cancel` encerra o processo e os filhos dele.
// `extraPath`: pastas postas na frente do PATH do processo.
int RunProcess(const std::vector<std::string>& args, const std::vector<std::filesystem::path>& extraPath,
               const std::function<void(const std::string&)>& onOutput, const std::atomic<bool>& cancel,
               std::string* error = nullptr);

// Compila os scripts de um projeto em segundo plano (CMake configura se precisar, depois compila).
// O editor chama Poll() a cada frame para saber quando terminou.
class ScriptBuilder {
public:
    enum class State { Idle, Running, Succeeded, Failed };

    struct FileTiming {
        std::string file;     // caminho relativo a Scripts/ (ex.: "Girar.cpp") ou "MotorScriptEntry.cpp"
        float seconds = 0.0f;
    };

    struct Timings {
        float configure = 0.0f;
        float compile = 0.0f;
        float link = 0.0f;
        float total = 0.0f;
        std::vector<FileTiming> files;
    };

    ScriptBuilder() = default;
    ~ScriptBuilder();  // cancela e espera a compilação em andamento
    ScriptBuilder(const ScriptBuilder&) = delete;
    ScriptBuilder& operator=(const ScriptBuilder&) = delete;

    // Não faz nada (e devolve false) se já houver uma compilação rodando.
    bool Start(const ScriptProject::Paths& paths, const ScriptProject::Toolchain& toolchain);
    void Cancel();
    bool IsRunning() const { return m_state == State::Running; }
    State GetState() const { return m_state; }

    // true uma única vez, no frame em que a compilação termina.
    bool Poll();

    std::string Log() const;     // saída completa da última compilação
    float Seconds() const;       // duração da última compilação (ou da atual, até agora)
    Timings GetTimings() const;  // tempos por etapa da última compilação

private:
    void Append(const std::string& text);

    std::thread m_thread;
    std::atomic<State> m_state = State::Idle;
    std::atomic<bool> m_finished = false;
    std::atomic<bool> m_cancel = false;
    mutable std::mutex m_mutex;
    std::string m_log;
    std::chrono::steady_clock::time_point m_start{};
    std::chrono::steady_clock::time_point m_end{};
    Timings m_timings;
};

} // namespace editor
