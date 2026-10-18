// Desenhador do Inspector para o vínculo com o prefab (motor::PrefabLink, ver motor/scene/PrefabInstance.h):
// de qual prefab a entidade é, os overrides e os botões aplicar / reverter / desfazer vínculo.
#include "drawers/FieldDrawers.h"

#include <motor/core/Paths.h>

#include <imgui.h>

#include <format>

namespace {
// Texto de um override para o usuário ("Mesh Renderer: Cor").
std::string OverrideLabel(const std::string& key) {
    if (key == "name") return "Nome";
    if (key == "active") return "Ativa";
    if (key == "position") return "Posição";
    if (key == "rotation") return "Rotação";
    if (key == "scale") return "Escala";
    auto typeLabel = [](std::string_view name) {
        const motor::ComponentType* type = motor::ComponentRegistry::Instance().Find(name);
        return type ? type->label : std::string(name);
    };
    if (key.starts_with("-#")) return std::format("Entidade do prefab apagada (#{})", key.substr(2));
    if (key.starts_with("-")) return "Componente removido: " + typeLabel(std::string_view(key).substr(1));
    if (key.starts_with("+")) return "Componente adicionado: " + typeLabel(std::string_view(key).substr(1));
    const size_t dot = key.find('.');
    if (dot == std::string::npos) return key;
    const std::string_view typeName = std::string_view(key).substr(0, dot);
    const std::string_view fieldKey = std::string_view(key).substr(dot + 1);
    if (const motor::ComponentType* type = motor::ComponentRegistry::Instance().Find(typeName)) {
        for (const motor::FieldInfo& f : type->fields) {
            if (f.key == fieldKey) return type->label + ": " + f.label;
        }
    }
    return key;
}
} // namespace

MOTOR_FIELD_DRAWER(PrefabLink) {
    using Action = editor::EditorContext::PrefabAction;
    const auto& link = *static_cast<const motor::PrefabLink*>(value);
    const std::filesystem::path file = ctx.assets.Database().PathFor(link.guid);
    const motor::Prefab* prefab = file.empty() ? nullptr : ctx.prefabs.Get(link.guid, ctx.assets);

    if (!prefab) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "(prefab não encontrado)");
        ImGui::SetItemTooltip("GUID %s. A entidade fica como está até o arquivo voltar.", link.guid.c_str());
        return;
    }
    const bool root = link.source == prefab->rootSource;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s%s", motor::ToUtf8(file.stem()).c_str(),
                       root ? "" : "  (parte)");
    ImGui::SetItemTooltip("%s", ctx.assets.KeyForFile(file).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Abrir")) {
        ctx.inspectedAsset = file;  // o Inspector mostra o prefab quando não há entidade selecionada
        ctx.selected = motor::kNullEntity;
    }
    ImGui::SetItemTooltip("Editar o prefab no Inspector (muda todas as instâncias)");

    if (ImGui::SmallButton("Aplicar")) ctx.RequestPrefabAction(Action::Apply, ctx.selected);
    ImGui::SetItemTooltip("Grava esta instância (com as alterações) no prefab: as outras instâncias seguem");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reverter")) ctx.RequestPrefabAction(Action::Revert, ctx.selected);
    ImGui::SetItemTooltip("Descarta as alterações desta instância e volta a ser igual ao prefab");
    ImGui::SameLine();
    if (ImGui::SmallButton("Desvincular")) ctx.RequestPrefabAction(Action::Unpack, ctx.selected);
    ImGui::SetItemTooltip("As entidades ficam como estão, mas não seguem mais o prefab");

    if (link.overrides.empty()) {
        ImGui::TextDisabled("Sem alterações locais");
    } else if (ImGui::TreeNode("##overrides", "%zu alteração(ões) nesta entidade", link.overrides.size())) {
        for (const std::string& key : link.overrides) ImGui::BulletText("%s", OverrideLabel(key).c_str());
        ImGui::TreePop();
    }
}
