#include "EditorContext.h"

#include <motor/core/Paths.h>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace editor {

namespace fs = std::filesystem;

void SetPathDragPayload(const std::vector<fs::path>& paths) {
    std::string text;
    for (const fs::path& p : paths) {
        if (!text.empty()) text += '\n';
        text += motor::ToUtf8(p);
    }
    ImGui::SetDragDropPayload(kPathPayload, text.data(), text.size());
}

namespace {
std::vector<fs::path> PayloadPaths(const ImGuiPayload* payload) {
    std::vector<fs::path> paths;
    if (!payload || !payload->IsDataType(kPathPayload)) return paths;
    const std::string_view text(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find('\n', start), text.size());
        if (end > start) paths.push_back(motor::PathFromUtf8(text.substr(start, end - start)));
        start = end + 1;
    }
    return paths;
}

std::vector<fs::path> OfType(const std::vector<fs::path>& paths, motor::AssetType type) {
    std::vector<fs::path> result;
    for (const fs::path& p : paths) {
        if (motor::AssetTypeFromPath(p) == type) result.push_back(p);
    }
    return result;
}
} // namespace

std::vector<fs::path> AcceptPathDrop(motor::AssetType type) {
    // Espia o payload antes: assim o alvo nem acende para arquivos que ele não aceita.
    if (OfType(PayloadPaths(ImGui::GetDragDropPayload()), type).empty()) return {};
    std::vector<fs::path> result;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPathPayload)) {
            result = OfType(PayloadPaths(payload), type);
        }
        ImGui::EndDragDropTarget();
    }
    return result;
}

std::vector<fs::path> AcceptAnyPathDrop() {
    std::vector<fs::path> result;
    if (ImGui::BeginDragDropTarget()) {
        result = PayloadPaths(ImGui::AcceptDragDropPayload(kPathPayload));
        ImGui::EndDragDropTarget();
    }
    return result;
}

motor::Entity& EditorContext::CreatePrimitive(Primitive type, EntityId parent) {
    const char* names[] = {"Entidade", "Cubo", "Esfera", "Plano", "Câmera", "Sol", "Lâmpada", "Holofote"};
    motor::Entity& e = scene.CreateEntity(names[static_cast<int>(type)], parent);

    if (type == Primitive::Camera) {
        e.Add<motor::CameraComponent>();
        if (parent == kNullEntity) {
            e.transform.position = {0.0f, 2.0f, 6.0f};
            e.transform.SetEulerDegrees({-12.0f, 0.0f, 0.0f});
        }
    } else if (type == Primitive::DirectionalLight || type == Primitive::PointLight || type == Primitive::SpotLight) {
        motor::LightComponent& light = e.Add<motor::LightComponent>();
        if (type == Primitive::DirectionalLight) {
            light.type = motor::LightType::Directional;
            e.transform.rotation = motor::RotationLookingAt({-0.4f, -1.0f, -0.3f});
        } else if (type == Primitive::SpotLight) {
            light.type = motor::LightType::Spot;
            e.transform.rotation = motor::RotationLookingAt({0.0f, -1.0f, 0.0f});  // aponta para o chão
        }
        if (parent == kNullEntity) e.transform.position = {0.0f, type == Primitive::DirectionalLight ? 8.0f : 3.0f, 0.0f};
    } else if (type != Primitive::Empty) {
        e.Add<motor::MeshRenderer>().mesh = assets.FindMesh(names[static_cast<int>(type)]);
        if (type != Primitive::Plane && parent == kNullEntity) e.transform.position.y = 0.5f;
    }
    selected = e.id;
    return e;
}

motor::Entity* EditorContext::CreateFromMeshFile(const fs::path& file, EntityId parent) {
    const std::string key = assets.KeyForFile(file);
    if (key.empty()) {
        pendingError = std::format("\"{}\" está fora do projeto. Importe o arquivo para a pasta Assets primeiro.",
                                   motor::ToUtf8(file.filename()));
        return nullptr;
    }
    std::string error;
    auto mesh = assets.GetMesh(key, &error);
    if (!mesh) {
        pendingError = std::move(error);
        return nullptr;
    }

    motor::Entity& e = scene.CreateEntity(motor::ToUtf8(file.stem()), parent);
    e.Add<motor::MeshRenderer>().mesh = std::move(mesh);
    selected = e.id;
    return &e;
}

bool EditorContext::ApplyTextureFile(EntityId id, const fs::path& file) {
    motor::Entity* e = scene.Find(id);
    if (!e) return false;
    const std::string key = assets.KeyForFile(file);
    std::string error = "O arquivo está fora do projeto.";
    auto texture = key.empty() ? nullptr : assets.GetTexture(key, &error);
    if (!texture) {
        pendingError = std::move(error);
        return false;
    }
    motor::MeshRenderer* mr = e->Get<motor::MeshRenderer>();
    if (!mr) {
        mr = &e->Add<motor::MeshRenderer>();
        mr->mesh = assets.FindMesh("Cubo");
    }
    mr->material.albedo = std::move(texture);
    selected = id;
    return true;
}

void EditorContext::DuplicateSelected() {
    if (scene.Find(selected)) selected = scene.Duplicate(selected);
}

void EditorContext::ApplyPendingDelete() {
    if (pendingDelete == kNullEntity) return;
    scene.DestroyEntity(pendingDelete);
    if (!scene.Find(selected)) selected = kNullEntity;
    pendingDelete = kNullEntity;
}

void DrawCreateEntityMenuItems(EditorContext& ctx, EntityId parent) {
    if (ImGui::MenuItem("Entidade vazia")) ctx.CreatePrimitive(Primitive::Empty, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Cubo")) ctx.CreatePrimitive(Primitive::Cube, parent);
    if (ImGui::MenuItem("Esfera")) ctx.CreatePrimitive(Primitive::Sphere, parent);
    if (ImGui::MenuItem("Plano")) ctx.CreatePrimitive(Primitive::Plane, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Câmera")) ctx.CreatePrimitive(Primitive::Camera, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Luz direcional")) ctx.CreatePrimitive(Primitive::DirectionalLight, parent);
    if (ImGui::MenuItem("Luz pontual")) ctx.CreatePrimitive(Primitive::PointLight, parent);
    if (ImGui::MenuItem("Luz spot")) ctx.CreatePrimitive(Primitive::SpotLight, parent);
}

} // namespace editor
