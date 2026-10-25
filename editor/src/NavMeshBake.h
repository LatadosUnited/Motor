#pragma once

#include <motor/ai/NavSystem.h>

#include <filesystem>
#include <string>

namespace motor {
struct Entity;
struct NavMeshSurface;
} // namespace motor

namespace editor {

struct EditorContext;

// "Gerar navmesh" (Fase 34), usado pelo botão do Inspector e pela ferramenta nav_bake do MCP: gera o navmesh da cena
// aberta com os parâmetros da superfície, grava no .mnav dela (ou num novo em Assets/Navegacao) e liga o arquivo ao
// campo Navmesh. Mudar o campo vira um passo de desfazer, como qualquer edição; o arquivo gravado não volta.
struct NavBakeResult {
    bool ok = false;
    std::string error;  // frase para o usuário, sem ponto final
    std::filesystem::path file;
    motor::NavStats stats;
};
NavBakeResult BakeNavMesh(EditorContext& ctx, motor::NavMeshSurface& surface);

// Primeira entidade ativa com Superfície de navegação (nullptr se a cena não tiver).
motor::Entity* FindNavSurface(EditorContext& ctx);

} // namespace editor
