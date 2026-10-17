#pragma once

// O que a distribuição (servidor, instalador e hub) pede ao Windows: pastas do usuário, Lixeira, versão de um
// .exe, atalhos (IShellLink), chave de desinstalação no HKCU, abrir um endereço no navegador e a linha de
// comando em UTF-8. Fora do núcleo testável (Manifest, Download, InstallPlan).

#include "InstallPlan.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace distrib::platform {

namespace fs = std::filesystem;

enum class Folder { LocalAppData, StartMenuPrograms, Desktop, Temp };
fs::path KnownFolder(Folder folder);

fs::path ExecutablePath();  // o .exe deste processo

bool MoveToTrash(const fs::path& path, std::string* error = nullptr);  // nunca apaga de vez

// Versão do recurso VERSIONINFO ("0.2.0"); vazio se o .exe não tiver.
std::string FileVersion(const fs::path& exe);

bool CreateShortcut(const Shortcut& shortcut, std::string* error = nullptr);

// HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\<key>
bool WriteUninstallKey(const std::string& key, const std::vector<RegistryValue>& values, std::string* error = nullptr);
bool DeleteUninstallKey(const std::string& key);
bool UninstallKeyExists(const std::string& key);

void OpenUrl(const std::string& url);  // navegador padrão

std::vector<std::string> CommandLineArgs();  // sem o programa, em UTF-8
void AttachParentConsole();                  // programas de janela: escrevem no console de quem chamou

} // namespace distrib::platform
