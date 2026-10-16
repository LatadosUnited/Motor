#include "EditorContext.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <string>
#include <type_traits>
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

// Combo de malha/textura ligado direto ao campo do componente.
template <class Asset>
void AssetField(EditorContext& ctx, const char* id, motor::AssetType type, std::shared_ptr<Asset>& value) {
    const auto ref = ctx.assets.RefOf(value.get());
    const auto key = AssetCombo(ctx, id, type, ref ? &ref->name : nullptr, true);
    if (!key) return;
    if (key->empty()) {
        value = nullptr;
        return;
    }
    std::string error;
    std::shared_ptr<Asset> loaded;
    if constexpr (std::is_same_v<Asset, motor::Mesh>) {
        loaded = ctx.assets.GetMesh(*key, &error);
    } else {
        loaded = ctx.assets.GetTexture(*key, &error);
    }
    if (loaded) {
        value = std::move(loaded);
    } else {
        ctx.pendingError = error;
    }
}

// Um campo de componente, com o widget certo para o tipo (descrição em motor/scene/Component.h).
void DrawField(EditorContext& ctx, const motor::FieldInfo& f, void* component) {
    using motor::FieldType;
    const motor::FieldOptions& o = f.options;
    Property(f.label.c_str());
    ImGui::PushID(f.key.c_str());
    const bool limited = o.min < o.max;
    const ImGuiSliderFlags sliderFlags = o.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
    const ImGuiSliderFlags dragFlags = limited ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
    const char* floatFormat = o.format ? o.format : "%.2f";

    switch (f.type) {
        case FieldType::Bool: ImGui::Checkbox("##v", &f.As<bool>(component)); break;
        case FieldType::Int: {
            int& v = f.As<int>(component);
            const char* format = o.format ? o.format : "%d";
            if (o.slider && limited) {
                ImGui::SliderInt("##v", &v, static_cast<int>(o.min), static_cast<int>(o.max), format, sliderFlags);
            } else {
                ImGui::DragInt("##v", &v, std::max(o.speed, 0.1f), static_cast<int>(o.min), static_cast<int>(o.max),
                               format, dragFlags);
            }
            break;
        }
        case FieldType::Float: {
            float& v = f.As<float>(component);
            if (o.slider && limited) {
                ImGui::SliderFloat("##v", &v, o.min, o.max, floatFormat, sliderFlags);
            } else {
                ImGui::DragFloat("##v", &v, o.speed, o.min, o.max, floatFormat, dragFlags);
            }
            break;
        }
        case FieldType::Vec2:
            ImGui::DragFloat2("##v", &f.As<glm::vec2>(component).x, o.speed, o.min, o.max, floatFormat, dragFlags);
            break;
        case FieldType::Vec3:
            if (o.color) {
                ImGui::ColorEdit3("##v", &f.As<glm::vec3>(component).x);
            } else {
                ImGui::DragFloat3("##v", &f.As<glm::vec3>(component).x, o.speed, o.min, o.max, floatFormat, dragFlags);
            }
            break;
        case FieldType::Vec4:
            if (o.color) {
                ImGui::ColorEdit4("##v", &f.As<glm::vec4>(component).x, ImGuiColorEditFlags_AlphaBar);
            } else {
                ImGui::DragFloat4("##v", &f.As<glm::vec4>(component).x, o.speed, o.min, o.max, floatFormat, dragFlags);
            }
            break;
        case FieldType::String: ImGui::InputText("##v", &f.As<std::string>(component)); break;
        case FieldType::Mesh:
            AssetField(ctx, "##v", motor::AssetType::Mesh, f.As<std::shared_ptr<motor::Mesh>>(component));
            break;
        case FieldType::Texture:
            AssetField(ctx, "##v", motor::AssetType::Texture, f.As<std::shared_ptr<motor::Texture2D>>(component));
            break;
    }
    if (o.tooltip) ImGui::SetItemTooltip("%s", o.tooltip);
    ImGui::PopID();
}

// Componente de tipo desconhecido (script removido, renomeado, com erro ou ainda não compilado): só mostra
// o que está guardado. Devolve false se o usuário pediu para removê-lo.
bool DrawMissingComponent(const motor::MissingComponent& missing) {
    bool keep = true;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
    const std::string header = missing.type + " (script não carregado)###missing";
    const bool open = ImGui::CollapsingHeader(header.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (open && keep) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("O tipo \"%s\" não está registrado: o script foi removido ou renomeado, não compilou, ou a "
                            "DLL de scripts ainda não carregou. Os dados ficam guardados na cena e voltam a valer "
                            "quando o script existir de novo.", missing.type.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::TreeNode("Dados guardados")) {
            ImGui::TextWrapped("%s", missing.json.c_str());
            ImGui::TreePop();
        }
    }
    return keep;
}

// Cabeçalho + campos de um componente. Devolve false se o usuário pediu para removê-lo.
bool DrawComponent(EditorContext& ctx, motor::ComponentPtr& component) {
    const motor::ComponentType& type = component.Type();
    if (&type == &motor::ComponentRegistry::MissingType()) {
        return DrawMissingComponent(*static_cast<const motor::MissingComponent*>(component.Data()));
    }
    ImGui::PushID(type.name.c_str());
    bool keep = true;
    const bool open = ImGui::CollapsingHeader(type.label.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##component_ctx")) {
        if (ImGui::MenuItem("Redefinir valores")) {
            component = motor::ComponentPtr(type);
            if (type.onAdd) type.onAdd(component.Data(), ctx.assets);
        }
        if (ImGui::MenuItem("Remover componente")) keep = false;
        ImGui::EndPopup();
    }
    if (open && keep && BeginProperties("##fields")) {
        for (const motor::FieldInfo& f : type.fields) DrawField(ctx, f, component.Data());
        ImGui::EndTable();
    }
    ImGui::PopID();
    return keep;
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

    // Removido depois do laço, para não invalidar a lista. Pelo índice: pode haver vários desconhecidos.
    std::optional<size_t> toRemove;
    for (size_t i = 0; i < e->components.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (!DrawComponent(ctx, e->components[i])) toRemove = i;
        ImGui::PopID();
    }
    if (toRemove) e->components.erase(e->components.begin() + static_cast<std::ptrdiff_t>(*toRemove));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float buttonWidth = 180.0f;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f));
    if (ImGui::Button("Adicionar componente", ImVec2(buttonWidth, 0.0f))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        // Todos os tipos registrados, scripts por último; os que a entidade já tem aparecem desativados (um de cada).
        for (const bool scripts : {false, true}) {
            if (scripts) ImGui::SeparatorText("Scripts");
            for (const auto& type : motor::ComponentRegistry::Instance().Types()) {
                if (type->IsScript() != scripts) continue;
                if (ImGui::MenuItem(type->label.c_str(), nullptr, false, !e->Get(*type))) {
                    void* data = e->Add(*type);
                    if (type->onAdd) type->onAdd(data, ctx.assets);
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace editor
