#pragma once

// Identidade do .exe exportado (Fase 21): ícone e informações de versão gravados como recursos do Windows no
// Player copiado (BeginUpdateResource/UpdateResource). O grupo do ícone se chama "GLFW_ICON": a GLFW o usa
// sozinha na janela e na barra de tarefas, e o Explorer o mostra no arquivo.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace editor::ExeResources {

// Uma imagem do ícone: um PNG ou um DIB, como estão dentro de um .ico.
struct IconImage {
    int width = 0;   // 1 a 256
    int height = 0;
    uint16_t bitCount = 32;
    std::vector<uint8_t> data;
};

// Lê um .ico (todas as imagens) ou um .png (uma imagem; até 256x256). false com `error` se não serve.
bool ReadIcon(const std::filesystem::path& file, std::vector<IconImage>& images, std::string& error);

struct VersionInfo {
    std::string name;        // nome do jogo (descrição e produto)
    std::string version;     // "1.2.3"; os números também vão para a versão fixa do arquivo
    std::string executable;  // nome do arquivo ("Meu Jogo.exe")
};

// Bloco VS_VERSIONINFO (RT_VERSION) em pt-BR / Unicode.
std::vector<uint8_t> BuildVersionResource(const VersionInfo& info);

// Grava no .exe o ícone (se `icon` não estiver vazio) e as informações de versão.
bool Apply(const std::filesystem::path& exe, const std::vector<IconImage>& icon, const VersionInfo& info,
           std::string& error);

} // namespace editor::ExeResources
