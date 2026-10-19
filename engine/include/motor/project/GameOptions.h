#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Sombras do jogo: Desligadas tira todas; as outras limitam a qualidade de cada luz (uma luz Alta com a opção
// Média fica Média, uma Baixa continua Baixa).
enum class ShadowSetting { Off = 0, Low = 1, Medium = 2, High = 3 };

const char* ShadowSettingName(ShadowSetting shadows);   // "desligadas", "baixas"... (gravado no arquivo)
const char* ShadowSettingLabel(ShadowSetting shadows);  // "Desligadas", "Baixas"... (menus)

struct Resolution {
    int width = 0;
    int height = 0;
    bool operator==(const Resolution&) const = default;
};

// Opções do jogador (Fase 21): tela, vsync e qualidade. O projeto tem as padrão (Configurações do jogo, no
// .mproject); o jogo exportado grava as escolhidas pelo jogador em %APPDATA%\<Jogo>\opcoes.json. Os scripts
// leem e mudam pelo Script::GetOptions/ApplyOptions (menu de opções).
struct GameOptions {
    static constexpr int kMsaaChoices[] = {1, 2, 4, 8};  // 1 = sem MSAA

    bool fullscreen = false;
    // Tamanho: da janela, ou a resolução da tela cheia. 0 = o padrão (1280x720 em janela, a do monitor em tela cheia).
    int width = 0;
    int height = 0;
    bool vsync = true;
    int msaa = 4;
    ShadowSetting shadows = ShadowSetting::High;

    // Valores fora do lugar voltam para perto do válido: MSAA para a escolha mais próxima, tamanho negativo para 0.
    void Clamp();
    bool operator==(const GameOptions&) const = default;
};

// JSON (o mesmo formato no .mproject e no opcoes.json). Campos que faltam ficam com o valor de `defaults`.
std::string GameOptionsToJson(const GameOptions& options);
bool GameOptionsFromJson(std::string_view text, GameOptions& options, const GameOptions& defaults = {},
                         std::string* error = nullptr);

// Arquivo opcoes.json: Load devolve false (e deixa `options` = `defaults`) se ele não existe ou é inválido.
bool LoadGameOptions(const std::filesystem::path& file, GameOptions& options, const GameOptions& defaults = {},
                     std::string* error = nullptr);
bool SaveGameOptions(const std::filesystem::path& file, const GameOptions& options, std::string* error = nullptr);

// Pasta de dados do jogador de um jogo: %APPDATA%\<Jogo>\ (Windows) ou ~/.local/share/<Jogo>/. Opções e saves
// ficam nela. Não cria a pasta. Vazio se não há pasta de usuário. O nome é limpo de caracteres inválidos.
std::filesystem::path PlayerDataDir(std::string_view game);

} // namespace motor
