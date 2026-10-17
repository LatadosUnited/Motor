#pragma once

// O que o hub pede ao sistema (Windows): pastas padrão, abrir o editor, Explorer, Lixeira e o seletor de pastas.
// Fora do núcleo (HubCore) para os testes não dependerem de nada disso.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hub::platform {

namespace fs = std::filesystem;

struct Paths {
    fs::path dataDir;             // %LOCALAPPDATA%\Motor (ou --data)
    fs::path configFile;          // <dataDir>\hub.json
    fs::path versionsDir;         // <dataDir>\Versões
    fs::path defaultProjectsDir;  // Documentos\Motor
};

// `dataOverride` (opção --data) troca a pasta de dados inteira: testes e agentes não mexem na do usuário.
Paths DefaultPaths(const fs::path& dataOverride);

// Abre `exe` com os argumentos (UTF-8), sem esperar, com a pasta do exe como pasta de trabalho. O editor é um
// programa de console: abre sem janela de console (o log continua disponível pelo MCP, log_read).
bool LaunchProcess(const fs::path& exe, const std::vector<std::string>& args, std::string* error = nullptr);

void RevealInExplorer(const fs::path& path);
bool MoveToTrash(const fs::path& path, std::string* error = nullptr);  // nunca apaga de vez
std::optional<fs::path> PickFolder(const fs::path& initialDir, const wchar_t* title);

std::vector<std::string> CommandLineArgs();  // sem o programa, em UTF-8
void AttachParentConsole();                  // modo de linha de comando: escreve no console de quem chamou

} // namespace hub::platform
