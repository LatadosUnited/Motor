#include "motor/core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace motor::log {

namespace {
std::mutex g_mutex;
Level g_minLevel = Level::Trace;
std::vector<std::pair<int, Sink>> g_sinks;  // protegidos por g_mutex
int g_nextSinkId = 1;

#ifdef _WIN32
// Os textos da engine são UTF-8; sem isso o console do Windows mostra acentos quebrados.
const bool g_utf8Console = [] { return SetConsoleOutputCP(CP_UTF8) != 0; }();
#endif

const char* LevelTag(Level level) {
    switch (level) {
        case Level::Trace: return "trace";
        case Level::Info:  return "info ";
        case Level::Warn:  return "warn ";
        case Level::Error: return "error";
    }
    return "?????";
}
} // namespace

void SetMinLevel(Level level) {
    g_minLevel = level;
}

void Write(Level level, std::string_view message) {
    if (level < g_minLevel) return;

    const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const std::string line = std::format("[{:%H:%M:%S}] [{}] {}\n", now, LevelTag(level), message);

    std::lock_guard lock(g_mutex);
    std::FILE* out = level >= Level::Warn ? stderr : stdout;
    std::fputs(line.c_str(), out);
    std::fflush(out);
    for (const auto& [id, sink] : g_sinks) sink(level, message);
}

int AddSink(Sink sink) {
    std::lock_guard lock(g_mutex);
    const int id = g_nextSinkId++;
    g_sinks.emplace_back(id, std::move(sink));
    return id;
}

void RemoveSink(int id) {
    std::lock_guard lock(g_mutex);
    std::erase_if(g_sinks, [id](const auto& entry) { return entry.first == id; });
}

void AssertFailed(const char* expr, std::string_view message, const char* file, int line) {
    Write(Level::Error, std::format("Assertion failed: {} ({})\n    at {}:{}", expr, message, file, line));
    std::abort();
}

} // namespace motor::log
