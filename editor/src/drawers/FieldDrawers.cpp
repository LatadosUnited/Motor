#include "drawers/FieldDrawers.h"

#include <imgui.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

namespace {
// Estática local: os registradores rodam na inicialização estática, em qualquer ordem entre os .cpp.
std::unordered_map<std::string, FieldDrawerFn>& Drawers() {
    static std::unordered_map<std::string, FieldDrawerFn> drawers;
    return drawers;
}

// Texto curto para uma chave: arquivos mostram só o nome; o caminho completo vai na dica.
std::string DisplayName(const std::string& key) {
    if (!motor::AssetLibrary::IsFileKey(key)) return key;
    return key.substr(key.find_last_of('/') + 1);
}
} // namespace

FieldDrawerRegistrar::FieldDrawerRegistrar(const char* typeName, FieldDrawerFn fn) {
    Drawers()[typeName] = fn;
}

FieldDrawerFn FindFieldDrawer(std::string_view typeName) {
    const auto it = Drawers().find(std::string(typeName));
    return it == Drawers().end() ? nullptr : it->second;
}

std::optional<std::string> AssetCombo(EditorContext& ctx, const char* id, motor::AssetType type,
                                      const std::string* current, const char* noneLabel) {
    static std::vector<std::string> s_projectFiles;  // um combo aberto por vez
    std::optional<std::string> chosen;

    const std::string preview = current ? DisplayName(*current) : (noneLabel ? noneLabel : "");
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (ImGui::IsWindowAppearing()) s_projectFiles = ctx.assets.ScanFiles(type);

        if (noneLabel && ImGui::Selectable(noneLabel, !current)) chosen = std::string();
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
        } else if (type == motor::AssetType::Material) {
            ImGui::TextDisabled("Nenhum .mmat no projeto (crie em Projeto > + Criar).");
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

} // namespace editor
