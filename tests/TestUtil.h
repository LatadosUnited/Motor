#pragma once

// Mini-framework dos testes: CHECK conta falhas sem abortar; o main devolve 1 se algo falhou (o ctest lê isso).

#include <cstdio>
#include <filesystem>

inline int g_failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (cond) {                                                                      \
            std::printf("  ok   %s\n", #cond);                                           \
        } else {                                                                         \
            std::printf("  FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);              \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

#define SECTION(title) std::printf("%s\n", title)

inline int TestResult() {
    std::printf("\n%s (%d falha(s))\n", g_failures ? "FALHOU" : "TUDO OK", g_failures);
    return g_failures ? 1 : 0;
}

// Pasta de trabalho do teste (o ctest passa uma dentro de build/). Começa vazia.
inline std::filesystem::path TestDir(int argc, char** argv, const char* fallback) {
    std::filesystem::path dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(fallback);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}
