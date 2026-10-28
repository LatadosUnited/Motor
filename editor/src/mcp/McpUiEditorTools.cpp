// Ferramentas MCP da janela Editor de UI (uidoc/UiDocEditor.h): abrir um .mui, ver a árvore e as regras, capturar a
// prévia numa resolução, selecionar (também pelo ponto, com o mesmo hit test do clique), mexer nos elementos (paleta,
// mover, redimensionar pelas alças, propriedades), nas regras das folhas e no desfazer, e gravar. As mesmas operações
// da janela: cada mudança vira um passo do desfazer dela.

#include "McpController.h"

#include "ImageEncode.h"
#include "McpSceneTools.h"
#include "uidoc/UiDocConvert.h"
#include "uidoc/UiDocEditor.h"
#include "uidoc/UiDocFileOps.h"

#include <motor/core/Paths.h>
#include <motor/renderer/Framebuffer.h>
#include <motor/ui/UiDocument.h>

#include <algorithm>
#include <format>

namespace editor {

using namespace motor;
using mcp::Json;
using mcp::Prop;
using mcp::Schema;
using mcp::ToolError;
using mcp::ToolResult;
namespace fs = std::filesystem;

namespace {

Json PathJson(const UiPath& path) {
    Json out = Json::array();
    for (const int i : path) out.push_back(i);
    return out;
}

// Elemento por nome ("jogar") ou caminho ([0, 2]). `allowRoot`: ausente, null, "" ou [] = a raiz (vazio).
UiPath PathArg(const UiDocEditor& editor, const Json& args, const char* key, bool allowRoot) {
    const auto it = args.find(key);
    if (it == args.end() || it->is_null() || (it->is_string() && it->get<std::string>().empty()) ||
        (it->is_array() && it->empty())) {
        if (allowRoot) return {};
        throw ToolError(std::format("informe '{}' (nome do elemento ou caminho [i, j...])", key));
    }
    const auto& elements = editor.State().doc.elements;
    UiPath path;
    if (it->is_string()) {
        if (!FindUiElementPath(elements, it->get<std::string>(), path)) {
            throw ToolError(std::format("nenhum elemento com o nome '{}'", it->get<std::string>()));
        }
        return path;
    }
    if (it->is_array()) {
        for (const Json& v : *it) {
            if (!v.is_number_integer()) throw ToolError(std::format("'{}': caminho deve ser [i, j...] de inteiros", key));
            path.push_back(v.get<int>());
        }
        if (!UiElementAt(elements, path)) throw ToolError(std::format("'{}': nenhum elemento no caminho {}", key, it->dump()));
        return path;
    }
    throw ToolError(std::format("'{}': use o nome do elemento ou o caminho [i, j...]", key));
}

UiElementType TypeArg(const Json& args) {
    UiElementType type;
    if (ParseUiElementType(args.value("type", std::string()), type)) return type;
    throw ToolError("type: use Panel, Text, Image, Button, ProgressBar, Slider, Toggle ou TextField");
}

Json DeclarationsJson(const UiStyleDeclarations& declarations) {
    Json out = Json::object();
    for (const UiStyleDeclaration& d : declarations.items) {
        out[UiStylePropertyName(d.property)] = Json::parse(UiStyleValueToJson(d.property, d.value), nullptr, false);
    }
    return out;
}

Json ElementJson(const UiDocEditor& editor, const UiElement& e, UiPath& path) {
    Json j = UiElementSummaryJson(e, path);
    j.erase("children");  // refeitos abaixo, com as caixas
    UiBox box;
    if (editor.BoxOf(path, box)) j["box"] = {box.min.x, box.min.y, box.max.x, box.max.y};
    if (!e.children.empty()) {
        Json children = Json::array();
        for (size_t i = 0; i < e.children.size(); ++i) {
            path.push_back(static_cast<int>(i));
            children.push_back(ElementJson(editor, e.children[i], path));
            path.pop_back();
        }
        j["children"] = std::move(children);
    }
    return j;
}

void RequireOpen(const UiDocEditor& editor) {
    if (!editor.IsOpen()) throw ToolError("nenhum documento aberto no Editor de UI (use ui_editor_open)");
}

int HandlesArg(const Json& args) {
    const Json h = args.value("handles", Json());
    std::vector<std::string> names;
    if (h.is_string()) {
        names.push_back(h.get<std::string>());
    } else if (h.is_array()) {
        for (const Json& v : h) {
            if (v.is_string()) names.push_back(v.get<std::string>());
        }
    }
    int sides = 0;
    for (const std::string& n : names) {
        if (n == "left") sides |= UiDocEditor::kLeft;
        else if (n == "right") sides |= UiDocEditor::kRight;
        else if (n == "top") sides |= UiDocEditor::kTop;
        else if (n == "bottom") sides |= UiDocEditor::kBottom;
        else throw ToolError("handles: use left, right, top e bottom (lista: [\"right\", \"bottom\"] = canto)");
    }
    if (sides == 0) throw ToolError("informe handles: os lados que a alça move (left, right, top, bottom)");
    return sides;
}

size_t SheetArg(const UiDocEditor& editor, const Json& args) {
    const auto& sheets = editor.State().sheets;
    const Json s = args.value("sheet", Json(0));
    if (s.is_number_integer()) {
        const int i = s.get<int>();
        if (i < 0 || i >= static_cast<int>(sheets.size())) throw ToolError(std::format("sheet: o documento tem {} folha(s)", sheets.size()));
        return static_cast<size_t>(i);
    }
    if (s.is_string()) {
        for (size_t i = 0; i < sheets.size(); ++i) {
            if (sheets[i].key == s.get<std::string>()) return i;
        }
    }
    throw ToolError("sheet: índice (0...) ou caminho da folha (\"Assets/UI/Tema.mstyle\")");
}

} // namespace

void McpController::AddUiEditorTools() {
    // --- Arquivos direto, sem a janela (Tarefa 23) ---
    m_protocol.AddTool({
        .name = "ui_document_edit",
        .description =
            "Cria ou muda um documento de UI (.mui) direto no arquivo, sem abrir o Editor de UI. create = true cria se "
            "não existe (o .meta vem junto). ops: lista aplicada em ordem; elemento = nome ou caminho [i, j...]: "
            "{action: add, type: Panel/Text/Image/Button/ProgressBar/Slider/Toggle/TextField, parent, index, ...campos}, "
            "{action: set, element, ...campos}, {action: move, element, parent, index}, {action: delete, element}, "
            "{action: duplicate, element}, {action: add_sheet, path: \"Assets/UI/Tema.mstyle\"}, {action: remove_sheet, "
            "path}. Campos: name, classes, text, visible, interactable, image, value, min, max, step, checked, "
            "placeholder, maxLength, follow, followOffset e style {propriedade: valor JSON como no .mui, null tira}. "
            "Grava (tudo ou nada) e as entidades da cena que usam o arquivo releem (também no Play). Devolve a árvore.",
        .inputSchema = Schema({{"path", Prop("string", "caminho do .mui relativo ao projeto (\"Assets/UI/Menu.mui\")")},
                               {"create", Prop("boolean", "cria o arquivo se não existir (documento vazio)")},
                               {"ops", Prop("array", "operações (ver a descrição); vazio = só lê")}},
                              {"path"}),
        .run = [this](const Json& args) {
            const fs::path file = ProjectPath(args.value("path", Json()));
            if (file.extension() != ".mui") throw ToolError("o arquivo precisa ser .mui");
            UiDocumentData doc;
            std::string error;
            std::error_code ec;
            const bool exists = fs::is_regular_file(file, ec);
            if (exists) {
                if (!LoadUiDocument(file, doc, &error)) throw ToolError("não leu: " + error);
            } else if (!args.value("create", false)) {
                throw ToolError("o arquivo não existe (use create = true para criar)");
            } else {
                doc.styled = true;
            }
            const Json ops = args.value("ops", Json::array());
            if (!ApplyUiDocumentOps(doc, ops, &error)) throw ToolError(error + " (nada foi gravado)");
            if (!exists || !ops.empty()) {
                fs::create_directories(file.parent_path(), ec);
                if (!SaveUiDocument(file, doc, &error)) throw ToolError("não gravou: " + error);
                m_host.ctx.assets.Database().GuidFor(file);  // o .meta
                m_host.uiEditor.NotifyFilesSaved();
            }
            Json elements = Json::array();
            for (size_t i = 0; i < doc.elements.size(); ++i) {
                UiPath path{static_cast<int>(i)};
                elements.push_back(UiElementSummaryJson(doc.elements[i], path));
            }
            Json styles = Json::array();
            for (const UiFont& s : doc.styles) styles.push_back(s.path.empty() ? s.guid : s.path);
            return ToolResult::Data({{"file", m_host.ctx.assets.KeyForFile(file)},
                                     {"created", !exists},
                                     {"styles", std::move(styles)},
                                     {"elements", std::move(elements)}});
        },
    });

    m_protocol.AddTool({
        .name = "ui_style_edit",
        .description = "Cria ou muda uma folha de estilo (.mstyle) direto no arquivo, sem o Editor de UI. create = true "
                       "cria se não existe. ops: {action: add_rule, selector: \"Button:focus\", set: {propriedade: valor "
                       "JSON}}, {action: set_rule, rule: índice ou seletor, selector (novo), set: {... null tira}}, "
                       "{action: delete_rule, rule}. Grava (tudo ou nada); os documentos que usam a folha releem.",
        .inputSchema = Schema({{"path", Prop("string", "caminho do .mstyle relativo ao projeto")},
                               {"create", Prop("boolean", "cria o arquivo se não existir (folha vazia)")},
                               {"ops", Prop("array", "operações (ver a descrição); vazio = só lê")}},
                              {"path"}),
        .run = [this](const Json& args) {
            const fs::path file = ProjectPath(args.value("path", Json()));
            if (file.extension() != ".mstyle") throw ToolError("o arquivo precisa ser .mstyle");
            UiStyleSheet sheet;
            std::string error;
            std::error_code ec;
            const bool exists = fs::is_regular_file(file, ec);
            if (exists) {
                if (!LoadUiStyleSheet(file, sheet, &error)) throw ToolError("não leu: " + error);
            } else if (!args.value("create", false)) {
                throw ToolError("o arquivo não existe (use create = true para criar)");
            }
            const Json ops = args.value("ops", Json::array());
            if (!ApplyUiStyleOps(sheet, ops, &error)) throw ToolError(error + " (nada foi gravado)");
            if (!exists || !ops.empty()) {
                fs::create_directories(file.parent_path(), ec);
                if (!SaveUiStyleSheet(file, sheet, &error)) throw ToolError("não gravou: " + error);
                m_host.ctx.assets.Database().GuidFor(file);
                m_host.uiEditor.NotifyFilesSaved();
            }
            Json rules = Json::array();
            for (const UiStyleRule& r : sheet.rules) {
                rules.push_back({{"selector", r.selector}, {"declarations", DeclarationsJson(r.declarations)}});
            }
            return ToolResult::Data(
                {{"file", m_host.ctx.assets.KeyForFile(file)}, {"created", !exists}, {"rules", std::move(rules)}});
        },
    });

    m_protocol.AddTool({
        .name = "ui_convert_entities",
        .description = "Converte a UI por entidades (UiRect, UiText, UiImage, UiButton) de uma entidade num documento de UI "
                       "(.mui), como \"Converter UI em documento\" da Hierarquia: o arquivo vai para Assets/UI/<nome>.mui "
                       "(ou path), as entidades de UI saem e uma entidade com UI: Documento fica no lugar (a própria, se ela "
                       "não for de UI). As caixas na tela ficam as mesmas. Desfaz com a ferramenta undo.",
        .inputSchema = Schema({{"entity", {{"type", {"integer", "string"}}, {"description", "id ou nome da entidade"}}},
                               {"path", Prop("string", "caminho do .mui (padrão Assets/UI/<nome>.mui)")}},
                              {"entity"}),
        .run = [this](const Json& args) {
            const Entity& e = mcp::ResolveEntity(m_host.ctx, args);
            UiDocConvertResult result;
            std::string error;
            if (!ConvertUiToDocument(m_host.ctx, e.id, args.value("path", std::string()), result, &error)) {
                throw ToolError(error);
            }
            return ToolResult::Data({{"entity", result.entity}, {"file", result.key}, {"elements", result.elements}});
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_open",
        .description = "Abre um documento de UI (.mui) na janela Editor de UI (como o duplo clique no painel Projeto) e "
                       "mostra a janela. Com alterações não salvas de outro documento, falha (salve com ui_editor_history "
                       "ou passe discard = true).",
        .inputSchema = Schema({{"path", Prop("string", "caminho do .mui, relativo ao projeto (\"Assets/UI/Menu.mui\")")},
                               {"discard", Prop("boolean", "descarta as alterações não salvas do documento aberto")}},
                              {"path"}),
        .run = [this](const Json& args) {
            UiDocEditor& editor = m_host.uiEditor;
            const fs::path file = ProjectPath(args.value("path", Json()));
            if (editor.HasUnsavedChanges() && !args.value("discard", false) && file.lexically_normal() != editor.File()) {
                throw ToolError("o documento aberto tem alterações não salvas (salve ou use discard = true)");
            }
            std::string error;
            if (!editor.Open(file, &error)) throw ToolError("não abriu: " + error);
            editor.Show();
            return ToolResult::Text(std::format("{} aberto no Editor de UI ({} folha(s) de estilo)", editor.Key(),
                                                editor.State().sheets.size()));
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_state",
        .description = "Estado do Editor de UI: arquivo, não salvo, desfazer, resolução da prévia, o selecionado, a árvore "
                       "(path, tipo, nome, classes, texto, declarações de estilo do elemento e a caixa na prévia em pixels "
                       "[x0, y0, x1, y1]) e as folhas com as regras (seletor e declarações).",
        .inputSchema = Schema(),
        .readOnly = true,
        .run = [this](const Json&) {
            UiDocEditor& editor = m_host.uiEditor;
            RequireOpen(editor);
            editor.RenderPreview();  // caixas do layout na resolução de agora
            const UiEditState& state = editor.State();
            Json elements = Json::array();
            UiPath path;
            for (size_t i = 0; i < state.doc.elements.size(); ++i) {
                path = {static_cast<int>(i)};
                elements.push_back(ElementJson(editor, state.doc.elements[i], path));
            }
            Json sheets = Json::array();
            for (const UiEditSheet& s : state.sheets) {
                Json rules = Json::array();
                for (const UiStyleRule& r : s.sheet.rules) {
                    rules.push_back({{"selector", r.selector}, {"declarations", DeclarationsJson(r.declarations)}});
                }
                sheets.push_back({{"path", s.key}, {"opened", !s.file.empty()}, {"rules", std::move(rules)}});
            }
            const glm::ivec2 res = editor.GetResolution();
            const UiElement* selected = UiElementAt(state.doc.elements, state.selected);
            return ToolResult::Data({{"file", editor.Key()},
                                     {"unsaved", editor.HasUnsavedChanges()},
                                     {"can_undo", editor.CanUndo()},
                                     {"can_redo", editor.CanRedo()},
                                     {"resolution", {res.x, res.y}},
                                     {"ui_scale", UiScale(glm::vec2(res))},
                                     {"selected", selected ? Json{{"path", PathJson(state.selected)}, {"name", selected->name}}
                                                           : Json()},
                                     {"elements", std::move(elements)},
                                     {"sheets", std::move(sheets)}});
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_capture",
        .description = "Imagem (PNG) da prévia do Editor de UI desenhada agora pelo mesmo UiRenderer do jogo, na resolução "
                       "da prévia (resolution muda a resolução, como o seletor da janela: [1920, 1080], [2560, 1080], "
                       "[1440, 1080], [1080, 1920]...). A imagem é reduzida até max_width x max_height.",
        .inputSchema = Schema({{"resolution", Prop("array", "[largura, altura] da prévia (fica escolhida na janela)")},
                               {"max_width", Prop("integer", "largura máxima em pixels (padrão 960)")},
                               {"max_height", Prop("integer", "altura máxima em pixels (padrão 960)")}}),
        .run = [this](const Json& args) {
            UiDocEditor& editor = m_host.uiEditor;
            RequireOpen(editor);
            if (const auto it = args.find("resolution"); it != args.end()) {
                if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
                    throw ToolError("resolution: [largura, altura]");
                }
                editor.SetResolution((*it)[0].get<int>(), (*it)[1].get<int>());
            }
            const int maxW = std::clamp<int>(args.value("max_width", 960), 64, 4096);
            const int maxH = std::clamp<int>(args.value("max_height", 960), 64, 4096);
            if (!editor.RenderPreview()) throw ToolError("a prévia não foi desenhada");
            const Framebuffer& fb = *editor.Preview();
            const mcp::Image full{fb.Width(), fb.Height(), fb.ReadPixels()};
            const mcp::Image image = mcp::Downscale(full, maxW, maxH);
            ToolResult r = ToolResult::Text(std::format("prévia {}x{} (imagem {}x{}) de {}", full.width, full.height,
                                                        image.width, image.height, editor.Key()));
            r.AddImage(mcp::Base64(mcp::EncodePng(image)), "image/png");
            return r;
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_select",
        .description = "Seleciona um elemento no Editor de UI pelo nome ou caminho (element), ou pelo ponto da prévia em "
                       "pixels da resolução (x, y), com o mesmo hit test do clique na janela. Sem nada, limpa a seleção.",
        .inputSchema = Schema({{"element", {{"type", {"string", "array"}}, {"description", "nome ou caminho [i, j...]"}}},
                               {"x", Prop("number", "ponto na prévia (pixels)")},
                               {"y", Prop("number", "ponto na prévia (pixels)")}}),
        .run = [this](const Json& args) {
            UiDocEditor& editor = m_host.uiEditor;
            RequireOpen(editor);
            UiPath path;
            if (args.contains("x") || args.contains("y")) {
                editor.RenderPreview();
                const auto hit = editor.HitTest({args.value("x", 0.0f), args.value("y", 0.0f)});
                if (hit) path = *hit;
            } else {
                path = PathArg(editor, args, "element", true);
            }
            editor.Select(path);
            const UiElement* e = UiElementAt(editor.State().doc.elements, path);
            return ToolResult::Data({{"selected", e ? Json{{"path", PathJson(path)}, {"name", e->name},
                                                           {"type", UiElementTypeName(e->type)}}
                                                    : Json()}});
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_element",
        .description =
            "Muda os elementos do documento aberto no Editor de UI (cada chamada = um passo de desfazer da janela). "
            "action: add (type Panel/Text/Image/Button, como a paleta; parent e index, padrão no fim da raiz), move "
            "(element para parent na posição index, contada sem o elemento), delete, duplicate, resize (arrasta as "
            "alças: handles [\"right\", \"bottom\"...] por dx, dy pixels da prévia; grava width/height em pixels e, "
            "por âncora, a posição), set (name, classes [lista ou texto], text, visible, interactable, image "
            "\"Assets/...\" ou \"\", value, min, max, step, checked, placeholder, maxLength, follow, followOffset e "
            "style {propriedade: valor JSON como no .mui, null tira a declaração}).",
        .inputSchema = Schema(
            {{"action", {{"type", "string"}, {"enum", {"add", "move", "delete", "duplicate", "resize", "set"}}}},
             {"element", {{"type", {"string", "array"}}, {"description", "nome ou caminho [i, j...]"}}},
             {"type", Prop("string", "add: Panel, Text, Image, Button, ProgressBar, Slider, Toggle ou TextField")},
             {"parent", {{"type", {"string", "array", "null"}}, {"description", "add/move: pai (nome ou caminho); vazio = raiz"}}},
             {"index", Prop("integer", "add/move: posição entre os irmãos (padrão: no fim)")},
             {"handles", Prop("array", "resize: lados que a alça move: left, right, top, bottom")},
             {"dx", Prop("number", "resize: pixels da prévia")},
             {"dy", Prop("number", "resize: pixels da prévia")},
             {"name", Prop("string", "set: nome")},
             {"classes", {{"type", {"array", "string"}}, {"description", "set: classes"}}},
             {"text", Prop("string", "set: texto")},
             {"visible", Prop("boolean", "set")},
             {"interactable", Prop("boolean", "set")},
             {"image", Prop("string", "set: textura do projeto (\"\" = cor sólida)")},
             {"value", Prop("number", "set: valor da barra ou do slider")},
             {"min", Prop("number", "set")},
             {"max", Prop("number", "set")},
             {"step", Prop("number", "set: passo do slider (0 = contínuo)")},
             {"checked", Prop("boolean", "set: caixa de seleção marcada")},
             {"placeholder", Prop("string", "set: texto do campo vazio")},
             {"maxLength", Prop("integer", "set: caracteres no máximo do campo (0 = sem limite)")},
             {"follow", Prop("string", "set: nome da entidade 3D que o elemento (Absolute) segue na tela")},
             {"followOffset", Prop("array", "set: [x, y, z] metros somados à posição da entidade")},
             {"style", Prop("object", "set: {propriedade: valor JSON ou null}")}},
            {"action"}),
        .run = [this](const Json& args) {
            UiDocEditor& editor = m_host.uiEditor;
            RequireOpen(editor);
            const std::string action = args.value("action", std::string());
            const int index = args.value("index", -1);
            UiPath result;
            if (action == "add") {
                result = editor.AddElement(TypeArg(args), PathArg(editor, args, "parent", true), index);
            } else if (action == "move") {
                result = editor.MoveElement(PathArg(editor, args, "element", false), PathArg(editor, args, "parent", true), index);
                if (result.empty()) throw ToolError("não dá para mover um elemento para dentro dele mesmo");
            } else if (action == "delete") {
                editor.DeleteElement(PathArg(editor, args, "element", false));
            } else if (action == "duplicate") {
                result = editor.DuplicateElement(PathArg(editor, args, "element", false));
            } else if (action == "resize") {
                result = PathArg(editor, args, "element", false);
                editor.RenderPreview();
                if (!editor.ResizeElement(result, HandlesArg(args), {args.value("dx", 0.0f), args.value("dy", 0.0f)})) {
                    throw ToolError("o elemento não está visível na prévia (sem caixa para redimensionar)");
                }
            } else if (action == "set") {
                result = PathArg(editor, args, "element", false);
                UiElement& e = *UiElementAt(editor.Edit().doc.elements, result);
                bool assets = false;
                std::string error;
                const bool ok = UiElementFieldsFromJson(e, args, &assets, &error);
                editor.Changed(false, assets);  // o que já entrou antes do erro também vale (e entra no desfazer)
                if (!ok) {
                    editor.Commit();
                    throw ToolError(error);
                }
                editor.Commit();
            } else {
                throw ToolError("action: add, move, delete, duplicate, resize ou set");
            }
            editor.RenderPreview();
            const UiElement* e = UiElementAt(editor.State().doc.elements, result);
            Json out{{"action", action}, {"unsaved", editor.HasUnsavedChanges()}};
            if (e) {
                UiPath p = result;
                out["element"] = ElementJson(editor, *e, p);
            }
            return ToolResult::Data(out);
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_style",
        .description = "Muda as regras de uma folha .mstyle do documento aberto no Editor de UI (a prévia mostra na hora; "
                       "ui_editor_history save grava). sheet: índice ou caminho da folha. action: add_rule (selector), "
                       "delete_rule (rule), set_rule (rule; selector novo e/ou set {propriedade: valor JSON ou null para "
                       "tirar}).",
        .inputSchema = Schema({{"action", {{"type", "string"}, {"enum", {"add_rule", "delete_rule", "set_rule"}}}},
                               {"sheet", {{"type", {"integer", "string"}}, {"description", "índice (padrão 0) ou caminho"}}},
                               {"rule", Prop("integer", "índice da regra na folha")},
                               {"selector", Prop("string", "seletor (\"Button.menu:hover\")")},
                               {"set", Prop("object", "{propriedade: valor JSON ou null}")}},
                              {"action"}),
        .run = [this](const Json& args) {
            UiDocEditor& editor = m_host.uiEditor;
            RequireOpen(editor);
            const size_t si = SheetArg(editor, args);
            UiEditSheet& sheet = editor.Edit().sheets[si];
            if (sheet.file.empty()) throw ToolError("a folha " + sheet.key + " não abriu (só leitura)");
            auto& rules = sheet.sheet.rules;
            const std::string action = args.value("action", std::string());
            int ri = args.value("rule", -1);
            std::string error;
            if (action == "add_rule") {
                UiStyleRule rule;
                if (!SetUiRuleSelector(rule, args.value("selector", std::string("*")), &error)) throw ToolError("selector: " + error);
                rules.push_back(std::move(rule));
                ri = static_cast<int>(rules.size()) - 1;
            } else if (ri < 0 || ri >= static_cast<int>(rules.size())) {
                throw ToolError(std::format("rule: a folha tem {} regra(s)", rules.size()));
            }
            if (action == "delete_rule") {
                rules.erase(rules.begin() + ri);
            } else if (action == "set_rule" || action == "add_rule") {
                UiStyleRule& rule = rules[static_cast<size_t>(ri)];
                if (action == "set_rule" && args.contains("selector") &&
                    !SetUiRuleSelector(rule, args["selector"].get<std::string>(), &error)) {
                    throw ToolError("selector: " + error);
                }
                if (const auto it = args.find("set"); it != args.end()) {
                    if (!it->is_object()) throw ToolError("set: objeto {propriedade: valor}");
                    for (const auto& [prop, value] : it->items()) {
                        if (FindUiStyleProperty(prop) < 0) throw ToolError("set: propriedade desconhecida '" + prop + "'");
                        if (value.is_null()) {
                            rule.declarations.Remove(prop);
                        } else if (!rule.declarations.Set(prop, value.dump())) {
                            throw ToolError(std::format("set: valor inválido para '{}': {}", prop, value.dump()));
                        }
                    }
                }
            } else if (action != "delete_rule") {
                throw ToolError("action: add_rule, delete_rule ou set_rule");
            }
            editor.Changed(true);
            editor.Commit();
            Json out = Json::array();
            for (const UiStyleRule& r : editor.State().sheets[si].sheet.rules) {
                out.push_back({{"selector", r.selector}, {"declarations", DeclarationsJson(r.declarations)}});
            }
            return ToolResult::Data({{"sheet", editor.State().sheets[si].key}, {"rules", std::move(out)},
                                     {"unsaved", editor.HasUnsavedChanges()}});
        },
    });

    m_protocol.AddTool({
        .name = "ui_editor_history",
        .description = "Desfazer e refazer da janela Editor de UI (o mesmo Ctrl+Z/Ctrl+Y com ela em foco; separado do "
                       "desfazer da cena) e gravar o .mui e as folhas mudadas (save, como Ctrl+S nela). As entidades da "
                       "cena que usam o documento releem o arquivo gravado (também no Play).",
        .inputSchema = Schema({{"action", {{"type", "string"}, {"enum", {"undo", "redo", "save"}}}}}, {"action"}),
        .run = [this](const Json& args) {
            UiDocEditor& editor = m_host.uiEditor;
            RequireOpen(editor);
            const std::string action = args.value("action", std::string());
            bool done = false;
            if (action == "undo") {
                done = editor.Undo();
            } else if (action == "redo") {
                done = editor.Redo();
            } else if (action == "save") {
                std::string error;
                if (!editor.Save(&error)) throw ToolError("não gravou: " + error);
                done = true;
            } else {
                throw ToolError("action: undo, redo ou save");
            }
            return ToolResult::Data({{"action", action}, {"done", done}, {"unsaved", editor.HasUnsavedChanges()},
                                     {"can_undo", editor.CanUndo()}, {"can_redo", editor.CanRedo()}});
        },
    });
}

} // namespace editor
