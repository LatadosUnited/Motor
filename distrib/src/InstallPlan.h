#pragma once

// Instalador do Hub (Fase 15), sem janela nem efeitos: o que instalar (arquivos, atalhos, chave de
// desinstalação no HKCU), o registro da instalação (instalacao.json, ao lado do MotorHub.exe) e o que a
// desinstalação manda para a Lixeira. O MotorSetup.exe executa estes planos; os testes só os conferem.

#include "Manifest.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace distrib {

inline constexpr const char* kInstallRecordFile = "instalacao.json";
inline constexpr const char* kUninstallKey = "MotorHub";  // HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\<>
inline constexpr const char* kShortcutName = "Motor Hub.lnk";
inline constexpr const char* kMsys2Url = "https://www.msys2.org/";

// instalacao.json: o que esta instalação criou, para a desinstalação desfazer exatamente isso.
struct InstallRecord {
    static constexpr const char* kFormat = "motor-hub-install";
    static constexpr int kVersion = 1;

    std::string hubVersion;
    std::string installed;              // data ISO
    fs::path installDir;
    fs::path dataDir;                   // vazio = a padrão do hub (%LOCALAPPDATA%\Motor)
    std::vector<fs::path> shortcuts;    // .lnk criados
    std::string registryKey;            // vazio = não registrou a desinstalação
    std::vector<std::string> servers;   // servidores configurados na instalação

    static std::optional<InstallRecord> Load(const fs::path& file, std::string* error = nullptr);
    bool Save(const fs::path& file, std::string* error = nullptr) const;  // .tmp e renomear
};

struct InstallOptions {
    fs::path installDir;
    fs::path dataDir;                  // vazio = padrão do hub
    bool startMenuShortcut = true;
    bool desktopShortcut = false;
    bool registerUninstall = true;
    std::string registryKey = kUninstallKey;
    std::string hubVersion;
    uint64_t installedBytes = 0;       // EstimatedSize (KB) no Painel de Controle
    std::string server;                // para o hub.json (vazio = nenhum a mais)
};

struct Shortcut {
    fs::path link;         // ...\Motor Hub.lnk
    fs::path target;       // ...\MotorHub.exe
    std::string arguments; // UTF-8 (ex.: --data "<pasta>" quando a pasta de dados não é a padrão)
    fs::path workingDir;
    std::string description;
};

struct RegistryValue {
    std::string name;
    std::string text;      // REG_SZ, se !isNumber
    uint32_t number = 0;   // REG_DWORD
    bool isNumber = false;
};

struct InstallPlan {
    fs::path hubExe;      // <installDir>\MotorHub.exe
    fs::path setupExe;    // <installDir>\MotorSetup.exe (cópia do instalador, para desinstalar)
    fs::path recordFile;  // <installDir>\instalacao.json
    std::vector<Shortcut> shortcuts;
    std::string registryKey;  // vazio = não registra
    std::vector<RegistryValue> registryValues;
    InstallRecord record;
};

// `startMenuDir` e `desktopDir`: pastas dos atalhos (Programs do menu Iniciar e Área de Trabalho).
InstallPlan PlanInstall(const InstallOptions& options, const fs::path& startMenuDir, const fs::path& desktopDir);

struct UninstallPlan {
    std::vector<fs::path> shortcuts;  // apagados (são só atalhos criados pelo instalador)
    std::string registryKey;
    std::vector<fs::path> toTrash;    // para a Lixeira: a pasta do hub e, se pedido, Versões\ e hub.json
    fs::path emptyDirToRemove;        // pasta de dados, se ficar vazia
};

// Só põe na lista o que é desta instalação: a pasta só vai para a Lixeira se tiver o instalacao.json ou o
// MotorHub.exe (um instalacao.json estragado nunca leva C:\ junto). `defaultDataDir` vale quando o registro não
// diz a pasta de dados.
UninstallPlan PlanUninstall(const InstallRecord& record, bool removeData, const fs::path& defaultDataDir);

// Ferramentas para compilar os scripts: o g++ e o cmake que a versão instalada usa (sdk/MotorSDK.json) ou, sem
// versão, os do MSYS2 UCRT64 no lugar padrão.
struct BuildTool {
    std::string name;  // "g++ (MSYS2 UCRT64)", "CMake"
    fs::path path;
    bool found = false;
};
std::vector<BuildTool> CheckBuildTools(const fs::path& sdkJson, const fs::path& msysRoot = "C:/msys64");

// Linha de argumentos do hub instalado (vazia com a pasta de dados padrão).
std::string HubArguments(const fs::path& dataDir);

} // namespace distrib
