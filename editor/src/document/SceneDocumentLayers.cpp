// Documento do editor: camadas de colisão do projeto (Arquivo > Camadas de colisão). Nomes das 16 camadas e a
// tabela de quem se choca com quem, gravadas no .mproject e copiadas para a física (vale a partir do próximo Play).

#include "document/SceneDocument.h"

#include <motor/physics/PhysicsSystem.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>
#include <string>
#include <vector>

namespace editor {

using namespace motor;

bool SceneDocument::SetCollisionLayers(const CollisionLayers& layers, std::string& error) {
    if (!m_project) {
        error = "nenhum projeto aberto";
        return false;
    }
    const CollisionLayers before = m_project->Layers();
    m_project->SetLayers(layers);
    if (!m_project->Save(&error)) {
        m_project->SetLayers(before);
        return false;
    }
    PhysicsSettings::Global().layers = layers;
    return true;
}

void SceneDocument::DrawCollisionLayersWindow() {
    if (!m_showLayersWindow) return;
    ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Camadas de colisão", &m_showLayersWindow)) {
        ImGui::End();
        return;
    }
    if (!m_project) {
        ImGui::TextDisabled("Abra um projeto para editar as camadas.");
        ImGui::End();
        return;
    }

    CollisionLayers layers = m_project->Layers();
    bool changed = false;

    ImGui::TextWrapped("Cada Collider e Controlador de personagem escolhe a sua camada no Inspector. Vale para "
                       "todas as cenas do projeto, a partir do próximo Play.");
    ImGui::SeparatorText("Nomes");
    if (ImGui::BeginTable("##nomes", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("nome", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < CollisionLayers::kCount; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::PushID(i);
            std::string& name = layers.names[static_cast<size_t>(i)];
            ImGui::InputTextWithHint("##nome", "(sem nome)", &name);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;  // grava ao sair do campo, não a cada tecla
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Tabela só com as camadas que têm nome (e a 0): as outras ainda não são usadas.
    std::vector<int> shown;
    for (int i = 0; i < CollisionLayers::kCount; ++i) {
        if (i == 0 || !layers.names[static_cast<size_t>(i)].empty()) shown.push_back(i);
    }
    ImGui::SeparatorText("Quem se choca com quem");
    const int columns = static_cast<int>(shown.size()) + 1;
    if (ImGui::BeginTable("##matriz", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        for (int j : shown) ImGui::TableSetupColumn(std::to_string(j).c_str());
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        for (int j : shown) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::to_string(j).c_str());
            ImGui::SetItemTooltip("%s", layers.Label(j).c_str());
        }
        for (size_t r = 0; r < shown.size(); ++r) {
            const int a = shown[r];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d: %s", a, layers.Label(a).c_str());
            for (size_t c = 0; c < shown.size(); ++c) {
                ImGui::TableNextColumn();
                if (c < r) continue;  // tabela simétrica: só a metade de cima
                const int b = shown[c];
                bool collide = layers.Collide(a, b);
                ImGui::PushID(a * CollisionLayers::kCount + b);
                if (ImGui::Checkbox("##c", &collide)) {
                    layers.SetCollide(a, b, collide);
                    changed = true;
                }
                ImGui::SetItemTooltip("%s x %s", layers.Label(a).c_str(), layers.Label(b).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Corpos parados (sem Rigidbody) nunca se chocam entre si.");

    if (changed && !(layers == m_project->Layers())) {
        std::string error;
        if (!SetCollisionLayers(layers, error)) ShowError("Não foi possível salvar as camadas: " + error);
    }
    ImGui::End();
}

} // namespace editor
