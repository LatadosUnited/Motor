#pragma once

#include <filesystem>

namespace editor {

// Avisa quando algo mudou numa pasta ou nas subpastas (arquivo criado, apagado, renomeado ou gravado), para o editor
// só varrer os arquivos do projeto quando precisa: com a cena de referência, a varredura de 2 em 2 s (.meta e
// modelos) custava ~150 ms num quadro (Fase 27). No Windows, pelo FindFirstChangeNotificationW; sem ele (ou se não
// der para vigiar a pasta), Changed() é sempre true e quem chama varre como antes.
class FolderWatcher {
public:
    FolderWatcher() = default;
    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;
    ~FolderWatcher();

    void Watch(const std::filesystem::path& folder);  // vazio = para de vigiar
    const std::filesystem::path& Folder() const { return m_folder; }

    // Mudou algo desde a última chamada? A primeira depois de Watch é sempre true.
    bool Changed();

private:
    void Close();

    std::filesystem::path m_folder;
    void* m_handle = nullptr;  // HANDLE da notificação; nulo = sem vigia
    bool m_first = true;
};

} // namespace editor
