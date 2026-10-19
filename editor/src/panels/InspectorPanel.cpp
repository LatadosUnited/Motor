#include "EditorContext.h"
#include "drawers/FieldDrawers.h"
#include "panels/MultiEdit.h"

#include <motor/core/Paths.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cfloat>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace editor {

namespace {
const ImVec4 kOverrideColor{0.45f, 0.75f, 1.0f, 1.0f};  // propriedade mudada numa instância de prefab

// Override de prefab da entidade sendo desenhada (vazio = não é instância).
using OverrideCheck = std::function<bool(std::string_view key)>;

// Layout de duas colunas (rótulo | controle) para as propriedades.
bool BeginProperties(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void Property(const char* label, bool overridden = false) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (overridden) {
        ImGui::TextColored(kOverrideColor, "%s", label);
        ImGui::SetItemTooltip("Alterado nesta instância: não segue o prefab");
    } else {
        ImGui::TextUnformatted(label);
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool Has(const OverrideCheck& overridden, std::string_view key) {
    return overridden && overridden(key);
}

// Ângulos de Euler mostrados na Rotação, guardados para não "saltarem" na conversão quat <-> euler: só são
// recalculados quando a entidade muda, a rotação muda por fora ou alguém pede (EditorContext::ForgetAngles).
struct EulerCache {
    EntityId entity = kNullEntity;
    glm::quat source{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 degrees{0.0f};
};
EulerCache g_euler;

void DrawTransform(EditorContext& ctx, motor::Entity& e, const OverrideCheck& overridden) {
    motor::Transform& t = e.transform;

    const bool forgotten = ctx.TakeForgottenAngles(e.id);
    if (forgotten || g_euler.entity != e.id || g_euler.source != t.rotation) {
        g_euler.degrees = glm::degrees(glm::eulerAngles(t.rotation)) + 0.0f;  // + 0 elimina o "-0.0"
        g_euler.entity = e.id;
        g_euler.source = t.rotation;
    }

    if (!BeginProperties("##transform")) return;

    Property("Posição", Has(overridden, "position"));
    ImGui::DragFloat3("##position", &t.position.x, 0.05f, 0.0f, 0.0f, "%.2f");

    Property("Rotação", Has(overridden, "rotation"));
    if (ImGui::DragFloat3("##rotation", &g_euler.degrees.x, 0.5f, 0.0f, 0.0f, "%.1f°")) {
        t.SetEulerDegrees(g_euler.degrees);
        g_euler.source = t.rotation;
    }

    Property("Escala", Has(overridden, "scale"));
    ImGui::DragFloat3("##scale", &t.scale.x, 0.02f, 0.0f, 0.0f, "%.2f");

    ImGui::EndTable();
}

// Combo de malha/textura ligado direto ao campo do componente.
template <class Asset>
void AssetField(EditorContext& ctx, const char* id, motor::AssetType type, std::shared_ptr<Asset>& value) {
    const auto ref = ctx.assets.RefOf(value.get());
    const auto key = AssetCombo(ctx, id, type, ref ? &ref->name : nullptr);
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
void DrawField(EditorContext& ctx, const motor::FieldInfo& f, void* component, bool overridden = false) {
    using motor::FieldType;
    const motor::FieldOptions& o = f.options;
    Property(f.label.c_str(), overridden);
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
        case FieldType::Custom:
            if (const FieldDrawerFn drawer = FindFieldDrawer(f.custom->name)) {
                drawer(ctx, f, f.Get(component));
            } else {
                ImGui::TextDisabled("(sem editor para \"%s\")", f.custom->name.c_str());
            }
            break;
    }
    if (o.tooltip) ImGui::SetItemTooltip("%s", o.tooltip);
    ImGui::PopID();
}

// --- Materiais de asset (.mmat) ---------------------------------------------------------

// Campos de um motor::Material, descritos como os de um componente para reaproveitar DrawField e a comparação.
const motor::ComponentType& MaterialFields() {
    static const motor::ComponentType type = [] {
        motor::ComponentType t;
        t.name = "Material";
        motor::ComponentBuilder<motor::Material>(t)
            .Field("texture", "Textura", &motor::Material::albedo)
            .Field("color", "Cor", &motor::Material::color, {.color = true})
            .Field("blend", "Alfa", &motor::Material::blend,
                   {.tooltip = "Opaco; Recorte (folhas, grades); Transparente (vidro, água)"})
            .Field("alphaCutoff", "Limite do recorte", &motor::Material::alphaCutoff,
                   {.slider = true, .min = 0.0f, .max = 1.0f})
            .Field("doubleSided", "Duas faces", &motor::Material::doubleSided)
            .Field("metallic", "Metal", &motor::Material::metallic, {.slider = true, .min = 0.0f, .max = 1.0f})
            .Field("roughness", "Rugosidade", &motor::Material::roughness, {.slider = true, .min = 0.0f, .max = 1.0f})
            .Field("specular", "Reflexo", &motor::Material::specular,
                   {.slider = true, .min = 0.0f, .max = 1.0f, .tooltip = "Reflexo dos não metais: 0.5 = 4% (o comum)"})
            .Field("normalMap", "Mapa de normal", &motor::Material::normalMap)
            .Field("normalStrength", "Força da normal", &motor::Material::normalStrength,
                   {.slider = true, .min = 0.0f, .max = 2.0f})
            .Field("metallicRoughnessMap", "Mapa metal/rugosidade", &motor::Material::metallicRoughnessMap,
                   {.tooltip = "Como no glTF: verde = rugosidade, azul = metal"})
            .Field("occlusionMap", "Mapa de oclusão", &motor::Material::occlusionMap)
            .Field("occlusionStrength", "Força da oclusão", &motor::Material::occlusionStrength,
                   {.slider = true, .min = 0.0f, .max = 1.0f})
            .Field("emissive", "Emissão", &motor::Material::emissive, {.color = true})
            .Field("emissiveIntensity", "Intensidade da emissão", &motor::Material::emissiveIntensity,
                   {.speed = 0.05f, .min = 0.0f, .max = 100.0f, .format = "%.2f"})
            .Field("emissiveMap", "Mapa de emissão", &motor::Material::emissiveMap);
        return t;
    }();
    return type;
}

// Campos do Mesh Renderer que são o material próprio (FieldRef para MeshRenderer::material, MeshRenderer.cpp).
bool IsOwnMaterialField(std::string_view key) {
    // Os campos do material próprio têm os mesmos nomes dos de MaterialFields.
    for (const motor::FieldInfo& f : MaterialFields().fields) {
        if (f.key == key) return true;
    }
    return false;
}

// Materiais editados no Inspector e os valores gravados por último. Quando um muda e o usuário termina a
// edição (soltou o slider), o .mmat é gravado. weak_ptr: o material pode sumir (troca de projeto).
struct EditedMaterial {
    std::weak_ptr<motor::Material> material;
    motor::Material saved;
};
std::map<const motor::Material*, EditedMaterial> s_editedMaterials;

void TrackMaterial(EditorContext& ctx, const motor::Material& m) {
    if (s_editedMaterials.contains(&m)) return;
    for (const auto& entry : ctx.assets.Materials()) {
        if (entry.asset.get() == &m) s_editedMaterials[&m] = {entry.asset, m};
    }
}

void SaveEditedMaterials(EditorContext& ctx, bool interacting) {
    for (auto it = s_editedMaterials.begin(); it != s_editedMaterials.end();) {
        const auto material = it->second.material.lock();
        if (!material) {
            it = s_editedMaterials.erase(it);
            continue;
        }
        if (!interacting && !motor::ComponentsEqual(MaterialFields(), material.get(), &it->second.saved)) {
            std::string error;
            if (!ctx.assets.SaveMaterial(material.get(), &error)) ctx.pendingError = std::move(error);
            it->second.saved = *material;
        }
        ++it;
    }
}

// Linhas da tabela de propriedades com os campos do material de asset (editar muda todos que o usam).
void DrawMaterialRows(EditorContext& ctx, motor::Material& m) {
    TrackMaterial(ctx, m);
    ImGui::PushID(&m);
    for (const motor::FieldInfo& f : MaterialFields().fields) DrawField(ctx, f, &m);
    ImGui::PopID();
}

// Quantos Mesh Renderers da cena usam o material.
size_t CountUsers(const motor::Scene& scene, const motor::Material* m) {
    size_t count = 0;
    for (const auto& e : scene.Entities()) {
        const auto* mr = e->Get<motor::MeshRenderer>();
        if (mr && mr->sharedMaterial.get() == m) ++count;
    }
    return count;
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
bool DrawComponent(EditorContext& ctx, motor::ComponentPtr& component, const std::string& entityName,
                   const OverrideCheck& overridden) {
    const motor::ComponentType& type = component.Type();
    if (&type == &motor::ComponentRegistry::MissingType()) {
        return DrawMissingComponent(*static_cast<const motor::MissingComponent*>(component.Data()));
    }
    ImGui::PushID(type.name.c_str());
    bool keep = true;
    const bool added = Has(overridden, "+" + type.name);
    if (added) ImGui::PushStyleColor(ImGuiCol_Text, kOverrideColor);
    const bool open = ImGui::CollapsingHeader(type.label.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen);
    if (added) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Componente adicionado nesta instância (o prefab não tem)");
    }
    if (ImGui::BeginPopupContextItem("##component_ctx")) {
        if (ImGui::MenuItem("Redefinir valores")) {
            component = motor::ComponentPtr(type);
            if (type.onAdd) type.onAdd(component.Data(), ctx.assets);
        }
        if (ImGui::MenuItem("Remover componente")) keep = false;
        ImGui::EndPopup();
    }
    if (open && keep && BeginProperties("##fields")) {
        // Mesh Renderer com asset de material: os campos do material próprio não valem; no lugar deles vão
        // os do asset (compartilhado).
        auto* renderer = &type == motor::ComponentRegistry::TypeOf<motor::MeshRenderer>()
                             ? static_cast<motor::MeshRenderer*>(component.Data())
                             : nullptr;
        for (const motor::FieldInfo& f : type.fields) {
            if (renderer && renderer->sharedMaterial && IsOwnMaterialField(f.key)) continue;
            DrawField(ctx, f, component.Data(), Has(overridden, type.name + "." + f.key));
            if (!renderer || f.key != "material") continue;
            if (renderer->sharedMaterial) {
                const size_t users = CountUsers(ctx.scene, renderer->sharedMaterial.get());
                Property("");
                ImGui::TextDisabled("Asset compartilhado (%zu na cena)", users);
                ImGui::SetItemTooltip("Mudar os campos abaixo muda todos os objetos que usam este material.\n"
                                      "O arquivo .mmat é gravado quando você solta o controle.");
                DrawMaterialRows(ctx, *renderer->sharedMaterial);
            } else {
                Property("");
                if (ImGui::SmallButton("Salvar como asset")) ctx.CreateMaterialFromRenderer(*renderer, entityName);
                ImGui::SetItemTooltip("Grava o material deste objeto em Assets/Materiais (.mmat) e passa a usá-lo");
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    return keep;
}

// Nome, transform, componentes e "Adicionar componente". `prefabAsset`: entidade de um prefab sendo editado
// direto (sem o vínculo, que é do arquivo).
void DrawEntity(EditorContext& ctx, motor::Entity& e, bool prefabAsset) {
    const motor::PrefabInstance* instance = prefabAsset ? nullptr : e.Get<motor::PrefabInstance>();
    OverrideCheck overridden;
    if (instance) overridden = [instance](std::string_view key) { return instance->link.HasOverride(key); };
    const motor::ComponentType* prefabType = motor::ComponentRegistry::TypeOf<motor::PrefabInstance>();

    ImGui::Checkbox("##active", &e.active);
    ImGui::SetItemTooltip(Has(overridden, "active") ? "Ativa (alterado nesta instância)" : "Ativa");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (Has(overridden, "name")) ImGui::PushStyleColor(ImGuiCol_Text, kOverrideColor);
    ImGui::InputText("##name", &e.name);
    if (Has(overridden, "name")) ImGui::PopStyleColor();
    if (!prefabAsset) ImGui::TextDisabled("ID %u", e.id);
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawTransform(ctx, e, overridden);
    }

    // Removido depois do laço, para não invalidar a lista. Pelo índice: pode haver vários desconhecidos.
    std::optional<size_t> toRemove;
    for (size_t i = 0; i < e.components.size(); ++i) {
        if (prefabAsset && &e.components[i].Type() == prefabType) continue;
        ImGui::PushID(static_cast<int>(i));
        if (!DrawComponent(ctx, e.components[i], e.name, overridden)) toRemove = i;
        ImGui::PopID();
    }
    if (toRemove) e.components.erase(e.components.begin() + static_cast<std::ptrdiff_t>(*toRemove));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float buttonWidth = 180.0f;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f));
    if (ImGui::Button("Adicionar componente", ImVec2(buttonWidth, 0.0f))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        // Todos os tipos registrados, scripts por último; os que a entidade já tem aparecem desativados (um de
        // cada). O vínculo com prefab não entra: quem cria é o editor, ao instanciar.
        for (const bool scripts : {false, true}) {
            if (scripts) ImGui::SeparatorText("Scripts");
            for (const auto& type : motor::ComponentRegistry::Instance().Types()) {
                if (type->IsScript() != scripts || type.get() == prefabType) continue;
                if (ImGui::MenuItem(type->label.c_str(), nullptr, false, !e.Get(*type))) {
                    void* data = e.Add(*type);
                    if (type->onAdd) type->onAdd(data, ctx.assets);
                }
            }
        }
        ImGui::EndPopup();
    }
}

// --- Assets selecionados no painel Projeto -------------------------------------------------

void AssetHeader(EditorContext& ctx, const std::filesystem::path& file, const char* kind) {
    ImGui::TextUnformatted(motor::ToUtf8(file.stem()).c_str());
    ImGui::TextDisabled("%s  ·  %s", kind, ctx.assets.KeyForFile(file).c_str());
    ImGui::Spacing();
}

void DrawMaterialAsset(EditorContext& ctx, const std::filesystem::path& file) {
    AssetHeader(ctx, file, "Material");
    std::string error;
    const auto material = ctx.assets.GetMaterial(ctx.assets.KeyForFile(file), &error);
    if (!material) {
        ImGui::TextWrapped("%s", error.c_str());
        return;
    }
    ImGui::TextDisabled("Usado por %zu objeto(s) da cena. Mudanças valem para todos.",
                        CountUsers(ctx.scene, material.get()));
    if (BeginProperties("##material")) {
        DrawMaterialRows(ctx, *material);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Arraste o material para um objeto (viewport ou Hierarquia) para aplicá-lo.");
}

// Prefab editado direto no Inspector. Cada mudança vira uma revisão nova (as instâncias seguem na hora) e o
// arquivo é gravado quando a edição termina.
struct PrefabEdit {
    std::string guid;
    EntityId entity = kNullEntity;  // entidade do prefab mostrada
    motor::Scene saved;             // conteúdo da última revisão vista
    uint64_t revision = 0;
    bool dirty = false;             // mudou e ainda não foi gravado
};
PrefabEdit s_prefabEdit;

void DrawPrefabTree(EditorContext& ctx, const motor::Prefab& prefab, EntityId id, int depth) {
    const motor::Entity* e = prefab.scene.Find(id);
    if (!e) return;
    ImGui::PushID(static_cast<int>(id));
    ImGui::Indent(static_cast<float>(depth) * ImGui::GetStyle().IndentSpacing * 0.6f);
    if (ImGui::Selectable(e->name.c_str(), s_prefabEdit.entity == id)) s_prefabEdit.entity = id;
    ImGui::Unindent(static_cast<float>(depth) * ImGui::GetStyle().IndentSpacing * 0.6f);
    ImGui::PopID();
    for (EntityId child : prefab.scene.Children(id)) DrawPrefabTree(ctx, prefab, child, depth + 1);
    (void)ctx;
}

void DrawPrefabAsset(EditorContext& ctx, const std::filesystem::path& file) {
    AssetHeader(ctx, file, "Prefab");
    std::string error;
    motor::Prefab* prefab = ctx.prefabs.GetFile(file, ctx.assets, &error);
    if (!prefab) {
        ImGui::TextWrapped("%s", error.c_str());
        return;
    }
    PrefabEdit& edit = s_prefabEdit;
    if (edit.guid != prefab->guid || edit.revision != prefab->revision) {
        // Outro prefab, ou este mudou por fora (arquivo relido, instância aplicada).
        if (edit.guid != prefab->guid || !prefab->scene.Find(edit.entity)) edit.entity = prefab->root;
        edit.guid = prefab->guid;
        edit.saved = prefab->scene;
        edit.revision = prefab->revision;
    }

    size_t instances = 0;
    for (const auto& e : ctx.scene.Entities()) {
        if (motor::Prefabs::IsInstanceRoot(ctx.scene, e->id, ctx.prefabs, ctx.assets)) {
            const auto* p = e->Get<motor::PrefabInstance>();
            if (p && p->link.guid == prefab->guid) ++instances;
        }
    }
    ImGui::TextDisabled("%zu instância(s) na cena. Mudanças aqui valem para todas.", instances);
    if (ImGui::Button("Adicionar à cena")) ctx.InstantiatePrefabFile(file);

    ImGui::SeparatorText("Entidades");
    DrawPrefabTree(ctx, *prefab, prefab->root, 0);
    ImGui::Separator();
    ImGui::Spacing();

    motor::Entity* e = prefab->scene.Find(edit.entity);
    if (!e) e = prefab->scene.Find(prefab->root);
    ImGui::PushID("prefab_entity");
    DrawEntity(ctx, *e, true);
    ImGui::PopID();
    if (e->id == prefab->root) {
        ImGui::TextDisabled("A posição e a rotação da raiz são de cada instância.");
    }

    if (!(prefab->scene == edit.saved)) {
        ctx.prefabs.MarkChanged(*prefab);
        edit.saved = prefab->scene;
        edit.revision = prefab->revision;
        edit.dirty = true;
    }
}

// Grava o prefab editado quando a edição termina (mesmo que o Inspector já mostre outra coisa).
void SavePrefabEdit(EditorContext& ctx, bool interacting) {
    PrefabEdit& edit = s_prefabEdit;
    if (!edit.dirty || interacting) return;
    edit.dirty = false;
    std::string error;
    motor::Prefab* prefab = ctx.prefabs.Get(edit.guid, ctx.assets, &error);
    if (!prefab || !ctx.prefabs.Save(*prefab, ctx.assets, &error)) ctx.pendingError = std::move(error);
}
} // namespace

void DrawInspectorPanel(EditorContext& ctx) {
    const bool interacting = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    SaveEditedMaterials(ctx, interacting);
    SavePrefabEdit(ctx, interacting);

    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    motor::Entity* e = ctx.scene.Find(ctx.selected);
    if (!e) {
        const motor::AssetType assetType = motor::AssetTypeFromPath(ctx.inspectedAsset);
        std::error_code ec;
        if (!ctx.inspectedAsset.empty() && std::filesystem::exists(ctx.inspectedAsset, ec)) {
            if (assetType == motor::AssetType::Material) DrawMaterialAsset(ctx, ctx.inspectedAsset);
            if (assetType == motor::AssetType::Prefab) DrawPrefabAsset(ctx, ctx.inspectedAsset);
        } else {
            ImGui::TextDisabled("Nenhuma entidade selecionada.");
        }
        ImGui::End();
        return;
    }

    if (ctx.SelectionCount() <= 1) {
        DrawEntity(ctx, *e, false);
        ImGui::End();
        return;
    }

    // Vários selecionados: mostra a principal e repete nos outros o que mudar nela (MultiEdit.h).
    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.15f, 1.0f), "%zu objetos selecionados", ctx.SelectionCount());
    ImGui::SetItemTooltip("Mostrando \"%s\" (o último escolhido). O que você mudar aqui vale para todos os\n"
                          "selecionados que têm o mesmo componente; posição, rotação e escala só no eixo mudado.",
                          e->name.c_str());
    ImGui::Separator();
    const motor::Entity before = *e;
    DrawEntity(ctx, *e, false);
    if (const motor::Entity* after = ctx.scene.Find(ctx.selected)) {
        for (EntityId id : ctx.selection) {
            motor::Entity* target = id == ctx.selected ? nullptr : ctx.scene.Find(id);
            if (target) PropagateEdits(before, *after, *target, ctx.assets);
        }
    }
    ImGui::End();
}

} // namespace editor
