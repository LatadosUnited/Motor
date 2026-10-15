#include "EditorContext.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <string>
#include <vector>

namespace editor {

namespace {
// Layout de duas colunas (rótulo | controle) para as propriedades.
bool BeginProperties(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void Property(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void DrawTransform(EditorContext& ctx, motor::Entity& e) {
    motor::Transform& t = e.transform;

    if (ctx.eulerEntity != e.id || ctx.eulerSource != t.rotation) {
        ctx.eulerDegrees = glm::degrees(glm::eulerAngles(t.rotation)) + 0.0f;  // + 0 elimina o "-0.0"
        ctx.eulerEntity = e.id;
        ctx.eulerSource = t.rotation;
    }

    if (!BeginProperties("##transform")) return;

    Property("Posição");
    ImGui::DragFloat3("##position", &t.position.x, 0.05f, 0.0f, 0.0f, "%.2f");

    Property("Rotação");
    if (ImGui::DragFloat3("##rotation", &ctx.eulerDegrees.x, 0.5f, 0.0f, 0.0f, "%.1f°")) {
        t.SetEulerDegrees(ctx.eulerDegrees);
        ctx.eulerSource = t.rotation;
    }

    Property("Escala");
    ImGui::DragFloat3("##scale", &t.scale.x, 0.02f, 0.0f, 0.0f, "%.2f");

    ImGui::EndTable();
}

// Texto curto para uma chave: arquivos mostram só o nome; o caminho completo vai na dica.
std::string DisplayName(const std::string& key) {
    if (!motor::AssetLibrary::IsFileKey(key)) return key;
    return key.substr(key.find_last_of('/') + 1);
}

// Combo de asset: embutidos primeiro, depois os arquivos do projeto desse tipo (lidos ao abrir o combo).
// Aceita arquivos arrastados do painel Projeto. Devolve a chave escolhida.
std::optional<std::string> AssetCombo(EditorContext& ctx, const char* id, motor::AssetType type,
                                      const std::string* current, bool allowNone) {
    static std::vector<std::string> s_projectFiles;  // um combo aberto por vez
    std::optional<std::string> chosen;

    const std::string preview = current ? DisplayName(*current) : "(nenhuma)";
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (ImGui::IsWindowAppearing()) s_projectFiles = ctx.assets.ScanFiles(type);

        if (allowNone && ImGui::Selectable("(nenhuma)", !current)) chosen = std::string();
        auto item = [&](const std::string& key) {
            const bool selected = current && *current == key;
            if (ImGui::Selectable(DisplayName(key).c_str(), selected)) chosen = key;
            if (motor::AssetLibrary::IsFileKey(key)) ImGui::SetItemTooltip("%s", key.c_str());
            if (selected) ImGui::SetItemDefaultFocus();
        };
        auto builtIn = [&](const auto& list) {
            for (const auto& a : list) {
                if (a.guid.empty()) item(a.name);
            }
        };
        if (type == motor::AssetType::Mesh) builtIn(ctx.assets.Meshes());
        if (type == motor::AssetType::Texture) builtIn(ctx.assets.Textures());

        if (!s_projectFiles.empty()) {
            ImGui::SeparatorText("Projeto");
            for (const std::string& key : s_projectFiles) {
                ImGui::PushID(key.c_str());
                item(key);
                ImGui::PopID();
            }
        } else if (ctx.assets.Root().empty()) {
            ImGui::TextDisabled("Abra um projeto para usar arquivos.");
        }
        ImGui::EndCombo();
    }
    if (current && motor::AssetLibrary::IsFileKey(*current)) ImGui::SetItemTooltip("%s", current->c_str());

    for (const auto& file : AcceptPathDrop(type)) {
        chosen = ctx.assets.KeyForFile(file);
        if (chosen->empty()) {
            ctx.pendingError = "O arquivo está fora do projeto.";
            chosen.reset();
        }
        break;
    }
    return chosen;
}

void DrawMeshRenderer(EditorContext& ctx, motor::MeshRenderer& mr) {
    if (!BeginProperties("##meshrenderer")) return;

    std::string error;
    Property("Malha");
    const auto meshRef = ctx.assets.RefOf(mr.mesh.get());
    if (auto key = AssetCombo(ctx, "##mesh", motor::AssetType::Mesh, meshRef ? &meshRef->name : nullptr, false)) {
        if (auto mesh = ctx.assets.GetMesh(*key, &error)) {
            mr.mesh = std::move(mesh);
        } else {
            ctx.pendingError = error;
        }
    }

    motor::Material& mat = mr.material;

    Property("Cor");
    ImGui::ColorEdit4("##color", &mat.color.x, ImGuiColorEditFlags_AlphaBar);

    Property("Textura");
    const auto texRef = ctx.assets.RefOf(mat.albedo.get());
    if (auto key = AssetCombo(ctx, "##texture", motor::AssetType::Texture, texRef ? &texRef->name : nullptr, true)) {
        if (key->empty()) {
            mat.albedo = nullptr;
        } else if (auto texture = ctx.assets.GetTexture(*key, &error)) {
            mat.albedo = std::move(texture);
        } else {
            ctx.pendingError = error;
        }
    }

    Property("Especular");
    ImGui::SliderFloat("##specular", &mat.specular, 0.0f, 2.0f, "%.2f");

    Property("Brilho");
    ImGui::SliderFloat("##shininess", &mat.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);

    ImGui::EndTable();
}
} // namespace

void DrawInspectorPanel(EditorContext& ctx) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    motor::Entity* e = ctx.scene.Find(ctx.selected);
    if (!e) {
        ImGui::TextDisabled("Nenhuma entidade selecionada.");
        ImGui::End();
        return;
    }

    ImGui::Checkbox("##active", &e->active);
    ImGui::SetItemTooltip("Ativa");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", &e->name);
    ImGui::TextDisabled("ID %u", e->id);
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawTransform(ctx, *e);
    }

    if (e->meshRenderer) {
        bool keep = true;
        if (ImGui::CollapsingHeader("Mesh Renderer", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawMeshRenderer(ctx, *e->meshRenderer);
        }
        if (!keep) e->meshRenderer.reset();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float buttonWidth = 180.0f;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f));
    if (ImGui::Button("Adicionar componente", ImVec2(buttonWidth, 0.0f))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (ImGui::MenuItem("Mesh Renderer", nullptr, false, !e->meshRenderer)) {
            motor::MeshRenderer mr;
            mr.mesh = ctx.assets.FindMesh("Cubo");
            e->meshRenderer = mr;
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace editor
