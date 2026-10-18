#pragma once

// Aparência do Hub e peças de interface usadas pelas abas (HubApp*.cpp): cores, estilo, fonte e botões.

#include "HubCore.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace hub::widgets {

inline const ImVec4 kGreen(0.45f, 0.85f, 0.55f, 1.0f);
inline const ImVec4 kOrange(0.95f, 0.65f, 0.25f, 1.0f);
inline const ImVec4 kRed(0.95f, 0.40f, 0.35f, 1.0f);
inline const ImVec4 kDim(0.60f, 0.62f, 0.66f, 1.0f);

void ApplyStyle();
void LoadFont();
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));
// Combo das versões instaladas; `selected` é o texto da versão.
void VersionCombo(const char* label, const std::vector<InstalledVersion>& versions, std::string& selected,
                  std::string_view defaultVersion);
// Barra de situação no rodapé da aba (última ação).
void DrawStatusLine(const std::string& status, bool error);

} // namespace hub::widgets
