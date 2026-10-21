#include "motor/core/FrameStats.h"

#include "motor/common/FileWrite.h"

#include <algorithm>
#include <format>

namespace motor {

namespace {
// Poucas entradas (umas 20): procura linear sem criar string a cada medição.
size_t Slot(std::vector<FrameStats::Entry>& entries, std::string_view name) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name == name) return i;
    }
    entries.push_back({std::string(name), 0.0});
    return entries.size() - 1;
}
} // namespace

FrameStats& FrameStats::Instance() {
    static FrameStats stats;
    return stats;
}

void FrameStats::AddCpu(std::string_view name, double ms) {
    m_current.cpu[Slot(m_current.cpu, name)].value += ms;
}

void FrameStats::AddGpu(std::string_view name, double ms) {
    m_current.gpu[Slot(m_current.gpu, name)].value += ms;
}

void FrameStats::SetValue(std::string_view name, double value) {
    m_current.values[Slot(m_current.values, name)].value = value;
}

void FrameStats::EndFrame(double frameMs) {
    const auto now = std::chrono::steady_clock::now();
    if (m_current.index == 0) m_start = now;
    ++m_current.index;
    m_current.time = std::chrono::duration<double>(now - m_start).count();
    m_current.frameMs = frameMs;
    m_last = m_current;
    for (Entry& e : m_current.cpu) e.value = 0.0;
    for (Entry& e : m_current.gpu) e.value = 0.0;
    for (Entry& e : m_current.values) e.value = 0.0;
}

// --- PerfRecorder -------------------------------------------------------------------

size_t PerfRecorder::Column(const std::string& name) {
    for (size_t i = 0; i < m_columns.size(); ++i) {
        if (m_columns[i] == name) return i;
    }
    m_columns.push_back(name);
    return m_columns.size() - 1;
}

void PerfRecorder::Record(const FrameStats::Frame& frame) {
    Row row{frame.index, frame.time, frame.frameMs, {}};
    row.cells.reserve(frame.cpu.size() + frame.gpu.size() + frame.values.size());
    for (const auto& e : frame.cpu) row.cells.emplace_back(Column("cpu " + e.name), e.value);
    for (const auto& e : frame.gpu) row.cells.emplace_back(Column("gpu " + e.name), e.value);
    for (const auto& e : frame.values) row.cells.emplace_back(Column(e.name), e.value);
    m_rows.push_back(std::move(row));
}

std::string PerfRecorder::ToCsv() const {
    // Nomes com vírgula ou aspas vão entre aspas (nomes de sistema vêm do código, mas não custa).
    auto quote = [](const std::string& s) {
        if (s.find_first_of(",\"\n") == std::string::npos) return s;
        std::string out = "\"";
        for (char c : s) out += (c == '"') ? std::string("\"\"") : std::string(1, c);
        return out + "\"";
    };
    std::string csv = "quadro,tempo_s,quadro_ms";
    for (const std::string& c : m_columns) csv += "," + quote(c);
    csv += "\n";
    std::vector<const double*> cells(m_columns.size());
    for (const Row& row : m_rows) {
        std::fill(cells.begin(), cells.end(), nullptr);
        for (const auto& [column, value] : row.cells) cells[column] = &value;
        csv += std::format("{},{:.4f},{:.3f}", row.index, row.time, row.frameMs);
        for (const double* v : cells) csv += v ? std::format(",{:.3f}", *v) : std::string(",");
        csv += "\n";
    }
    return csv;
}

bool PerfRecorder::Save(const std::filesystem::path& file, std::string* error) const {
    return WriteFileAtomic(file, ToCsv(), error);
}

double PerfRecorder::MedianFrameMs(double skipSeconds) const {
    std::vector<double> ms;
    for (const Row& row : m_rows) {
        if (row.time >= skipSeconds) ms.push_back(row.frameMs);
    }
    if (ms.empty()) return 0.0;
    const auto mid = ms.begin() + static_cast<std::ptrdiff_t>(ms.size() / 2);
    std::nth_element(ms.begin(), mid, ms.end());
    return *mid;
}

} // namespace motor
