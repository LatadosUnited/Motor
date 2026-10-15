#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace editor {

// Diálogos nativos. Retornam nullopt se o usuário cancelar. Bloqueiam até o usuário responder.
namespace FileDialog {

enum class Kind { Scene, Project };

std::optional<std::filesystem::path> OpenFile(Kind kind, const std::filesystem::path& initialDir = {});
std::optional<std::filesystem::path> SaveFile(Kind kind, const std::filesystem::path& suggestedPath = {});
std::optional<std::filesystem::path> PickFolder(const std::filesystem::path& initialDir = {});

} // namespace FileDialog

// Integração com o gerenciador de arquivos do sistema.
namespace Shell {

void RevealInExplorer(const std::filesystem::path& path);  // pasta: abre; arquivo: abre a pasta e seleciona
// Envia para a Lixeira (recuperável). Nunca apaga definitivamente.
bool MoveToTrash(const std::filesystem::path& path, std::string* error = nullptr);
void OpenWithDefaultApp(const std::filesystem::path& path);  // como dar duplo clique no Explorer

} // namespace Shell

// Lista de arquivos na área de transferência do sistema (o mesmo formato do Explorer: Ctrl+C lá, Ctrl+V aqui).
namespace Clipboard {

// cut = recortar (o Explorer move em vez de copiar ao colar). Retorna false se não conseguiu.
bool SetFiles(const std::vector<std::filesystem::path>& paths, bool cut);
std::vector<std::filesystem::path> GetFiles();
// Muda toda vez que qualquer programa altera a área de transferência.
unsigned long SequenceNumber();

} // namespace Clipboard

} // namespace editor
