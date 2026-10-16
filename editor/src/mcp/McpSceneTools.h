#pragma once

#include "../EditorContext.h"
#include "McpProtocol.h"

namespace editor::mcp {

// Ferramentas que leem e mudam a cena aberta (entidades, transform, componentes e campos). Usam só o
// EditorContext, então funcionam sem janela (testes). Cada mudança vira um passo de desfazer, como na
// interface: o editor compara a cena no fim do quadro (UndoHistory::Track).
void AddSceneTools(Protocol& protocol, EditorContext& ctx);

// Entidade pelo argumento "entity" (id numérico ou nome). Lança ToolError se não existir.
motor::Entity& ResolveEntity(EditorContext& ctx, const Json& args, const char* key = "entity");
// Entidade como JSON: id, nome, pai, filhos, transform e componentes com os campos (formato da cena).
Json EntityToJson(const EditorContext& ctx, const motor::Entity& entity);
// Erro deixado pelo EditorContext (ele o mostraria num popup): passa para a ferramenta e limpa.
void ThrowPendingError(EditorContext& ctx);

} // namespace editor::mcp
