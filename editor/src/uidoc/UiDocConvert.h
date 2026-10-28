#pragma once

// "Converter UI em documento" (Hierarquia, clique direito; MCP ui_convert_entities): a UI por entidades de uma entidade
// (UiRect, UiText, UiImage, UiButton e filhos) vira um .mui em Assets/UI/ (motor/ui/UiConvert.h faz a árvore, com as
// mesmas caixas na tela). Na cena, as entidades de UI saem e uma entidade com o UI: Documento fica no lugar: a própria
// raiz, se ela não for de UI (um "Menu" vazio com o script, por exemplo), ou uma nova com o nome dela. Desfazer: o da
// cena (o arquivo .mui gravado fica).

#include "EditorContext.h"

#include <string>

namespace editor {

struct UiDocConvertResult {
    EntityId entity = kNullEntity;  // a entidade com o UiDocument
    std::string key;                // "Assets/UI/Menu.mui"
    size_t elements = 0;            // entidades de UI convertidas
};

// `file`: caminho do .mui relativo ao projeto (vazio = Assets/UI/<nome da entidade>.mui, com um número se já existir).
bool ConvertUiToDocument(EditorContext& ctx, EntityId root, const std::string& file, UiDocConvertResult& out,
                         std::string* error);
// A entidade é de UI ou tem um filho de UI (mostra o item no menu).
bool HasConvertibleUi(const motor::Scene& scene, EntityId id);

} // namespace editor
