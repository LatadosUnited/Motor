#pragma once

// Contadores por quadro (Fase 22): tempo de CPU de cada parte do quadro (loop, sistemas, scripts), tempo de GPU de
// cada passada do renderer e números (draws, triângulos). Sempre ligados, ao contrário do Tracy (motor/core/Profile.h):
// cada medição custa poucas dezenas de nanossegundos. A Application fecha um quadro por volta do loop; quem quer os
// números lê Last() (o Player grava com --perf, o MCP mostra no editor_status).
//
//   MOTOR_PROFILE_STAT("Update");         zona do Tracy e tempo de CPU até o fim do bloco (nome literal)
//   MOTOR_PROFILE_STAT_NAMED(nome);       o mesmo com nome em tempo de execução (std::string_view que dura o bloco)

#include "motor/core/Profile.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class FrameStats {
public:
    struct Entry {
        std::string name;
        double value = 0.0;
    };
    struct Frame {
        uint64_t index = 0;    // 1 no primeiro quadro fechado; 0 = nenhum ainda
        double time = 0.0;     // segundos desde o primeiro quadro
        double frameMs = 0.0;  // duração do quadro inteiro (inclui a espera do vsync e do limite de FPS)
        std::vector<Entry> cpu;     // ms, somados no quadro (um sistema chamado em 3 passos fixos soma os 3)
        std::vector<Entry> gpu;     // ms das passadas lidas neste quadro (a GPU responde ~4 quadros depois; 0 = sem
                                    // leitura, como com a janela minimizada)
        std::vector<Entry> values;  // números do quadro (draws, triângulos...)
    };

    static FrameStats& Instance();

    void AddCpu(std::string_view name, double ms);
    void AddGpu(std::string_view name, double ms);  // várias leituras da mesma passada no mesmo quadro somam
    void SetValue(std::string_view name, double value);

    // Fecha o quadro atual: ele vira Last() e os valores voltam a zero (os nomes ficam, para as colunas do CSV não
    // mudarem).
    void EndFrame(double frameMs);
    const Frame& Last() const { return m_last; }

private:
    Frame m_current;
    Frame m_last;
    std::chrono::steady_clock::time_point m_start{};
};

// Mede o tempo de CPU até o fim do escopo e soma em FrameStats.
class CpuStatTimer {
public:
    explicit CpuStatTimer(std::string_view name) : m_name(name), m_start(std::chrono::steady_clock::now()) {}
    ~CpuStatTimer() {
        const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - m_start;
        FrameStats::Instance().AddCpu(m_name, ms.count());
    }
    CpuStatTimer(const CpuStatTimer&) = delete;
    CpuStatTimer& operator=(const CpuStatTimer&) = delete;

private:
    std::string_view m_name;
    std::chrono::steady_clock::time_point m_start;
};

// Guarda quadros e grava um CSV (Player --perf). Colunas: quadro, tempo_s, quadro_ms, "cpu <nome>", "gpu <nome>" e os
// números, na ordem em que apareceram; vírgula como separador e ponto decimal (abre no Python, no Excel em inglês e
// no LibreOffice escolhendo a vírgula). Um quadro sem uma coluna que outro tem fica com a célula vazia.
class PerfRecorder {
public:
    void Record(const FrameStats::Frame& frame);
    size_t FrameCount() const { return m_rows.size(); }
    std::string ToCsv() const;
    bool Save(const std::filesystem::path& file, std::string* error = nullptr) const;
    // Mediana de quadro_ms dos quadros com time >= skipSeconds (descarta o aquecimento). 0 se não houver.
    double MedianFrameMs(double skipSeconds = 0.0) const;

private:
    struct Row {
        uint64_t index = 0;
        double time = 0.0;
        double frameMs = 0.0;
        std::vector<std::pair<size_t, double>> cells;  // (coluna, valor)
    };
    size_t Column(const std::string& name);

    std::vector<std::string> m_columns;
    std::vector<Row> m_rows;
};

} // namespace motor

#define MOTOR_STAT_CONCAT_(a, b) a##b
#define MOTOR_STAT_CONCAT(a, b) MOTOR_STAT_CONCAT_(a, b)

#define MOTOR_PROFILE_STAT(name) \
    MOTOR_PROFILE_SCOPE(name);   \
    const ::motor::CpuStatTimer MOTOR_STAT_CONCAT(motorStatTimer_, __LINE__)(name)
#define MOTOR_PROFILE_STAT_NAMED(name) \
    MOTOR_PROFILE_SCOPE_NAMED(name);   \
    const ::motor::CpuStatTimer MOTOR_STAT_CONCAT(motorStatTimer_, __LINE__)(name)
