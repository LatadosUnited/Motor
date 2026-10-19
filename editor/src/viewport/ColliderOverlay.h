#pragma once

#include <string>
#include <string_view>

namespace editor {

// Quais contornos de collider (e de personagem) o viewport desenha. Padrão Selecionado, como na Unity: com
// todos ligados a cena enche de linhas (toda forma criada já vem com Collider) e o custo cresce com a cena.
enum class ColliderOverlay { Selected, All, None };

// Nome gravado no editor.json; texto desconhecido volta ao padrão.
std::string_view ColliderOverlayName(ColliderOverlay mode);
ColliderOverlay ParseColliderOverlay(std::string_view name);
// Rótulo do botão da barra do viewport.
const char* ColliderOverlayLabel(ColliderOverlay mode);
// Próximo modo ao clicar no botão: Selecionado → Todos → Nenhum → Selecionado.
ColliderOverlay NextColliderOverlay(ColliderOverlay mode);

// Se o contorno de uma entidade com collider aparece. Inativa só aparece quando selecionada.
bool ShouldDrawCollider(ColliderOverlay mode, bool selected, bool activeInHierarchy);

} // namespace editor
