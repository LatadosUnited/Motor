#pragma once

// Propriedades de estilo no JSON (interno da camada ui): a mesma tabela lê e grava as chaves de estilo do .mui
// (UiDocument.cpp) e as regras do .mstyle (UiStyle.cpp).

#include "motor/ui/UiStyle.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace motor {

using UiJson = nlohmann::ordered_json;

bool ReadUiStyleJson(int property, const UiJson& json, UiStyleValue& out);
UiJson UiStyleValueJson(int property, const UiStyleValue& value);

// Lê as chaves de estilo de um objeto (as outras ficam para quem chamou). "size" é atalho de width e height em
// pixels. `invalid` recebe as chaves com valor da forma errada (ficam de fora).
void ReadUiStyleDeclarations(const UiJson& object, UiStyleDeclarations& out, std::vector<std::string>* invalid = nullptr);
void WriteUiStyleDeclarations(const UiStyleDeclarations& declarations, UiJson& object);
bool IsUiStyleKey(std::string_view key);  // propriedade da tabela ou "size"

} // namespace motor
