#include "McpSceneTools.h"

#include <motor/scene/SceneSerializer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>

namespace editor::mcp {

using namespace motor;

namespace {
const char* FieldTypeName(const FieldInfo& f) {
    switch (f.type) {
        case FieldType::Bool: return "bool";
        case FieldType::Int: return "int";
        case FieldType::Float: return "float";
        case FieldType::Vec2: return "vec2";
        case FieldType::Vec3: return "vec3";
        case FieldType::Vec4: return "vec4";
        case FieldType::String: return "string";
        case FieldType::Mesh: return "mesh";
        case FieldType::Texture: return "texture";
        case FieldType::Custom: return f.custom ? f.custom->name.c_str() : "custom";
    }
    return "?";
}

Json Vec3Json(const glm::vec3& v) {
    return Json::array({v.x, v.y, v.z});
}

glm::vec3 ReadVec3(const Json& value, const char* what) {
    if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number()) {
        throw ToolError(std::format("'{}' deve ser uma lista de 3 números, ex.: [0, 1.5, -2]", what));
    }
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Tipo pelo nome registrado ("MeshRenderer") ou pelo rótulo do editor ("Mesh Renderer"), sem diferenciar
// maiúsculas.
const ComponentType& ResolveType(const Json& args) {
    if (!args.contains("type") || !args["type"].is_string()) throw ToolError("informe 'type' (nome do componente)");
    const std::string wanted = Lower(args["type"].get<std::string>());
    for (const auto& type : ComponentRegistry::Instance().Types()) {
        if (Lower(type->name) == wanted || Lower(type->label) == wanted) return *type;
    }
    throw ToolError("componente desconhecido: " + args["type"].get<std::string>() +
                    " (veja os tipos com component_types)");
}

// Muda campos de um componente a partir de {chave: valor} no formato da cena. Chave desconhecida ou valor de
// tipo errado = erro (nada é mudado antes de conferir todos).
void SetFields(EditorContext& ctx, const ComponentType& type, void* data, const Json& values) {
    if (!values.is_object()) throw ToolError("'values' deve ser um objeto {campo: valor}");
    std::vector<std::pair<const FieldInfo*, std::string>> changes;
    for (auto it = values.begin(); it != values.end(); ++it) {
        auto field = std::find_if(type.fields.begin(), type.fields.end(), [&](const FieldInfo& f) {
            return f.key == it.key() || Lower(f.label) == Lower(it.key());
        });
        if (field == type.fields.end()) {
            std::string keys;
            for (const FieldInfo& f : type.fields) keys += (keys.empty() ? "" : ", ") + f.key;
            throw ToolError(std::format("{} não tem o campo '{}' (campos: {})", type.name, it.key(), keys));
        }
        changes.emplace_back(&*field, it.value().dump());
    }
    std::unique_ptr<void, std::function<void(void*)>> preview(type.clone(data), type.destroy);
    for (const auto& [field, dumped] : changes) {
        std::string error;
        if (!SceneSerializer::FieldValueFromJson(*field, preview.get(), dumped, ctx.assets, &error)) {
            throw ToolError(error);
        }
    }
    for (const auto& [field, dumped] : changes) {
        SceneSerializer::FieldValueFromJson(*field, data, dumped, ctx.assets, nullptr);
    }
}

void ApplyTransformArgs(EditorContext& ctx, Entity& e, const Json& args) {
    if (args.contains("position")) e.transform.position = ReadVec3(args["position"], "position");
    if (args.contains("rotation")) {
        e.transform.SetEulerDegrees(ReadVec3(args["rotation"], "rotation"));
        ctx.ForgetAngles(e.id);  // o Inspector recalcula os ângulos
    }
    if (args.contains("scale")) e.transform.scale = ReadVec3(args["scale"], "scale");
}

EntityId ParentArg(EditorContext& ctx, const Json& args) {
    if (!args.contains("parent") || args["parent"].is_null()) return kNullEntity;
    if (args["parent"].is_number_integer() && args["parent"].get<int64_t>() == 0) return kNullEntity;
    return ResolveEntity(ctx, args, "parent").id;
}

Json TreeNode(const EditorContext& ctx, const Entity& e, bool components) {
    Json node = {{"id", e.id}, {"name", e.name}};
    if (!e.active) node["active"] = false;
    if (components) {
        Json types = Json::array();
        for (const ComponentPtr& c : e.components) {
            if (&c.Type() == &ComponentRegistry::MissingType()) {
                types.push_back("? " + static_cast<const MissingComponent*>(c.Data())->type);
            } else {
                types.push_back(c.Type().name);
            }
        }
        if (!types.empty()) node["components"] = std::move(types);
    }
    Json children = Json::array();
    for (EntityId child : ctx.scene.Children(e.id)) children.push_back(TreeNode(ctx, *ctx.scene.Find(child), components));
    if (!children.empty()) node["children"] = std::move(children);
    return node;
}

const Json kEntityProp = {{"type", {"integer", "string"}}, {"description", "id da entidade ou o nome dela"}};
const Json kVec3Prop = {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}};

Json Vec3Prop(const char* description) {
    Json p = kVec3Prop;
    p["description"] = description;
    return p;
}
} // namespace

Entity& ResolveEntity(EditorContext& ctx, const Json& args, const char* key) {
    if (!args.contains(key)) throw ToolError(std::format("informe '{}' (id ou nome da entidade)", key));
    const Json& value = args[key];
    Entity* e = nullptr;
    if (value.is_number_integer()) {
        e = ctx.scene.Find(static_cast<EntityId>(value.get<int64_t>()));
    } else if (value.is_string()) {
        e = ctx.scene.FindByName(value.get<std::string>());
    }
    if (!e) throw ToolError(std::format("entidade não encontrada: {} (veja os ids com scene_tree)", value.dump()));
    return *e;
}

Json EntityToJson(const EditorContext& ctx, const Entity& e) {
    const Transform& t = e.transform;
    Json j = {
        {"id", e.id},
        {"name", e.name},
        {"active", e.active},
        {"parent", e.parent},
        {"transform", {{"position", Vec3Json(t.position)},
                       {"rotation", Vec3Json(glm::degrees(glm::eulerAngles(t.rotation)) + 0.0f)},
                       {"scale", Vec3Json(t.scale)}}},
    };
    const glm::vec3 world = glm::vec3(ctx.scene.WorldMatrix(e.id)[3]);
    j["world_position"] = Vec3Json(world);

    Json components = Json::array();
    for (const ComponentPtr& c : e.components) {
        if (&c.Type() == &ComponentRegistry::MissingType()) {
            const auto& missing = *static_cast<const MissingComponent*>(c.Data());
            components.push_back({{"type", missing.type}, {"missing", true},
                                  {"note", "script não carregado: os dados ficam guardados até ele voltar"}});
            continue;
        }
        Json fields = Json::object();
        for (const FieldInfo& f : c.Type().fields) {
            fields[f.key] = Json::parse(SceneSerializer::FieldValueToJson(f, c.Data(), ctx.assets), nullptr, false);
        }
        Json comp = {{"type", c.Type().name}, {"label", c.Type().label}};
        if (c.Type().IsScript()) comp["script"] = true;
        comp["fields"] = std::move(fields);
        components.push_back(std::move(comp));
    }
    j["components"] = std::move(components);
    Json children = Json::array();
    for (EntityId child : ctx.scene.Children(e.id)) children.push_back(child);
    if (!children.empty()) j["children"] = std::move(children);
    return j;
}

void ThrowPendingError(EditorContext& ctx) {
    if (!ctx.pendingError.empty()) throw ToolError(std::exchange(ctx.pendingError, {}));
}

// Ambiente da cena como o scene_environment aceita.
static Json EnvironmentJson(const EditorContext& ctx) {
    const Environment& env = ctx.scene.environment;
    Json skyTexture = nullptr;
    if (env.skyTexture) {
        if (const auto ref = ctx.assets.RefOf(env.skyTexture.get())) skyTexture = ref->name;
    }
    return {{"sky_mode", SkyModeName(env.skyMode)},
            {"sky_color", Vec3Json(env.skyColor)},
            {"sky_texture", std::move(skyTexture)},
            {"sky_intensity", env.skyIntensity},
            {"sky_rotation", env.skyRotation},
            {"sky_light", env.skyLight},
            {"fog_density", env.fogDensity},
            {"ambient", Vec3Json(env.ambient)},
            {"shadow_distance", env.shadowDistance},
            {"exposure", env.exposure},
            {"tone_mapper", ToneMapperName(env.toneMapper)},
            {"bloom", env.bloom},
            {"bloom_intensity", env.bloomIntensity},
            {"bloom_threshold", env.bloomThreshold},
            {"fxaa", env.fxaa}};
}

namespace {
// Hierarquia, ler, criar e mudar entidades.
void AddEntityTools(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "scene_tree",
        .description = "Hierarquia da cena aberta: entidades (id, nome) com os filhos aninhados e os tipos de "
                       "componente de cada uma. Use antes de mexer em entidades para descobrir os ids.",
        .inputSchema = Schema({{"components", Prop("boolean", "listar os componentes de cada entidade (padrão: true)")}}),
        .readOnly = true,
        .run = [&ctx](const Json& args) {
            const bool components = args.value("components", true);
            Json roots = Json::array();
            for (EntityId id : ctx.scene.Children(kNullEntity)) roots.push_back(TreeNode(ctx, *ctx.scene.Find(id), components));
            return ToolResult::Data({{"entity_count", ctx.scene.Entities().size()},
                                     {"selected", ctx.selected},
                                     {"environment", EnvironmentJson(ctx)},
                                     {"entities", std::move(roots)}});
        },
    });

    protocol.AddTool({
        .name = "entity_get",
        .description = "Tudo de uma entidade: transform (posição, rotação em graus, escala), posição no mundo, "
                       "filhos e cada componente com os valores dos campos (no formato aceito por component_set).",
        .inputSchema = Schema({{"entity", kEntityProp}}, {"entity"}),
        .readOnly = true,
        .run = [&ctx](const Json& args) { return ToolResult::Data(EntityToJson(ctx, ResolveEntity(ctx, args))); },
    });

    protocol.AddTool({
        .name = "entity_create",
        .description = "Cria uma entidade. kind: empty, cube, sphere, plane, cylinder, capsule, cone, camera, "
                       "directional_light, point_light ou spot_light (com malha/câmera/luz já configuradas, como "
                       "no menu Entidade; as formas já vêm com o Collider que bate com elas). Fica selecionada. "
                       "Devolve a entidade criada.",
        .inputSchema = Schema({{"kind", {{"type", "string"},
                                         {"enum", {"empty", "cube", "sphere", "plane", "cylinder", "capsule", "cone",
                                                   "camera", "directional_light", "point_light", "spot_light"}}}},
                               {"name", Prop("string", "nome (padrão: o do tipo)")},
                               {"parent", kEntityProp},
                               {"position", Vec3Prop("posição local [x, y, z]")},
                               {"rotation", Vec3Prop("rotação em graus [x, y, z]")},
                               {"scale", Vec3Prop("escala [x, y, z]")}},
                              {"kind"}),
        .run = [&ctx](const Json& args) {
            static const std::pair<const char*, Primitive> kinds[] = {
                {"empty", Primitive::Empty}, {"cube", Primitive::Cube}, {"sphere", Primitive::Sphere},
                {"plane", Primitive::Plane}, {"camera", Primitive::Camera},
                {"directional_light", Primitive::DirectionalLight}, {"point_light", Primitive::PointLight},
                {"spot_light", Primitive::SpotLight}, {"cylinder", Primitive::Cylinder},
                {"capsule", Primitive::Capsule}, {"cone", Primitive::Cone}};
            const std::string kind = args.value("kind", "");
            auto it = std::find_if(std::begin(kinds), std::end(kinds), [&](const auto& k) { return kind == k.first; });
            if (it == std::end(kinds)) throw ToolError("kind inválido: " + kind);
            const EntityId parent = ParentArg(ctx, args);
            Entity& e = ctx.CreatePrimitive(it->second, parent);
            if (args.contains("name")) e.name = args["name"].get<std::string>();
            ApplyTransformArgs(ctx, e, args);
            return ToolResult::Data(EntityToJson(ctx, e));
        },
    });

    protocol.AddTool({
        .name = "entity_update",
        .description = "Muda nome, ativo, pai (0 = raiz; mantém a posição no mundo) e/ou o transform local "
                       "(position, rotation em graus, scale) de uma entidade. Só os argumentos passados mudam.",
        .inputSchema = Schema({{"entity", kEntityProp},
                               {"name", Prop("string", "novo nome")},
                               {"active", Prop("boolean", "ativa ou não")},
                               {"parent", kEntityProp},
                               {"position", Vec3Prop("posição local [x, y, z]")},
                               {"rotation", Vec3Prop("rotação em graus [x, y, z]")},
                               {"scale", Vec3Prop("escala [x, y, z]")}},
                              {"entity"}),
        .run = [&ctx](const Json& args) {
            Entity& e = ResolveEntity(ctx, args);
            if (args.contains("parent")) {
                const EntityId parent = ParentArg(ctx, args);
                if (!ctx.scene.SetParent(e.id, parent)) throw ToolError("não dá para pôr a entidade dentro dela mesma ou de um filho");
            }
            if (args.contains("name")) e.name = args["name"].get<std::string>();
            if (args.contains("active")) e.active = args["active"].get<bool>();
            ApplyTransformArgs(ctx, e, args);
            return ToolResult::Data(EntityToJson(ctx, e));
        },
    });
}

// Apagar, duplicar e selecionar entidades.
void AddEntityEditTools(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "entity_delete",
        .description = "Apaga uma entidade e todos os filhos (dá para desfazer com undo).",
        .inputSchema = Schema({{"entity", kEntityProp}}, {"entity"}),
        .run = [&ctx](const Json& args) {
            Entity& e = ResolveEntity(ctx, args);
            const std::string name = e.name;
            const EntityId id = e.id;
            ctx.scene.DestroyEntity(id);
            if (!ctx.scene.Find(ctx.selected)) ctx.selected = kNullEntity;
            if (!ctx.scene.Find(ctx.pendingDelete)) ctx.pendingDelete = kNullEntity;
            return ToolResult::Text(std::format("apagada: {} (id {})", name, id));
        },
    });

    protocol.AddTool({
        .name = "entity_duplicate",
        .description = "Duplica uma entidade com os filhos (mesmo pai). Devolve a cópia.",
        .inputSchema = Schema({{"entity", kEntityProp}}, {"entity"}),
        .run = [&ctx](const Json& args) {
            const EntityId copy = ctx.scene.Duplicate(ResolveEntity(ctx, args).id);
            ctx.selected = copy;
            return ToolResult::Data(EntityToJson(ctx, *ctx.scene.Find(copy)));
        },
    });

    protocol.AddTool({
        .name = "select",
        .description = "Seleciona uma entidade no editor (Hierarquia e Inspector), ou nenhuma com entity = 0.",
        .inputSchema = Schema({{"entity", kEntityProp}}, {"entity"}),
        .run = [&ctx](const Json& args) {
            if (args["entity"].is_number_integer() && args["entity"].get<int64_t>() == 0) {
                ctx.selected = kNullEntity;
                return ToolResult::Text("nenhuma entidade selecionada");
            }
            Entity& e = ResolveEntity(ctx, args);
            ctx.selected = e.id;
            ctx.inspectedAsset.clear();
            return ToolResult::Text(std::format("selecionada: {} (id {})", e.name, e.id));
        },
    });
}

// Componentes: tipos registrados, adicionar, remover e mudar campos.
void AddComponentTools(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "component_types",
        .description = "Tipos de componente registrados (da engine e os scripts do projeto carregados) com os "
                       "campos: chave, rótulo e tipo do valor. Use para saber o que component_add/set aceitam.",
        .inputSchema = Schema({{"scripts_only", Prop("boolean", "só os scripts do projeto")}}),
        .readOnly = true,
        .run = [](const Json& args) {
            const bool scriptsOnly = args.value("scripts_only", false);
            Json types = Json::array();
            for (const auto& type : ComponentRegistry::Instance().Types()) {
                if (scriptsOnly && !type->IsScript()) continue;
                Json fields = Json::array();
                for (const FieldInfo& f : type->fields) {
                    fields.push_back({{"key", f.key}, {"label", f.label}, {"type", FieldTypeName(f)}});
                }
                Json t = {{"name", type->name}, {"label", type->label}};
                if (type->IsScript()) t["script"] = true;
                t["fields"] = std::move(fields);
                types.push_back(std::move(t));
            }
            return ToolResult::Data({{"types", std::move(types)},
                                     {"value_formats", "bool, int, float; vec2/3/4 = lista de números; string; mesh/"
                                                       "texture = nome embutido (\"Cubo\", \"Xadrez\") ou "
                                                       "{\"path\": \"Assets/...\"}; null tira o asset"}});
        },
    });

    protocol.AddTool({
        .name = "component_add",
        .description = "Adiciona um componente (ou script) a uma entidade, com os valores iniciais do editor, e "
                       "opcionalmente muda campos (values). Se já existir, só muda os campos.",
        .inputSchema = Schema({{"entity", kEntityProp},
                               {"type", Prop("string", "nome do componente, ex.: Rigidbody, Collider, Girar")},
                               {"values", Prop("object", "{campo: valor} opcionais, como em component_set")}},
                              {"entity", "type"}),
        .run = [&ctx](const Json& args) {
            Entity& e = ResolveEntity(ctx, args);
            const ComponentType& type = ResolveType(args);
            const bool existed = e.Get(type) != nullptr;
            void* data = e.Add(type);
            if (!existed && type.onAdd) type.onAdd(data, ctx.assets);
            if (args.contains("values")) {
                try {
                    SetFields(ctx, type, data, args["values"]);
                } catch (...) {
                    if (!existed) e.Remove(type);
                    throw;
                }
            }
            return ToolResult::Data(EntityToJson(ctx, e));
        },
    });

    protocol.AddTool({
        .name = "component_remove",
        .description = "Tira um componente de uma entidade.",
        .inputSchema = Schema({{"entity", kEntityProp}, {"type", Prop("string", "nome do componente")}},
                              {"entity", "type"}),
        .run = [&ctx](const Json& args) {
            Entity& e = ResolveEntity(ctx, args);
            const ComponentType& type = ResolveType(args);
            if (!e.Get(type)) throw ToolError(std::format("{} não tem {}", e.name, type.name));
            e.Remove(type);
            return ToolResult::Data(EntityToJson(ctx, e));
        },
    });

    protocol.AddTool({
        .name = "component_set",
        .description = "Muda campos de um componente de uma entidade. values = {campo: valor} no formato da cena "
                       "(o mesmo que entity_get mostra): números, listas para vetores/cores ([r, g, b, a] de 0 a 1), "
                       "textos, malha/textura pelo nome ou {\"path\": \"Assets/...\"}.",
        .inputSchema = Schema({{"entity", kEntityProp},
                               {"type", Prop("string", "nome do componente, ex.: MeshRenderer")},
                               {"values", Prop("object", "{campo: valor}")}},
                              {"entity", "type", "values"}),
        .run = [&ctx](const Json& args) {
            Entity& e = ResolveEntity(ctx, args);
            const ComponentType& type = ResolveType(args);
            void* data = e.Get(type);
            if (!data) throw ToolError(std::format("{} não tem {} (use component_add)", e.name, type.name));
            SetFields(ctx, type, data, args["values"]);
            return ToolResult::Data(EntityToJson(ctx, e));
        },
    });
}

// Ambiente da cena: céu, luz ambiente, sombras e pós-processamento.
void AddEnvironmentTools(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "scene_environment",
        .description = "Lê ou muda o ambiente da cena (cada mudança vira um passo de desfazer). Céu: cor ou imagem "
                       "HDR (.hdr do projeto) com brilho, rotação e quanto ilumina a cena; neblina e luz ambiente; "
                       "distância das sombras; pós-processamento (exposição, tone mapping, bloom, FXAA). Cores RGB "
                       "de 0 a 1. Sem argumentos, só lê.",
        .inputSchema = Schema({{"sky_color", Vec3Prop("cor do céu (horizonte) e da neblina [r, g, b]")},
                               {"sky_mode", {{"type", "string"}, {"enum", {"Color", "Texture"}},
                                             {"description", "céu de cor (degradê) ou imagem HDR"}}},
                               {"sky_texture", Prop("string", "imagem .hdr do projeto (\"Assets/...\"); \"\" = nenhuma. "
                                                              "Escolher uma imagem muda sky_mode para Texture")},
                               {"sky_intensity", Prop("number", "brilho do céu (fundo e luz), 1 = normal")},
                               {"sky_rotation", Prop("number", "rotação do céu em graus (eixo Y)")},
                               {"sky_light", Prop("number", "quanto o céu ilumina a cena (difusa e reflexos), 0 = nada")},
                               {"fog_density", Prop("number", "densidade da neblina (0 = sem neblina)")},
                               {"ambient", Vec3Prop("luz ambiente extra [r, g, b]")},
                               {"shadow_distance", Prop("number", "metros, a partir da câmera, com sombra da luz direcional")},
                               {"exposure", Prop("number", "exposição (1 = normal)")},
                               {"tone_mapper", {{"type", "string"}, {"enum", {"ACES", "Reinhard", "None"}}}},
                               {"bloom", Prop("boolean", "liga o bloom (brilho que vaza das luzes fortes)")},
                               {"bloom_intensity", Prop("number", "intensidade do bloom")},
                               {"bloom_threshold", Prop("number", "brilho a partir do qual há bloom (1 = acima do branco)")},
                               {"fxaa", Prop("boolean", "liga o FXAA (suaviza o serrilhado)")}}),
        .run = [&ctx](const Json& args) {
            Environment env = ctx.scene.environment;  // só aplica se tudo for válido
            auto number = [&](const char* key, float& value) {
                if (!args.contains(key)) return;
                if (!args[key].is_number()) throw ToolError(std::format("{} precisa ser um número", key));
                value = args[key].get<float>();
            };
            auto boolean = [&](const char* key, bool& value) {
                if (!args.contains(key)) return;
                if (!args[key].is_boolean()) throw ToolError(std::format("{} precisa ser true ou false", key));
                value = args[key].get<bool>();
            };
            if (args.contains("sky_color")) env.skyColor = ReadVec3(args["sky_color"], "sky_color");
            if (args.contains("sky_mode") && !(args["sky_mode"].is_string() &&
                                               ParseSkyMode(args["sky_mode"].get<std::string>(), env.skyMode))) {
                throw ToolError("sky_mode: use \"Color\" ou \"Texture\"");
            }
            if (args.contains("sky_texture")) {
                if (!args["sky_texture"].is_string()) throw ToolError("sky_texture precisa ser o caminho de um .hdr");
                const std::string key = args["sky_texture"].get<std::string>();
                if (key.empty()) {
                    env.skyTexture = nullptr;
                } else {
                    std::string error;
                    env.skyTexture = ctx.assets.GetEnvironmentMap(key, &error);
                    if (!env.skyTexture) throw ToolError(error);
                    if (!args.contains("sky_mode")) env.skyMode = SkyMode::Texture;
                }
            }
            number("sky_intensity", env.skyIntensity);
            number("sky_rotation", env.skyRotation);
            number("sky_light", env.skyLight);
            number("fog_density", env.fogDensity);
            if (args.contains("ambient")) env.ambient = ReadVec3(args["ambient"], "ambient");
            number("shadow_distance", env.shadowDistance);
            number("exposure", env.exposure);
            if (args.contains("tone_mapper") && !(args["tone_mapper"].is_string() &&
                                                  ParseToneMapper(args["tone_mapper"].get<std::string>(), env.toneMapper))) {
                throw ToolError("tone_mapper: use \"ACES\", \"Reinhard\" ou \"None\"");
            }
            boolean("bloom", env.bloom);
            number("bloom_intensity", env.bloomIntensity);
            number("bloom_threshold", env.bloomThreshold);
            boolean("fxaa", env.fxaa);
            ctx.scene.environment = std::move(env);
            return ToolResult::Data(EnvironmentJson(ctx));
        },
    });
}

} // namespace

void AddSceneTools(Protocol& protocol, EditorContext& ctx) {
    AddEntityTools(protocol, ctx);
    AddEntityEditTools(protocol, ctx);
    AddComponentTools(protocol, ctx);
    AddEnvironmentTools(protocol, ctx);
}

} // namespace editor::mcp
