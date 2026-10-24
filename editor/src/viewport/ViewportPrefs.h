#pragma once

#include "viewport/ColliderOverlay.h"

#include <motor/renderer/Mesh.h>

#include <glm/vec3.hpp>

#include <memory>

namespace editor {

// Preferências do viewport lembradas entre aberturas do editor (editor.json, pelo EditorApp).
struct ViewportPrefs {
    ColliderOverlay colliders = ColliderOverlay::Selected;
    bool snap = false;          // encaixe ligado pelo botão (Ctrl segurado liga só durante o arraste)
    float snapMove = 0.5f;      // metros: passo do mover e tamanho da célula da grade
    float snapRotate = 15.0f;   // graus
    float snapScale = 0.1f;
    bool grid = false;          // grade visível no plano horizontal (liga no botão Grade)
    bool gridFine = false;      // também as linhas finas (célula = passo do mover); senão só as de 10 em 10
    float gridHeight = 0.0f;    // altura (Y) do plano da grade
    bool stats = false;         // painel dos contadores por quadro (botão Estatísticas)
    bool navMesh = true;        // navmesh e caminhos dos agentes (botão Navmesh; só aparece se a cena tiver um)

    // Valores fora da faixa (editor.json editado à mão) voltam para dentro dela.
    void Clamp();
};

// Mover com encaixe: a posição no mundo vai para a grade (múltiplos de `step`, com Y contado a partir de
// `height`), só nos eixos que o gizmo mudou. Assim um objeto fora da grade entra nela no primeiro passo.
glm::vec3 SnapToGrid(const glm::vec3& before, const glm::vec3& after, float step, float height);

// Malha de linhas da grade: 2 * halfCount + 1 linhas em X e em Z, espaçadas de 1, no plano y = 0, centradas
// na origem. Cada linha é um triângulo degenerado (a, b, b), desenhado em wireframe pelo renderer.
std::shared_ptr<motor::Mesh> CreateGridMesh(int halfCount);

// Onde a grade fica neste quadro: centrada embaixo da câmera, presa a múltiplos de `alignTo` para as linhas não
// "andarem" com a câmera.
glm::vec3 GridCenter(const glm::vec3& camera, float alignTo, float height);

} // namespace editor
