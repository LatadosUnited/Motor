#pragma once

#include <filesystem>

namespace motor {

// Pasta exportada com os assets da engine (shaders e fontes), ao lado do executável do jogo exportado.
inline constexpr const char* kEngineAssetsFolder = "engine";

// Pasta com os assets da engine (shaders/, fonts/). Se existir <pasta do executável>/engine/shaders (jogo
// exportado, Fase 10), usa essa; senão, a pasta do código-fonte gravada na compilação (editor e testes).
std::filesystem::path EngineAssetsDir();

// Pasta do código-fonte (engine/assets), mesmo num jogo exportado. Quem exporta copia daqui.
std::filesystem::path SourceEngineAssetsDir();

// Pasta do executável do processo (o .exe, não a DLL).
std::filesystem::path ExecutableDir();

} // namespace motor
