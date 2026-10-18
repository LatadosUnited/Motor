#pragma once

// Editor de código para os scripts do projeto: arquivos do VS Code (.vscode/) gerados pelo Motor, com o
// IntelliSense apontando para o compile_commands.json da compilação dos scripts, uma tarefa de compilar e
// "anexar ao editor" com o gdb; e abrir um arquivo na linha (erro clicável, duplo clique num .cpp).

#include "scripts/ScriptProject.h"

#include <filesystem>
#include <string>
#include <vector>

namespace editor::ScriptIde {

// Primeira linha dos arquivos que o Motor gera: só esses são reescritos (um arquivo do usuário fica como está).
inline constexpr const char* kGeneratedMarker = "// Gerado pelo Motor Editor";

struct GeneratedFile {
    std::string name;  // relativo a .vscode/ ("tasks.json")
    std::string text;
};
// Conteúdo dos arquivos de .vscode/ para o projeto. `editorExe`: o MotorEditor.exe, para o depurador anexar.
std::vector<GeneratedFile> VsCodeFiles(const ScriptProject::Paths& paths, const ScriptProject::Toolchain& toolchain,
                                       const std::filesystem::path& editorExe);
// Grava em <projeto>/.vscode os arquivos que faltam ou que o Motor gerou antes (com kGeneratedMarker).
// Devolve quantos gravou; -1 em erro (`error`).
int WriteVsCodeFiles(const ScriptProject::Paths& paths, const ScriptProject::Toolchain& toolchain,
                     const std::filesystem::path& editorExe, std::string* error = nullptr);

// Code.exe do VS Code instalado (por usuário, para todos, ou o `code` do PATH), ou vazio.
std::filesystem::path FindVsCode();
// Linha de comando para abrir a pasta do projeto no VS Code, já no arquivo e na linha (line/column 0: só o arquivo;
// `file` vazio: só a pasta).
std::vector<std::wstring> VsCodeCommand(const std::filesystem::path& code, const std::filesystem::path& projectRoot,
                                        const std::filesystem::path& file, int line, int column);
// Abre no VS Code, se instalado; senão, o arquivo no programa padrão do Windows (sem ir para a linha).
void OpenFile(const std::filesystem::path& projectRoot, const std::filesystem::path& file, int line = 0, int column = 0);
// Abre a pasta do projeto no VS Code. false se ele não está instalado.
bool OpenProjectFolder(const std::filesystem::path& projectRoot);

} // namespace editor::ScriptIde
