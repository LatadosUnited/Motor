#pragma once

#include <imgui.h>

#include <functional>
#include <optional>

#include <glm/vec3.hpp>

namespace motor {
class SceneRuntime;
} // namespace motor

namespace editor {

struct EditorContext;

// Percepção no viewport (Fase 37), junto do navmesh (botão Navmesh): o cone de visão de cada Percepção no plano (o
// leque até o alcance e o círculo de "percebe em volta"), e o alcance dos sons de cada Alvo de percepção com Fonte de
// áudio (círculo azul-claro). No Play, também: linha até quem o agente vê (vermelha), até cada ruído ouvido
// (azul-claro) e um X onde lembra ter percebido quem não percebe mais (cinza, com quantos segundos faz).
// Em linhas por cima da imagem; `project`: mundo -> pixels do viewport.
void DrawPerception(const EditorContext& ctx, motor::SceneRuntime* runtime, ImDrawList* dl, const ImVec2& origin,
                    const std::function<std::optional<ImVec2>(const glm::vec3&)>& project);

} // namespace editor
