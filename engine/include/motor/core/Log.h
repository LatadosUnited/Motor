#pragma once

#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace motor::log {

enum class Level { Trace, Info, Warn, Error };

void Write(Level level, std::string_view message);
void SetMinLevel(Level level);

// Ouvinte que recebe cada mensagem escrita (ex.: o servidor MCP do editor guarda as últimas). Chamado de
// qualquer thread, com o log travado: não escreva no log de dentro dele. Devolve um id para RemoveSink.
using Sink = std::function<void(Level level, std::string_view message)>;
int AddSink(Sink sink);
void RemoveSink(int id);

template <class... Args>
void Trace(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

[[noreturn]] void AssertFailed(const char* expr, std::string_view message, const char* file, int line);

} // namespace motor::log

#define MOTOR_ASSERT(cond, msg)                                              \
    do {                                                                     \
        if (!(cond)) ::motor::log::AssertFailed(#cond, msg, __FILE__, __LINE__); \
    } while (0)
