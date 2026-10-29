#pragma once

// Conversão da UI por entidades (UiRect, UiText, UiImage, UiButton) num documento de UI (.mui): o "Converter UI em
// documento" da Hierarquia e a ferramenta MCP ui_convert_entities. Só CPU, sem OpenGL (testado em test_ui_controles).
//
// Cada entidade de UI vira um elemento Absolute com a mesma âncora, posição e tamanho (as mesmas caixas na tela), na
// ordem de desenho (UiRect::order, empate na ordem da cena), com o nome da entidade (WasClicked("Jogar") continua
// valendo):
//   UiButton -> Button (cores dos estados, interactable, cor e textura da UiImage, texto e fonte do UiText)
//   UiImage  -> Image (com um Text filho esticado se tiver UiText)
//   UiText   -> Text (sem quebra, centrado na vertical como o UiText)
//   só UiRect -> Panel transparente
// Entidade inativa ou UiRect::visible falso vira elemento escondido.

#include "motor/ui/UiDocument.h"

#include <string>
#include <vector>

namespace motor {

class AssetLibrary;
class Scene;

struct UiConversion {
    UiDocumentData document;          // com as declarações nos elementos (styled), pronto para SaveUiDocument
    std::vector<EntityId> converted;  // as entidades de UI que viraram elementos (a sair da cena)
    bool rootIsUi = false;            // `root` é ela mesma de UI (vira um elemento); senão ela recebe o UiDocument
};

// `root`: uma entidade de UI (vira o elemento de cima do documento) ou uma entidade comum com filhos de UI (os filhos
// viram os elementos de cima). Recusa (false com o erro) se não houver UI, se uma entidade de UI tiver outros
// componentes (scripts...), filhos que não são de UI ou um UiDocument (não dá para converter sem perder algo).
bool ConvertUiEntities(const Scene& scene, EntityId root, const AssetLibrary& assets, UiConversion& out,
                       std::string* error = nullptr);

} // namespace motor
