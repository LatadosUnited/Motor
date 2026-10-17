#pragma once

// Ações do instalador do Hub (Fase 15): instalar e desinstalar, as mesmas para a janela e para o modo silencioso.
// O plano (o que criar e o que apagar) vem de distrib/InstallPlan; aqui ele é executado no Windows: hub embutido
// como recurso, atalhos, chave de desinstalação no HKCU, hub.json, a versão do Motor (de um servidor ou de uma
// pasta) e a Lixeira.

#include <Download.h>
#include <InstallPlan.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace setup {

namespace fs = std::filesystem;

enum class MotorSource { None, Server, Folder };

struct Options {
    distrib::InstallOptions install;  // pasta, dados, atalhos, registro
    fs::path shortcutsDir;            // testes: os atalhos vão para esta pasta (no lugar do menu Iniciar/área de trabalho)
    MotorSource motor = MotorSource::None;
    std::string server;               // MotorSource::Server (e entra no hub.json)
    fs::path localVersion;            // MotorSource::Folder: pasta montada (motor_package)
};

using LogFn = std::function<void(const std::string&)>;
using ProgressFn = std::function<void(const distrib::ReleaseProgress&)>;

fs::path DefaultInstallDir();  // %LOCALAPPDATA%\Programs\Motor Hub
fs::path DefaultDataDir();     // %LOCALAPPDATA%\Motor (a do hub)
fs::path VersionsDir(const fs::path& dataDir);  // <dados>\Versões (dados vazio = o padrão)

// O MotorHub.exe embutido neste instalador (recurso RCDATA "MOTORHUB").
std::optional<std::string> EmbeddedHub(std::string* error = nullptr);

// Instala (ou atualiza) o hub. Falha na primeira etapa que der errado; a versão do Motor é a última etapa: se o
// download falhar, o hub fica instalado e o erro diz o que faltou (dá para baixar depois pelo hub).
bool Install(const Options& options, const LogFn& log, const ProgressFn& progress, const std::atomic<bool>* cancel,
             std::string& error);

// Desinstala o que o instalacao.json da pasta diz: atalhos, chave do registro e a pasta para a Lixeira; com
// `removeData`, também Versões\ e hub.json da pasta de dados.
bool Uninstall(const fs::path& installDir, bool removeData, const LogFn& log, std::string& error);

// Instalação existente nesta pasta (para a tela dizer "atualizar").
std::optional<distrib::InstallRecord> ExistingInstall(const fs::path& installDir);

} // namespace setup
