#pragma once

// Edição de documentos (.mui) e folhas (.mstyle) por JSON, sem a janela do Editor de UI: as ferramentas MCP
// ui_document_edit e ui_style_edit (que mexem direto nos arquivos) e o "set" do ui_editor_element. Usa as mesmas
// operações da janela (UiDocEdit.h: inserir com nome único, mover, duplicar). Sem janela nem OpenGL
// (tests/test_ui_controles.cpp).
//
// Operações de documento (lista "ops", aplicadas em ordem; elemento = nome ou caminho [i, j...]):
//   {"action": "add", "type": "Button", "parent": "menu", "index": 0, ...campos}  (parent ausente = raiz)
//   {"action": "set", "element": "jogar", ...campos}
//   {"action": "move", "element": "jogar", "parent": "menu", "index": 2}
//   {"action": "delete", "element": "jogar"}      {"action": "duplicate", "element": "jogar"}
//   {"action": "add_sheet", "path": "Assets/UI/Tema.mstyle"}   {"action": "remove_sheet", "path": "..."}
// Campos (UiElementFieldsFromJson): name, classes (lista ou texto), text, visible, interactable, image ("Assets/..." ou
// ""), value, min, max, step, checked, placeholder, maxLength, follow, followOffset e style {propriedade: valor JSON
// como no .mui, null tira a declaração}.
//
// Operações de folha: {"action": "add_rule", "selector": "Button:focus", "set": {...}}, {"action": "set_rule", "rule":
// 0 ou "Button:focus", "selector": "...", "set": {...}}, {"action": "delete_rule", "rule": ...}.

#include "uidoc/UiDocEdit.h"

#include <nlohmann/json.hpp>

#include <string>

namespace editor {

using UiJsonArgs = nlohmann::ordered_json;

// Elemento por nome ou caminho. false com o erro.
bool UiPathFromJson(const std::vector<motor::UiElement>& roots, const UiJsonArgs& value, UiPath& out, std::string* error);
// Os campos de `args` que existirem vão para o elemento. `assets`: mudou imagem ou fonte (recarregar os arquivos).
bool UiElementFieldsFromJson(motor::UiElement& element, const UiJsonArgs& args, bool* assets, std::string* error);
// O elemento como as ferramentas mostram: path, tipo, nome, classes, texto, valores dos controles, declarações, filhos.
UiJsonArgs UiElementSummaryJson(const motor::UiElement& element, UiPath& path);

bool ApplyUiDocumentOps(motor::UiDocumentData& document, const UiJsonArgs& ops, std::string* error);
bool ApplyUiStyleOps(motor::UiStyleSheet& sheet, const UiJsonArgs& ops, std::string* error);

} // namespace editor
