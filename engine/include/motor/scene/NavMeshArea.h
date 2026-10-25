#pragma once

// Área de navegação (Fase 35): uma caixa que marca o navmesh dentro dela com uma área (água, lama, grama alta). Cada
// área tem um custo: andar 1 m nela conta como `cost` metros na busca de caminho, então o agente dá a volta quando a
// volta sai mais curta que atravessar. E cada agente diz por quais áreas anda (NavMeshAgent::areas): fora delas,
// nem passa.
//
// Só dados: o navmesh do NavSystem (motor/ai/NavSystem.h) ganha as áreas quando é gerado. Mudou a caixa ou a área?
// Gere o navmesh de novo. O custo vale na hora (pode até mudar no jogo, por script).

#include "motor/scene/Component.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace motor {

// Áreas 0 a 15: 0 é o chão comum (fora de qualquer Área de navegação); as outras, as das caixas.
inline constexpr int kNavAreaCount = 16;

struct NavMeshArea {
    glm::vec3 size{4.0f, 2.0f, 4.0f};  // caixa no espaço da entidade (com a escala dela), centrada em `center`
    glm::vec3 center{0.0f};
    int area = 1;                      // 1 a 15
    std::string name = "Água";         // nome da área no Inspector (a máscara dos agentes mostra este)
    float cost = 2.0f;                 // a partir de 1: quanto vale andar 1 m na área

    bool operator==(const NavMeshArea&) const = default;
};

// Áreas por onde um agente anda: um bit por área (bit 0, o chão comum). O padrão é todas.
struct NavAreaMask {
    uint16_t bits = 0xffff;

    bool Allows(int area) const { return area >= 0 && area < kNavAreaCount && (bits & (1u << area)) != 0; }
    bool operator==(const NavAreaMask&) const = default;
};

// Na cena é o número dos bits; o Inspector mostra uma caixa de marcar por área, com os nomes das Áreas da cena.
template <> struct FieldTraits<NavAreaMask> {
    static CustomFieldType Describe();
};

} // namespace motor
