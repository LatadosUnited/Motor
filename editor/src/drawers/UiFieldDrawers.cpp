// Desenhadores do Inspector para os tipos de campo da UI do jogo (motor/ui/UiComponents.h): âncora,
// alinhamento do texto, fonte (asset .ttf/.otf do projeto) e documento de UI (.mui).
#include "drawers/FieldDrawers.h"

#include <motor/ui/UiDocument.h>

#include <imgui.h>

#include <algorithm>
#include <iterator>

namespace {

template <class E, size_t N>
void EnumCombo(E& value, const E (&values)[N], const char* (*label)(E)) {
    if (ImGui::BeginCombo("##v", label(value))) {
        for (E v : values) {
            const bool selected = v == value;
            if (ImGui::Selectable(label(v), selected)) value = v;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

} // namespace

// Âncora: uma grade 3x3 (o ponto da tela onde o elemento se prende) e o botão "Esticar".
MOTOR_FIELD_DRAWER(UiAnchor) {
    using motor::UiAnchor;
    auto& anchor = *static_cast<UiAnchor*>(value);
    constexpr UiAnchor kGrid[] = {UiAnchor::TopLeft,    UiAnchor::Top,    UiAnchor::TopRight,
                                  UiAnchor::Left,       UiAnchor::Center, UiAnchor::Right,
                                  UiAnchor::BottomLeft, UiAnchor::Bottom, UiAnchor::BottomRight};
    const float cell = ImGui::GetFrameHeight() * 0.8f;
    const ImVec4 on = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    for (int i = 0; i < 9; ++i) {
        ImGui::PushID(i);
        const bool selected = anchor == kGrid[i];
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, on);
        if (ImGui::Button("##a", ImVec2(cell, cell))) anchor = kGrid[i];
        if (selected) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", motor::UiAnchorLabel(kGrid[i]));
        ImGui::PopID();
        if (i % 3 != 2) ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const bool stretch = anchor == UiAnchor::Stretch;
    if (stretch) ImGui::PushStyleColor(ImGuiCol_Button, on);
    if (ImGui::Button("Esticar")) anchor = UiAnchor::Stretch;
    if (stretch) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Preenche o pai: Posição = margens esquerda e de cima; Tamanho = direita e de baixo");
    ImGui::TextDisabled("%s", motor::UiAnchorLabel(anchor));
    ImGui::EndGroup();
}

MOTOR_FIELD_DRAWER(UiTextAlign) {
    constexpr motor::UiTextAlign kAligns[] = {motor::UiTextAlign::Left, motor::UiTextAlign::Center,
                                              motor::UiTextAlign::Right};
    EnumCombo(*static_cast<motor::UiTextAlign*>(value), kAligns, motor::UiTextAlignLabel);
}

MOTOR_FIELD_DRAWER(UiFont) {
    auto& font = *static_cast<motor::UiFont*>(value);
    const std::string current = font.path.empty() ? font.guid : font.path;
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::Font, font.IsDefault() ? nullptr : &current,
                                           "(padrão: Roboto)");
    if (!chosen) return;
    if (chosen->empty()) {
        font = {};
        return;
    }
    const std::filesystem::path file = ctx.assets.FileForKey(*chosen);
    font.guid = ctx.assets.Database().GuidFor(file);
    font.path = *chosen;
}

// Documento de UI (motor/ui/UiDocument.h): combo com os .mui do projeto e "Recarregar" (lê de novo o arquivo
// mudado fora do editor).
MOTOR_FIELD_DRAWER(UiDocumentRef) {
    auto& ref = *static_cast<motor::UiDocumentRef*>(value);
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* reloadLabel = "Recarregar";
    const float buttonWidth = ImGui::CalcTextSize(reloadLabel).x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x));
    const std::string current = ref.path.empty() ? ref.guid : ref.path;
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::UiDocument, ref.IsEmpty() ? nullptr : &current,
                                           "(nenhum)");
    if (!ref.IsEmpty() && !ref.data) ImGui::SetItemTooltip("Não abriu: %s (veja o Console)", current.c_str());
    if (chosen) motor::SetUiDocumentFile(ref, *chosen, ctx.assets);
    ImGui::SameLine();
    ImGui::BeginDisabled(ref.IsEmpty());
    if (ImGui::Button(reloadLabel)) motor::ReloadUiDocument(ref, ctx.assets);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Lê de novo o .mui (depois de mudá-lo fora do editor)");
}
