#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace motor {

// Camadas de colisão do projeto (Fase 18): até 16, com nome, e a tabela de quais camadas se chocam. Cada
// Collider e CharacterController escolhe a sua camada (CollisionLayer); a física consulta esta tabela. Fica no
// .mproject ("physics") e vale para todas as cenas do projeto.
struct CollisionLayers {
    static constexpr int kCount = 16;
    static constexpr uint32_t kAll = 0xFFFF;  // máscara com todas as camadas (raios e consultas)

    std::array<std::string, kCount> names;  // vazio = camada sem nome (aparece como "Camada N")
    std::array<uint16_t, kCount> masks;     // bit j de masks[i]: a camada i se choca com a j (simétrico)

    CollisionLayers();  // só "Padrão" (0) com nome; todas se chocam com todas

    bool Collide(int a, int b) const;
    void SetCollide(int a, int b, bool collide);  // mantém a tabela simétrica
    std::string Label(int index) const;           // nome, ou "Camada N" sem nome
    int Find(std::string_view name) const;        // índice pelo nome; -1 se não houver
    // Máscara com as camadas citadas pelo nome ("Jogador", "Inimigo"); nomes desconhecidos são ignorados.
    uint32_t Mask(std::initializer_list<std::string_view> layerNames) const;

    bool operator==(const CollisionLayers&) const = default;
};

// Camada de um Collider ou CharacterController (índice em CollisionLayers; 0 = Padrão).
struct CollisionLayer {
    uint8_t index = 0;

    bool operator==(const CollisionLayer&) const = default;
};

} // namespace motor
