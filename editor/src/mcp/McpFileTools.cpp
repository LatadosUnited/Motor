// Ferramentas MCP de arquivos: cenas, projeto, assets, prefabs e materiais.

#include "McpController.h"

#include "export/ExportController.h"
#include "play/PlayController.h"
#include "document/SceneDocument.h"
#include "viewport/SceneViewport.h"
#include "scripts/ScriptController.h"
#include "ImageEncode.h"
#include "McpSceneTools.h"

#include <motor/core/Paths.h>
#include <motor/core/Version.h>
#include <motor/renderer/Framebuffer.h>
#include <motor/audio/AudioSystem.h>
#include <motor/ui/UiSystem.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace editor {

using namespace motor;
using mcp::Json;
using mcp::Prop;
using mcp::Schema;
using mcp::Tool;
using mcp::ToolError;
using mcp::ToolResult;
namespace fs = std::filesystem;

namespace {
bool DiscardArg(const Json& args) {
    return args.value("discard_changes", false);
}
} // namespace

// --- Arquivos: cenas e projeto ----------------------------------------------------------------------

void McpController::AddSceneFileTools() {
    const Json discardProp = Prop("boolean", "descartar as alterações não salvas da cena aberta (padrão: false)");

    m_protocol.AddTool({
        .name = "scene_save",
        .description = "Salva a cena aberta. Sem 'path', no arquivo atual; com 'path' (relativo ao projeto, ex.: "
                       "Assets/Cenas/Fase1.mscene), salva como esse arquivo.",
        .inputSchema = Schema({{"path", Prop("string", "arquivo .mscene (opcional)")}}),
        .run = [this](const Json& args) {
            const fs::path path = args.contains("path") ? ProjectPath(args["path"]) : fs::path{};
            std::string error;
            if (!m_host.document.SaveSceneTo(path, error)) throw ToolError(error);
            return ToolResult::Text("cena salva em " + ToUtf8(m_host.document.ScenePath()));
        },
    });

    m_protocol.AddTool({
        .name = "scene_open",
        .description = "Abre uma cena do projeto (para o Play se estiver jogando). Recusa se a cena aberta tiver "
                       "alterações não salvas, a menos que discard_changes seja true.",
        .inputSchema = Schema({{"path", Prop("string", "arquivo .mscene, relativo ao projeto")},
                               {"discard_changes", discardProp}},
                              {"path"}),
        .run = [this](const Json& args) {
            std::string error;
            if (!m_host.document.OpenSceneDirect(ProjectPath(args["path"]), DiscardArg(args), error)) throw ToolError(error);
            return ToolResult::Text(std::format("cena aberta: {} ({} entidades)", ToUtf8(m_host.document.ScenePath()),
                                                m_host.ctx.scene.Entities().size()));
        },
    });

    m_protocol.AddTool({
        .name = "scene_new",
        .description = "Começa uma cena nova e vazia (sem arquivo até salvar).",
        .inputSchema = Schema({{"discard_changes", discardProp}}),
        .run = [this](const Json& args) {
            std::string error;
            if (!m_host.document.NewSceneDirect(DiscardArg(args), error)) throw ToolError(error);
            return ToolResult::Text("cena nova aberta");
        },
    });

    m_protocol.AddTool({
        .name = "project_create",
        .description = "Cria um projeto novo, como Arquivo > Novo projeto: pasta <parent>/<name> com Assets/, Scripts/ "
                       "(script de exemplo) e a cena inicial, e abre no editor (fecha o projeto aberto). Recusa se a "
                       "cena aberta tiver alterações não salvas, a menos que discard_changes seja true.",
        .inputSchema = Schema({{"parent", Prop("string", "pasta onde o projeto será criado (caminho absoluto)")},
                               {"name", Prop("string", "nome do projeto (também o nome da pasta)")},
                               {"discard_changes", discardProp}},
                              {"parent", "name"}),
        .run = [this](const Json& args) {
            const fs::path parent = PathFromUtf8(args["parent"].get<std::string>());
            if (!parent.is_absolute()) throw ToolError("'parent' precisa ser um caminho absoluto");
            m_host.document.StopPlay();
            if (!DiscardArg(args) && m_host.document.HasUnsavedChanges()) {
                throw ToolError("a cena aberta tem alterações não salvas (salve com scene_save ou use discard_changes)");
            }
            std::string error;
            if (!m_host.document.CreateProjectAt(parent, args["name"].get<std::string>(), error)) throw ToolError(error);
            const Project* project = m_host.document.GetProject();
            return ToolResult::Text(std::format("projeto criado e aberto: {} (cena {})", ToUtf8(project->RootDir()),
                                                ToUtf8(m_host.document.ScenePath())));
        },
    });

    m_protocol.AddTool({
        .name = "physics_layers",
        .description = "Lê ou muda as camadas de colisão do projeto (como Arquivo > Camadas de colisão): nomes das 16 "
                       "camadas e quem se choca com quem. Sem argumentos, só lê. Collider e CharacterController "
                       "escolhem a camada pelo campo 'layer' (número). Grava o .mproject; vale no próximo Play.",
        .inputSchema = Schema({{"names", Prop("object", "{\"1\": \"Jogador\", \"2\": \"Inimigo\"} (texto vazio tira o nome)")},
                               {"collide", Prop("array", "[{\"a\": \"Jogador\", \"b\": \"Inimigo\", \"value\": false}] "
                                                         "(camada pelo nome ou pelo número)")}}),
        .run = [this](const Json& args) {
            const Project* project = m_host.document.GetProject();
            if (!project) throw ToolError("nenhum projeto aberto");
            CollisionLayers layers = project->Layers();
            if (args.contains("names")) {
                if (!args["names"].is_object()) throw ToolError("'names' deve ser um objeto {\"número\": \"nome\"}");
                for (const auto& [key, value] : args["names"].items()) {
                    int index = -1;
                    try {
                        index = std::stoi(key);
                    } catch (...) {
                    }
                    if (index < 0 || index >= CollisionLayers::kCount || !value.is_string()) {
                        throw ToolError(std::format("names: use \"0\" a \"{}\" com um texto", CollisionLayers::kCount - 1));
                    }
                    layers.names[static_cast<size_t>(index)] = value.get<std::string>();
                }
            }
            auto layerArg = [&layers](const Json& v) {
                if (v.is_number_integer() && v.get<int>() >= 0 && v.get<int>() < CollisionLayers::kCount) return v.get<int>();
                if (v.is_string() && layers.Find(v.get<std::string>()) >= 0) return layers.Find(v.get<std::string>());
                throw ToolError("camada desconhecida: " + v.dump());
            };
            if (args.contains("collide")) {
                if (!args["collide"].is_array()) throw ToolError("'collide' deve ser uma lista de {a, b, value}");
                for (const Json& pair : args["collide"]) {
                    if (!pair.is_object() || !pair.contains("a") || !pair.contains("b") || !pair.contains("value") ||
                        !pair["value"].is_boolean()) {
                        throw ToolError("cada item de 'collide' precisa de a, b e value (true ou false)");
                    }
                    layers.SetCollide(layerArg(pair["a"]), layerArg(pair["b"]), pair["value"].get<bool>());
                }
            }
            if (!(layers == project->Layers())) {
                std::string error;
                if (!m_host.document.SetCollisionLayers(layers, error)) throw ToolError(error);
            }
            Json list = Json::array();
            for (int i = 0; i < CollisionLayers::kCount; ++i) {
                if (layers.names[static_cast<size_t>(i)].empty() && i != 0) continue;
                Json with = Json::array();
                for (int j = 0; j < CollisionLayers::kCount; ++j) {
                    if (layers.Collide(i, j) && (j == 0 || !layers.names[static_cast<size_t>(j)].empty())) with.push_back(j);
                }
                list.push_back({{"index", i}, {"name", layers.Label(i)}, {"collides_with", std::move(with)}});
            }
            return ToolResult::Data({{"layers", std::move(list)}});
        },
    });

    m_protocol.AddTool({
        .name = "input_actions",
        .description = "Lê ou muda o mapa de ações do projeto (como Arquivo > Controles): cada ação (\"Pular\", "
                       "\"Horizontal\") com as entradas (key, mouse, gamepad ou axis, e scale -1 para o lado "
                       "negativo de um eixo). Os scripts usam IsActionDown/WasActionPressed/GetAxis. Sem argumentos, só "
                       "lê. actions substitui o mapa inteiro; reset volta ao padrão. Grava o .mproject.",
        .inputSchema = Schema({{"actions", Prop("array", "[{\"name\": \"Pular\", \"bindings\": [{\"key\": \"Space\"}, "
                                                         "{\"gamepad\": \"A\"}]}, {\"name\": \"Horizontal\", "
                                                         "\"bindings\": [{\"key\": \"A\", \"scale\": -1}, "
                                                         "{\"axis\": \"LeftX\"}]}]")},
                               {"reset", Prop("boolean", "true = mapa padrão")}}),
        .run = [this](const Json& args) {
            const Project* project = m_host.document.GetProject();
            if (!project) throw ToolError("nenhum projeto aberto");
            InputActions actions = project->Actions();
            std::string problems;
            if (args.value("reset", false)) actions = InputActions::Defaults();
            if (args.contains("actions")) {
                if (!actions.FromJson(Json{{"actions", args["actions"]}}.dump(), &problems)) throw ToolError(problems);
            }
            if (!(actions == project->Actions())) {
                std::string error;
                if (!m_host.document.SetInputActions(actions, error)) throw ToolError(error);
            }
            Json result = Json::parse(actions.ToJson());
            if (!problems.empty()) result["warnings"] = problems;
            result["gamepad"] = Input::IsGamepadConnected() ? Input::GamepadName() : std::string();
            return ToolResult::Data(result);
        },
    });

    m_protocol.AddTool({
        .name = "game_settings",
        .description = "Lê ou muda as configurações do jogo (como Arquivo > Configurações do jogo): identidade do jogo "
                       "exportado (nome do .exe, versão, ícone) e as opções com que ele abre da primeira vez. Sem "
                       "argumentos, só lê. Grava o .mproject.",
        .inputSchema = Schema({{"executable", Prop("string", "nome do .exe sem a extensão (vazio = o nome do projeto)")},
                               {"version", Prop("string", "versão do jogo: números separados por ponto (1.0.0)")},
                               {"icon", Prop("string", "ícone .png (até 256x256) ou .ico, relativo ao projeto "
                                                       "(\"Assets/icone.png\"); vazio = sem ícone")},
                               {"loading_scene", Prop("string", "cena da tela de carregamento, relativa ao projeto; "
                                                                "vazio = a padrão (\"Carregando...\")")},
                               {"fullscreen", Prop("boolean", "tela cheia")},
                               {"width", Prop("integer", "largura (0 = padrão: 1280 em janela, a da tela em tela cheia)")},
                               {"height", Prop("integer", "altura (0 = padrão)")},
                               {"vsync", Prop("boolean", "sincronizar com a tela")},
                               {"msaa", Prop("integer", "amostras de MSAA: 1 (sem), 2, 4 ou 8")},
                               {"shadows", Prop("string", "desligadas, baixas, medias ou altas")}}),
        .run = [this](const Json& args) {
            const Project* project = m_host.document.GetProject();
            if (!project) throw ToolError("nenhum projeto aberto");
            GameIdentity identity = project->Identity();
            if (args.contains("executable")) identity.executable = args["executable"].get<std::string>();
            if (args.contains("version")) identity.version = args["version"].get<std::string>();
            if (args.contains("icon")) identity.icon = args["icon"].get<std::string>();
            if (args.contains("loading_scene")) identity.loadingScene = args["loading_scene"].get<std::string>();
            if (!(identity == project->Identity())) {
                std::string error;
                if (!m_host.document.SetGameIdentity(identity, error)) throw ToolError(error);
            }
            GameOptions options = project->DefaultOptions();
            if (args.contains("fullscreen")) options.fullscreen = args["fullscreen"].get<bool>();
            if (args.contains("width")) options.width = args["width"].get<int>();
            if (args.contains("height")) options.height = args["height"].get<int>();
            if (args.contains("vsync")) options.vsync = args["vsync"].get<bool>();
            if (args.contains("msaa")) options.msaa = args["msaa"].get<int>();
            if (args.contains("shadows")) {
                const std::string name = args["shadows"].get<std::string>();
                bool found = false;
                for (ShadowSetting s : {ShadowSetting::Off, ShadowSetting::Low, ShadowSetting::Medium, ShadowSetting::High}) {
                    if (name == ShadowSettingName(s)) {
                        options.shadows = s;
                        found = true;
                    }
                }
                if (!found) throw ToolError("shadows: use desligadas, baixas, medias ou altas");
            }
            options.Clamp();
            if (!(options == project->DefaultOptions())) {
                std::string error;
                if (!m_host.document.SetGameOptions(options, error)) throw ToolError(error);
            }
            return ToolResult::Data({{"executable", project->ExecutableName() + ".exe"},
                                     {"version", identity.version},
                                     {"icon", identity.icon},
                                     {"loading_scene", identity.loadingScene},
                                     {"fullscreen", options.fullscreen},
                                     {"width", options.width},
                                     {"height", options.height},
                                     {"vsync", options.vsync},
                                     {"msaa", options.msaa},
                                     {"shadows", ShadowSettingName(options.shadows)}});
        },
    });
}

// --- Arquivos: assets, prefabs, modelos, materiais ---------------------------------------------------

void McpController::AddAssetTools() {
    m_protocol.AddTool({
        .name = "assets_list",
        .description = "Arquivos da pasta Assets do projeto com o tipo (Cena, Textura, Malha, Material, Prefab, "
                       "Modelo, Fonte...). Caminhos relativos ao projeto, como as outras ferramentas aceitam.",
        .inputSchema = Schema({{"folder", Prop("string", "subpasta, relativa ao projeto (padrão: Assets)")},
                               {"type", Prop("string", "só deste tipo, ex.: Prefab")}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const Project* project = m_host.document.GetProject();
            if (!project) throw ToolError("nenhum projeto aberto");
            const fs::path root = project->RootDir();
            const fs::path folder = args.contains("folder") ? ProjectPath(args["folder"]) : root / "Assets";
            const std::string wantedType = args.value("type", std::string());
            Json files = Json::array();
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const fs::path& p = it->path();
                const std::string ext = ToUtf8(p.extension());
                if (ext == ".meta" || ext == ".tmp") continue;
                const char* type = AssetTypeName(AssetTypeFromPath(p));
                if (!wantedType.empty() && wantedType != type) continue;
                std::string relative = ToUtf8(p.lexically_relative(root));
                std::replace(relative.begin(), relative.end(), '\\', '/');
                files.push_back({{"path", std::move(relative)}, {"type", type}});
                if (files.size() >= 1000) break;
            }
            if (ec) throw ToolError("pasta não encontrada: " + ToUtf8(folder));
            return ToolResult::Data({{"files", std::move(files)}});
        },
    });

    const Json entityProp = {{"type", {"integer", "string"}}, {"description", "id ou nome da entidade"}};

    m_protocol.AddTool({
        .name = "prefab_instantiate",
        .description = "Cria na cena uma instância de um prefab (.mprefab) ou de um modelo glTF (.gltf/.glb; "
                       "importado se preciso). Fica selecionada. Devolve a raiz criada.",
        .inputSchema = Schema({{"path", Prop("string", "arquivo, relativo ao projeto")}, {"parent", entityProp}},
                              {"path"}),
        .run = [this](const Json& args) {
            EditorContext& ctx = m_host.ctx;
            const fs::path path = ProjectPath(args["path"]);
            const EntityId parent = args.contains("parent") ? mcp::ResolveEntity(ctx, args, "parent").id : kNullEntity;
            const AssetType type = AssetTypeFromPath(path);
            Entity* e = type == AssetType::Model    ? ctx.InstantiateModelFile(path, parent)
                        : type == AssetType::Prefab ? ctx.InstantiatePrefabFile(path, parent)
                                                    : nullptr;
            mcp::ThrowPendingError(ctx);
            if (!e) throw ToolError("não é um prefab nem um modelo glTF: " + ToUtf8(path));
            return ToolResult::Data(mcp::EntityToJson(ctx, *e));
        },
    });

    m_protocol.AddTool({
        .name = "prefab_create",
        .description = "Grava uma entidade (com os filhos) como prefab novo em Assets/Prefabs (ou em 'folder'); a "
                       "entidade vira uma instância dele.",
        .inputSchema = Schema({{"entity", entityProp}, {"folder", Prop("string", "pasta, relativa ao projeto")}},
                              {"entity"}),
        .run = [this](const Json& args) {
            EditorContext& ctx = m_host.ctx;
            const Entity& e = mcp::ResolveEntity(ctx, args);
            const fs::path folder = args.contains("folder") ? ProjectPath(args["folder"]) : fs::path{};
            const bool ok = ctx.CreatePrefabFromEntity(e.id, folder);
            mcp::ThrowPendingError(ctx);
            if (!ok) throw ToolError("não foi possível criar o prefab");
            return ToolResult::Text("prefab criado a partir de " + e.name);
        },
    });

    m_protocol.AddTool({
        .name = "asset_apply",
        .description = "Aplica um arquivo do projeto ao Mesh Renderer de uma entidade: material (.mmat, todos os "
                       "objetos com ele mudam juntos) ou textura (.png/.jpg).",
        .inputSchema = Schema({{"entity", entityProp}, {"path", Prop("string", "arquivo, relativo ao projeto")}},
                              {"entity", "path"}),
        .run = [this](const Json& args) {
            EditorContext& ctx = m_host.ctx;
            Entity& e = mcp::ResolveEntity(ctx, args);
            const fs::path path = ProjectPath(args["path"]);
            const AssetType type = AssetTypeFromPath(path);
            bool ok = false;
            if (type == AssetType::Material) {
                ok = ctx.ApplyMaterialFile(e.id, path);
            } else if (type == AssetType::Texture) {
                ok = ctx.ApplyTextureFile(e.id, path);
            } else {
                throw ToolError("só material (.mmat) ou textura: " + ToUtf8(path));
            }
            mcp::ThrowPendingError(ctx);
            if (!ok) throw ToolError("não foi possível aplicar (a entidade tem Mesh Renderer?)");
            return ToolResult::Data(mcp::EntityToJson(ctx, e));
        },
    });
}

} // namespace editor
