#pragma once

// Exportar o jogo (Fase 10): monta uma pasta que roda o jogo sem o editor.
//
//   <saída>/
//     <Nome do jogo>.exe     o Player (player/), com o nome, o ícone e a versão das Configurações do jogo
//     libMotor.dll ...       a engine e as DLLs de runtime do MinGW
//     engine/                shaders e fontes da engine (motor/core/EngineAssets.h)
//     Dados/                 o projeto, só com o que o jogo usa:
//       <Nome>.mproject        com a cena inicial escolhida
//       GameScripts.dll        os scripts compilados (se o projeto tiver)
//       Assets/...             os assets referenciados, com os .meta (os GUIDs são os mesmos do projeto)
//     motor-export.json      marca de pasta exportada (exportar de novo só limpa o que estiver listado nela)
//
// O que entra de Assets/ (CollectAssets):
//   - todas as cenas: a troca de cena dos scripts (Script::LoadScene) é por nome, e não dá para saber de
//     antemão quais nomes o código monta;
//   - tudo dentro de Assets/Recursos/ (arquivos que o jogo abre por conta própria);
//   - caminhos "Assets/..." escritos entre aspas nos fontes da pasta Scripts/;
//   - e, seguindo as referências a partir desses, cada asset citado por GUID (ou pelo caminho de reserva
//     "Assets/...") em cenas, prefabs e materiais, e os arquivos que um glTF cita (.bin, imagens).
// Sem GPU: lê só os JSON e os cabeçalhos, não carrega nada.
//
// Gancho para a Fase 8 (áudio): um som (.wav/.ogg/.mp3) referenciado por GUID num componente já entra sozinho,
// desde que AssetTypeFromPath o reconheça (ganha .meta e GUID). Formato novo que aponte para outros arquivos
// (banco de sons, playlist...) ganha um caso em ReferencesOf (GameExport.cpp). Som tocado pelo caminho, de
// dentro de um script, vai em Assets/Recursos/ ou aparece como "Assets/..." no fonte.

#include <motor/assets/AssetDatabase.h>
#include <motor/project/Project.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace editor::GameExport {

inline constexpr const char* kDataFolder = "Dados";
inline constexpr const char* kScriptsDllName = "GameScripts.dll";
inline constexpr const char* kMarkerFile = "motor-export.json";
inline constexpr const char* kResourcesFolder = "Assets/Recursos";  // sempre exportada inteira

// DLLs que o Player e a DLL de scripts usam, procuradas na pasta de runtime (a do editor, build/bin).
// A engine liga o GLFW, o ImGui e o Jolt dentro da libMotor.dll; o resto é o runtime do GCC.
inline constexpr const char* kRuntimeDlls[] = {"libMotor.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll",
                                                "libwinpthread-1.dll"};

struct Collection {
    std::vector<std::string> files;    // relativos ao projeto, com '/', em ordem alfabética (sem os .meta)
    std::vector<std::string> missing;  // referências quebradas: "<arquivo que cita>: <guid ou caminho>"
};

// Assets que o jogo usa (ver o começo do arquivo). `db` precisa estar na pasta Assets do projeto.
Collection CollectAssets(const motor::Project& project, const motor::AssetDatabase& db);

struct Options {
    std::string startupScene;          // relativo ao projeto ("Assets/Cenas/Inicio.mscene"); vazio = a do projeto
    std::filesystem::path outputDir;
    std::filesystem::path playerExe;   // Player.exe compilado
    std::filesystem::path runtimeDir;  // pasta com as kRuntimeDlls
    std::filesystem::path engineAssetsDir;  // engine/assets (shaders/ e fonts/)
    std::filesystem::path scriptsDll;  // vazio = projeto sem scripts
    bool exeResources = true;          // grava o ícone e a versão no .exe (os testes usam um Player falso)
};

struct Result {
    bool ok = false;
    std::string error;
    std::filesystem::path executable;   // <saída>/<Nome>.exe
    std::vector<std::string> assets;    // os arquivos de Assets/ copiados
    std::vector<std::string> warnings;  // referências quebradas (o jogo exporta mesmo assim)
    uint64_t bytes = 0;                 // tamanho total da pasta exportada
};

// Copia tudo para `options.outputDir`. A pasta precisa estar vazia (ou não existir) ou ser uma exportação
// anterior (com kMarkerFile); não pode ficar dentro de Assets/ nem conter o projeto.
Result Export(const motor::Project& project, const motor::AssetDatabase& db, const Options& options);

// Pasta de saída sugerida: <projeto>/Exportado.
std::filesystem::path DefaultOutputDir(const motor::Project& project);

} // namespace editor::GameExport
