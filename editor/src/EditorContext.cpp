#include "EditorContext.h"
#include "viewport/UiEditor.h"

#include <motor/core/Paths.h>
#include <motor/scene/ModelImport.h>
#include <motor/scene/Terrain.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

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
    const char* names[] = {"Entidade", "Cubo", "Esfera", "Plano", "Câmera", "Sol", "Lâmpada", "Holofote",
                           "Cilindro", "Cápsula", "Cone", "Terreno"};
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
        if (parent == kNullEntity) e.transform.position = {0.0f, 3.0f, 0.0f};
    } else if (type == Primitive::Terrain) {
        CreateTerrainFile(e);
    } else if (type != Primitive::Empty) {
        e.Add<motor::MeshRenderer>().mesh = assets.FindMesh(names[static_cast<int>(type)]);
        // Em cima do chão: as formas de altura 2 (cilindro e cápsula) sobem 1, as outras 0,5.
        const float lift = type == Primitive::Plane                                      ? 0.0f
                           : type == Primitive::Cylinder || type == Primitive::Capsule ? 1.0f
                                                                                       : 0.5f;
        if (parent == kNullEntity) e.transform.position.y = lift;
        motor::ColliderComponent& collider = e.Add<motor::ColliderComponent>();  // medidas da forma
        switch (type) {
            case Primitive::Sphere: collider.shape = motor::ColliderShape::Sphere; break;
            case Primitive::Plane:  // sem espessura: uma caixa fina logo abaixo da superfície
                collider.size = {1.0f, 0.02f, 1.0f};
                collider.center = {0.0f, -0.01f, 0.0f};
                break;
            case Primitive::Cylinder: collider.shape = motor::ColliderShape::Cylinder; break;
            case Primitive::Capsule: collider.shape = motor::ColliderShape::Capsule; break;
            case Primitive::Cone: collider.shape = motor::ColliderShape::Mesh; break;  // com Rigidbody, casco convexo
            default: break;                                                         // cubo: a caixa 1 x 1 x 1
        }
    }
    selected = e.id;
    return e;
}

void EditorContext::PushTerrainStroke(const motor::TerrainStroke& stroke) {
    auto changes = std::make_shared<const std::vector<motor::TerrainSampleChange>>(stroke.Finish());
    if (changes->empty() || !pushSceneUndo) return;
    std::shared_ptr<motor::TerrainData> data = stroke.Data();
    pushSceneUndo([data, changes] { motor::ApplyTerrainChanges(*data, *changes, false); },
                  [data, changes] { motor::ApplyTerrainChanges(*data, *changes, true); });
}

void EditorContext::CreateTerrainFile(motor::Entity& e) {
    // 4 x 4 blocos planos em volta da origem (128 m de lado com os blocos de 32 m), gravados num .mterrain novo.
    auto data = std::make_shared<motor::TerrainData>();
    for (int z = -2; z < 2; ++z) {
        for (int x = -2; x < 2; ++x) data->AddBlock({x, z});
    }
    motor::Terrain& terrain = e.Add<motor::Terrain>();
    fs::path file = NewAssetPath("Terrenos", "Terreno", motor::kTerrainFileExtension);
    // A pasta dos blocos também não pode existir (sobrou de um terreno apagado à mão, por exemplo).
    std::error_code ec;
    for (int i = 2; !file.empty() && fs::exists(motor::TerrainData::BlocksFolder(file), ec); ++i) {
        file = file.parent_path() / motor::PathFromUtf8(std::format("Terreno {}{}", i, motor::kTerrainFileExtension));
    }
    std::string error;
    if (file.empty()) {
        pendingError = "Abra ou crie um projeto para gravar o terreno: este fica só na memória.";
    } else if (!data->Save(file, &error)) {
        pendingError = std::format("Não foi possível gravar o terreno: {}", error);
    } else {
        terrain.terrain.file = file;
        terrain.terrain.guid = assets.Database().GuidFor(file);
        terrain.terrain.path = assets.KeyForFile(file);
    }
    terrain.terrain.SetData(std::move(data));
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
    if (mr->sharedMaterial) {
        // Material de asset: a textura vai para ele (e para todos que o usam), e o .mmat é gravado.
        mr->sharedMaterial->albedo = std::move(texture);
        if (!assets.SaveMaterial(mr->sharedMaterial.get(), &error)) pendingError = std::move(error);
    } else {
        mr->material.albedo = std::move(texture);
    }
    selected = id;
    return true;
}

fs::path EditorContext::NewAssetPath(const fs::path& folder, const std::string& name, const char* extension) const {
    const fs::path assetsDir = assets.Database().AssetsDir();
    if (assetsDir.empty()) return {};
    const fs::path dir = folder.is_absolute() ? folder : assetsDir / folder;
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Nome de entidade pode ter caracteres que não valem em nome de arquivo.
    std::string base;
    for (char c : name) base += std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos && c >= ' ' ? c : '_';
    while (!base.empty() && (base.back() == ' ' || base.back() == '.')) base.pop_back();
    if (base.empty()) base = "Novo";

    fs::path path = dir / motor::PathFromUtf8(base + extension);
    for (int i = 2; fs::exists(path, ec); ++i) path = dir / motor::PathFromUtf8(std::format("{} {}{}", base, i, extension));
    return path;
}

bool EditorContext::ApplyMaterialFile(EntityId id, const fs::path& file) {
    motor::Entity* e = scene.Find(id);
    if (!e) return false;
    const std::string key = assets.KeyForFile(file);
    std::string error = "O arquivo está fora do projeto.";
    auto material = key.empty() ? nullptr : assets.GetMaterial(key, &error);
    if (!material) {
        pendingError = std::move(error);
        return false;
    }
    motor::MeshRenderer* mr = e->Get<motor::MeshRenderer>();
    if (!mr) {
        mr = &e->Add<motor::MeshRenderer>();
        mr->mesh = assets.FindMesh("Cubo");
    }
    mr->sharedMaterial = std::move(material);
    selected = id;
    return true;
}

bool EditorContext::CreateMaterialFromRenderer(motor::MeshRenderer& renderer, const std::string& name) {
    const fs::path file = NewAssetPath("Materiais", name, motor::kMaterialFileExtension);
    if (file.empty()) {
        pendingError = "Abra ou crie um projeto para criar materiais.";
        return false;
    }
    std::string error;
    if (!assets.WriteMaterialFile(file, renderer.material, &error)) {
        pendingError = std::move(error);
        return false;
    }
    auto material = assets.GetMaterial(assets.KeyForFile(file), &error);
    if (!material) {
        pendingError = std::move(error);
        return false;
    }
    renderer.sharedMaterial = std::move(material);
    motor::log::Info("Material criado: {}", assets.KeyForFile(file));
    return true;
}

motor::Entity* EditorContext::InstantiatePrefabFile(const fs::path& file, EntityId parent) {
    std::string error;
    const motor::Prefab* prefab = prefabs.GetFile(file, assets, &error);
    if (!prefab) {
        pendingError = std::move(error);
        return nullptr;
    }
    selected = motor::Prefabs::Instantiate(*prefab, scene, parent);
    return scene.Find(selected);
}

motor::Entity* EditorContext::InstantiateModelFile(const fs::path& file, EntityId parent) {
    if (motor::ModelNeedsImport(file) && !ReimportModelFile(file)) return nullptr;
    motor::Entity* e = InstantiatePrefabFile(motor::ModelPrefabPath(file), parent);
    if (e) e->name = motor::ToUtf8(file.stem());
    return e;
}

bool EditorContext::ReimportModelFile(const fs::path& file) {
    std::string error;
    if (!motor::ImportModel(file, assets, nullptr, &error)) {
        pendingError = std::move(error);
        return false;
    }
    prefabs.ReloadChanged(assets);  // prefab já em cache: as instâncias seguem o novo
    return true;
}

bool EditorContext::CreatePrefabFromEntity(EntityId id, const fs::path& folder) {
    const motor::Entity* e = scene.Find(id);
    if (!e) return false;
    const fs::path file = NewAssetPath(folder.empty() ? fs::path("Prefabs") : folder, e->name, motor::kPrefabFileExtension);
    if (file.empty()) {
        pendingError = "Abra ou crie um projeto para criar prefabs.";
        return false;
    }
    std::string error;
    if (!motor::Prefabs::CreateFromEntity(scene, id, file, prefabs, assets, &error)) {
        pendingError = std::move(error);
        return false;
    }
    motor::log::Info("Prefab criado: {}", assets.KeyForFile(file));
    return true;
}

void EditorContext::RequestPrefabAction(PrefabAction action, EntityId id) {
    pendingPrefabAction = action;
    pendingPrefabEntity = id;
}

void EditorContext::ApplyPendingPrefabAction() {
    const PrefabAction action = std::exchange(pendingPrefabAction, PrefabAction::None);
    const EntityId id = std::exchange(pendingPrefabEntity, kNullEntity);
    if (!scene.Find(id)) return;
    std::string error;
    switch (action) {
        case PrefabAction::None: break;
        case PrefabAction::Apply:
            if (!motor::Prefabs::ApplyInstance(scene, id, prefabs, assets, &error)) pendingError = std::move(error);
            break;
        case PrefabAction::Revert: motor::Prefabs::RevertInstance(scene, id, prefabs, assets); break;
        case PrefabAction::Unpack: motor::Prefabs::Unpack(scene, id, prefabs, assets); break;
    }
}

void EditorContext::UpdatePrefabs(const std::function<void(const std::function<void(motor::Scene&)>&)>& forEachSnapshot) {
    // Outro projeto (pode ser uma cópia, com os mesmos GUIDs): o cache é do projeto anterior.
    if (assets.Root() != m_prefabRoot) {
        prefabs.Clear();
        m_prefabRoot = assets.Root();
    }
    if (motor::Prefabs::NeedsSync(scene, prefabs, assets)) {
        motor::Prefabs::SyncInstances(scene, prefabs, assets);
        if (forEachSnapshot) forEachSnapshot([this](motor::Scene& s) { motor::Prefabs::SyncInstances(s, prefabs, assets); });
    }
    motor::Prefabs::RecordOverrides(scene, prefabs, assets);
}

bool EditorContext::IsSelected(EntityId id) const {
    return id != kNullEntity && std::find(selection.begin(), selection.end(), id) != selection.end();
}

void EditorContext::SelectOnly(EntityId id) {
    selected = scene.Find(id) ? id : kNullEntity;
    selection.clear();
    if (selected != kNullEntity) selection.push_back(selected);
}

void EditorContext::AddToSelection(EntityId id) {
    if (!scene.Find(id)) return;
    std::erase(selection, id);
    selection.push_back(id);
    selected = id;
}

void EditorContext::ToggleSelected(EntityId id) {
    if (!IsSelected(id)) {
        AddToSelection(id);
        return;
    }
    std::erase(selection, id);
    selected = selection.empty() ? kNullEntity : selection.back();
}

void EditorContext::SetSelection(const std::vector<EntityId>& ids) {
    selection.clear();
    selected = kNullEntity;
    for (EntityId id : ids) AddToSelection(id);
}

std::vector<EntityId> EditorContext::SelectionRoots() const {
    std::vector<EntityId> roots;
    for (EntityId id : selection) {
        if (!scene.Find(id)) continue;
        const bool underSelected = std::any_of(selection.begin(), selection.end(), [&](EntityId other) {
            return other != id && scene.IsDescendantOf(id, other);
        });
        if (!underSelected) roots.push_back(id);
    }
    return roots;
}

void EditorContext::SyncSelection() {
    if (!scene.Find(selected)) selected = kNullEntity;
    if (selected == kNullEntity) {
        selection.clear();
        return;
    }
    std::erase_if(selection, [this](EntityId id) { return !scene.Find(id); });
    if (!IsSelected(selected)) {
        selection.assign(1, selected);
    } else if (selection.back() != selected) {
        // Principal trocada por outra já selecionada (ex.: MCP): vai para o fim, as outras ficam.
        std::erase(selection, selected);
        selection.push_back(selected);
    }
}

void EditorContext::DuplicateSelected() {
    SyncSelection();
    std::vector<EntityId> copies;
    for (EntityId id : SelectionRoots()) copies.push_back(scene.Duplicate(id));
    if (!copies.empty()) SetSelection(copies);
}

void EditorContext::CopySelected() {
    SyncSelection();
    const std::vector<EntityId> roots = SelectionRoots();
    if (!roots.empty()) clipboard.Copy(scene, roots, sceneTag);
}

void EditorContext::CutSelected() {
    SyncSelection();
    const std::vector<EntityId> roots = SelectionRoots();
    if (roots.empty()) return;
    clipboard.Copy(scene, roots, sceneTag);
    for (EntityId id : roots) scene.DestroyEntity(id);
    SyncSelection();
}

void EditorContext::Paste(EntityId parent) {
    if (clipboard.Empty()) return;
    SetSelection(clipboard.Paste(scene, parent, sceneTag));
}

void EditorContext::ResetSceneState() {
    ++sceneTag;  // referências copiadas da cena anterior não valem nesta
    selected = kNullEntity;
    selection.clear();
    pendingDelete = kNullEntity;
    ForgetAngles();
}

void EditorContext::ForgetAngles(EntityId id) {
    if (id == kNullEntity) m_forgetAllAngles = true;
    else m_forgottenAngles.push_back(id);
}

bool EditorContext::TakeForgottenAngles(EntityId id) {
    const bool forgotten = m_forgetAllAngles ||
                           std::find(m_forgottenAngles.begin(), m_forgottenAngles.end(), id) != m_forgottenAngles.end();
    m_forgetAllAngles = false;
    m_forgottenAngles.clear();
    return forgotten;
}

void EditorContext::ApplyPendingDelete() {
    if (pendingDelete == kNullEntity) return;
    SyncSelection();
    if (IsSelected(pendingDelete)) {
        for (EntityId id : SelectionRoots()) scene.DestroyEntity(id);
    } else {
        scene.DestroyEntity(pendingDelete);
    }
    SyncSelection();
    pendingDelete = kNullEntity;
}

void DrawCreateEntityMenuItems(EditorContext& ctx, EntityId parent) {
    if (ImGui::MenuItem("Entidade vazia")) ctx.CreatePrimitive(Primitive::Empty, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Cubo")) ctx.CreatePrimitive(Primitive::Cube, parent);
    if (ImGui::MenuItem("Esfera")) ctx.CreatePrimitive(Primitive::Sphere, parent);
    if (ImGui::MenuItem("Plano")) ctx.CreatePrimitive(Primitive::Plane, parent);
    if (ImGui::MenuItem("Cilindro")) ctx.CreatePrimitive(Primitive::Cylinder, parent);
    if (ImGui::MenuItem("Cápsula")) ctx.CreatePrimitive(Primitive::Capsule, parent);
    if (ImGui::MenuItem("Cone")) ctx.CreatePrimitive(Primitive::Cone, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Terreno")) ctx.CreatePrimitive(Primitive::Terrain, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Câmera")) ctx.CreatePrimitive(Primitive::Camera, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Luz direcional")) ctx.CreatePrimitive(Primitive::DirectionalLight, parent);
    if (ImGui::MenuItem("Luz pontual")) ctx.CreatePrimitive(Primitive::PointLight, parent);
    if (ImGui::MenuItem("Luz spot")) ctx.CreatePrimitive(Primitive::SpotLight, parent);
    ImGui::Separator();
    if (ImGui::BeginMenu("Interface (UI)")) {
        DrawCreateUiMenuItems(ctx, parent);
        ImGui::EndMenu();
    }
}

} // namespace editor
