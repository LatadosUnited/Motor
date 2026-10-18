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
