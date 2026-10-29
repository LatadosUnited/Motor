// Documentos de UI, Etapa 6 (scripts, controles e MCP): consultas por seletor, eventos (OnClick, OnChange, OnHover)
// e a limpeza deles ao parar e na recarga dos scripts, os controles (barra, slider, caixa de seleção, campo de texto)
// com o mouse e o teclado simulados no estado interno do Input, a navegação do foco no contexto Menu, o elemento que
// segue uma entidade 3D, o formato .mui versão 4 (e a leitura da 3), a conversão da UI por entidades num documento
// (mesmas caixas) e a edição de .mui/.mstyle por JSON das ferramentas MCP. Sem janela nem OpenGL.
#include "TestUtil.h"

#include "uidoc/UiDocFileOps.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Input.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/ui/Ui.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <string>

using namespace motor;
using namespace editor;
namespace fs = std::filesystem;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const glm::vec2 kScreen{1920.0f, 1080.0f};  // escala 1

bool Near(float a, float b, float eps = 0.01f) { return std::abs(a - b) < eps; }
bool Near(const glm::vec2& a, const glm::vec2& b, float eps = 0.5f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

UiElement Element(UiElementType type, const char* name, glm::vec2 position, glm::vec2 size) {
    UiElement e = UiElement::Make(type);
    e.name = name;
    // Declarado no elemento: o documento tem estilo (folha padrão, :focus), e a cascata refaz os campos.
    e.SetStyleValue("placement", static_cast<int>(UiPlacement::Absolute));
    e.SetStyleValue("anchor", static_cast<int>(UiAnchor::Center));
    e.SetStyleValue("position", position);
    e.SetStyleValue("width", UiLength::Px(size.x));
    e.SetStyleValue("height", UiLength::Px(size.y));
    return e;
}

// Menu de opções: dois botões em cima, slider, caixa de seleção e campo embaixo, uma barra de vida.
UiDocumentData Options() {
    UiDocumentData doc;
    UiElement play = Element(UiElementType::Button, "jogar", {-150, -200}, {250, 80});
    play.classes = {"menu"};
    UiElement quit = Element(UiElementType::Button, "sair", {150, -200}, {250, 80});
    quit.classes = {"menu"};
    UiElement volume = Element(UiElementType::Slider, "volume", {0, -50}, {400, 40});
    volume.value = 0.5f;
    UiElement full = Element(UiElementType::Toggle, "tela_cheia", {0, 50}, {400, 50});
    full.text = "Tela cheia";
    UiElement name = Element(UiElementType::TextField, "nome", {0, 150}, {400, 60});
    name.placeholder = "Seu nome";
    name.maxLength = 8;
    UiElement life = Element(UiElementType::ProgressBar, "vida", {0, 300}, {400, 30});
    life.value = 0.25f;
    doc.elements = {play, quit, volume, full, name, life};
    doc.styled = true;
    ComputeUiStyles(doc);
    return doc;
}

Entity& AddDocument(Scene& scene, const char* name, const UiDocumentData& data) {
    Entity& e = scene.CreateEntity(name);
    e.Add<UiDocument>().document.data = std::make_shared<UiDocumentData>(data);
    return e;
}

void Frame(SceneRuntime& runtime, glm::vec2 mouse, bool down) {
    if (auto* ui = runtime.GetSystem<UiSystem>()) ui->SetPointer({kScreen, mouse, true, down});
    runtime.FixedUpdate(kDt);
    runtime.Update(kDt);
    Input::BeginFrame(kDt);
}

void Click(SceneRuntime& runtime, glm::vec2 at) {
    Frame(runtime, at, true);
    Frame(runtime, at, false);
}

// Tecla apertada num quadro e solta no seguinte (WasKeyPressed vale só no primeiro).
void Press(SceneRuntime& runtime, Key key) {
    Input::SetKeyState(key, true);
    Frame(runtime, {0, 0}, false);
    Input::SetKeyState(key, false);
    Frame(runtime, {0, 0}, false);
}

glm::vec2 Center(const UiSystem& ui, const UiElement* e) {
    UiBox box;
    return e && ui.ScreenRect(*e, box) ? box.Center() : glm::vec2(-1.0f);
}

const UiNode* NodeOf(const std::vector<UiNode>& layout, std::string_view name) {
    for (const UiNode& n : layout) {
        if (n.element && n.element->name == name) return &n;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    SECTION("Consultas: por #nome, .classe, tipo e nome sozinho; seletor inválido acha nada");
    {
        UiDocumentData doc = Options();
        CHECK(doc.Q("#volume") && doc.Q("#volume")->type == UiElementType::Slider);
        CHECK(doc.Q("volume") == doc.Q("#volume"));  // nome sozinho
        CHECK(doc.Query(".menu").size() == 2);
        CHECK(doc.Query("Button").size() == 2 && doc.Query("Button.menu#sair").size() == 1);
        CHECK(doc.Query("Toggle, TextField").size() == 2);
        CHECK(doc.Q("#naoexiste") == nullptr);
        std::vector<UiSelector> parsed;
        std::string error;
        CHECK(!ParseUiQuery("Button:", parsed, &error) && !error.empty());
        CHECK(ParseUiQuery("jogar", parsed, &error) && UiQueryMatches(parsed, *doc.Q("#jogar")));
    }

    SECTION("Controles: valores, fração, passo e texto mostrado");
    {
        UiElement slider = UiElement::Make(UiElementType::Slider);
        slider.minValue = 0.0f;
        slider.maxValue = 10.0f;
        slider.step = 2.0f;
        CHECK(slider.SetValue(4.9f) && Near(slider.value, 4.0f));
        CHECK(!slider.SetValue(4.2f));  // mesmo valor depois do passo
        CHECK(slider.SetValue(99.0f) && Near(slider.value, 10.0f) && Near(slider.Fraction(), 1.0f));
        UiElement field = UiElement::Make(UiElementType::TextField);
        field.placeholder = "Digite";
        CHECK(field.DisplayText() == "Digite" && field.IsFocusable() && field.IsControl());
        field.text = "Ana";
        CHECK(field.DisplayText() == "Ana");
        CHECK(!UiElement::Make(UiElementType::ProgressBar).IsFocusable());
        CHECK(UiElement::Make(UiElementType::Button).IsFocusable() && !UiElement::Make(UiElementType::Panel).IsFocusable());
    }

    SECTION("Formato .mui versão 4: controles e follow gravados e lidos; a versão 3 ainda abre");
    {
        const fs::path dir = TestDir(argc, argv, "test_ui_controles_tmp");
        UiDocumentData doc = Options();
        doc.Q("#vida")->follow = "Inimigo";
        doc.Q("#vida")->followOffset = {0.0f, 2.0f, 0.0f};
        doc.Q("#tela_cheia")->checked = true;
        doc.Q("#volume")->step = 0.1f;
        std::string error;
        CHECK(SaveUiDocument(dir / "Opcoes.mui", doc, &error));
        UiDocumentData back;
        CHECK(LoadUiDocument(dir / "Opcoes.mui", back, &error));
        const UiElement* life = back.Q("#vida");
        CHECK(life && life->type == UiElementType::ProgressBar && Near(life->value, 0.25f) && life->follow == "Inimigo" &&
              Near(life->followOffset.y, 2.0f));
        CHECK(back.Q("#tela_cheia") && back.Q("#tela_cheia")->checked);
        CHECK(back.Q("#nome") && back.Q("#nome")->placeholder == "Seu nome" && back.Q("#nome")->maxLength == 8);
        CHECK(back.Q("#volume") && Near(back.Q("#volume")->step, 0.1f) && Near(back.Q("#volume")->value, 0.5f));
        {
            std::ifstream in(dir / "Opcoes.mui", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            CHECK(text.find("\"version\": 4") != std::string::npos);
        }
        {
            std::ofstream out(dir / "Antigo.mui", std::ios::binary);
            out << R"({"format": "motor-ui", "version": 3, "elements": [{"type": "Button", "name": "ok", "text": "OK"}]})";
        }
        UiDocumentData old;
        CHECK(LoadUiDocument(dir / "Antigo.mui", old, &error) && old.Q("#ok") && old.Q("#ok")->text == "OK");
        UiElementType type;
        CHECK(ParseUiElementType("TextField", type) && type == UiElementType::TextField);
        CHECK(!ParseUiElementType("Botao", type));
        fs::remove_all(dir);
    }

    SECTION("Eventos: OnClick, OnChange do slider arrastado e da caixa, OnHover; somem ao parar e na recarga");
    {
        Scene scene;
        AddDocument(scene, "Opcoes", Options());
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui != nullptr);
        if (!ui) return TestResult();
        Frame(runtime, {0, 0}, false);  // layout

        int clicks = 0, changes = 0, hovers = 0;
        std::string lastClick;
        CHECK(ui->OnClick(".menu", [&](UiElement& e) { ++clicks, lastClick = e.name; }) == 2);
        CHECK(ui->OnChange("Slider, Toggle", [&](UiElement&) { ++changes; }) == 2);
        CHECK(ui->OnHover("#jogar", [&](UiElement&, bool entered) { hovers += entered ? 1 : -10; }) == 1);
        CHECK(ui->OnClick("#naoexiste", [] {}) == 0);

        const glm::vec2 play = Center(*ui, ui->Q("#jogar"));
        CHECK(Near(play, {960.0f - 150.0f, 540.0f - 200.0f}));
        Frame(runtime, play, false);
        CHECK(hovers == 1);
        Click(runtime, play);
        CHECK(clicks == 1 && lastClick == "jogar");
        Frame(runtime, {5, 5}, false);
        CHECK(hovers == -9);  // saiu

        // Slider: aperta e arrasta até a ponta direita; o valor segue o mouse.
        UiBox box;
        UiElement* volume = ui->Q("#volume");
        CHECK(volume && ui->ScreenRect(*volume, box));
        Frame(runtime, box.Center(), true);
        CHECK(volume && Near(volume->value, 0.5f, 0.05f));
        Frame(runtime, {box.max.x + 50.0f, box.Center().y}, true);
        CHECK(volume && Near(volume->value, 1.0f));
        Frame(runtime, {box.min.x - 50.0f, box.Center().y}, true);  // continua arrastando fora dele
        CHECK(volume && Near(volume->value, 0.0f));
        Frame(runtime, {box.min.x - 50.0f, box.Center().y}, false);
        CHECK(changes == 2);

        // Toggle: clique troca e dá OnChange.
        UiElement* full = ui->Q("#tela_cheia");
        Click(runtime, Center(*ui, full));
        CHECK(full && full->checked && changes == 3);
        Click(runtime, Center(*ui, full));
        CHECK(full && !full->checked && changes == 4);

        // Registro num elemento já achado e ClearEvents.
        int direct = 0;
        CHECK(ui->OnClick(ui->Q("#sair"), [&] { ++direct; }));
        Click(runtime, Center(*ui, ui->Q("#sair")));
        CHECK(direct == 1 && clicks == 2);
        CHECK(ui->ClearEvents("#sair") == 2);
        Click(runtime, Center(*ui, ui->Q("#sair")));
        CHECK(direct == 1 && clicks == 2);

        // Recarga da DLL de scripts: todos os callbacks saem (podiam apontar para o código antigo).
        runtime.ForgetScripts();
        Click(runtime, play);
        CHECK(clicks == 2 && ui->Q("#jogar")->handlers.empty());
        ui->OnClick("#jogar", [&] { ++clicks; });
        runtime.Stop();
        const UiDocumentData* tree = scene.FindByName("Opcoes")->Get<UiDocument>()->Tree();
        CHECK(!tree || tree->elements.empty() || tree->elements[0].handlers.empty());
    }

    SECTION("Campo de texto: clique dá o foco, digita (UTF-8, limite), Backspace, setas, Enter envia e Esc sai");
    {
        Scene scene;
        AddDocument(scene, "Opcoes", Options());
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        if (!ui) return TestResult();
        Frame(runtime, {0, 0}, false);
        UiElement* field = ui->Q("#nome");
        int submits = 0, changes = 0;
        ui->OnClick(field, [&] { ++submits; });
        ui->OnChange(field, [&](UiElement&) { ++changes; });
        Click(runtime, Center(*ui, field));
        CHECK(ui->Focused() == field && field->focused);
        Input::AddTextInput("Joãozinho da Silva");
        Frame(runtime, {0, 0}, false);
        CHECK(field->text == "Joãozinh" && changes == 1);  // 8 caracteres (o ã conta um)
        Press(runtime, Key::Backspace);
        CHECK(field->text == "Joãozin" && changes == 2);
        Press(runtime, Key::Home);
        Press(runtime, Key::Delete);
        CHECK(field->text == "oãozin");
        Press(runtime, Key::Right);
        Input::AddTextInput("!");
        Frame(runtime, {0, 0}, false);
        CHECK(field->text == "o!ãozin");
        Press(runtime, Key::Enter);
        CHECK(submits == 1);
        Press(runtime, Key::Escape);
        CHECK(ui->Focused() == nullptr && !field->focused);
        Input::AddTextInput("x");
        Frame(runtime, {0, 0}, false);
        CHECK(field->text == "o!ãozin");  // sem foco, não digita
        runtime.Stop();
    }

    SECTION("Foco: navegação espacial pelas setas no contexto Menu, Tab, Confirmar clica e troca; slider pelas setas");
    {
        Scene scene;
        AddDocument(scene, "Opcoes", Options());
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        if (!ui) return TestResult();
        Frame(runtime, {0, 0}, false);
        int clicks = 0;
        ui->OnClick("#sair", [&] { ++clicks; });

        CHECK(ui->Focus("#jogar") && ui->Focused() == ui->Q("#jogar"));
        Press(runtime, Key::Right);
        CHECK(ui->Focused() == ui->Q("#jogar"));  // fora do contexto Menu: nada
        runtime.SetInputContexts({"Menu"});
        Press(runtime, Key::Right);
        CHECK(ui->Focused() == ui->Q("#sair"));
        Frame(runtime, {0, 0}, false);  // a folha põe :focus
        CHECK(ui->Q("#sair")->focused && ui->Q("#sair")->borderWidth > 0.0f);
        Press(runtime, Key::Enter);
        CHECK(clicks == 1);
        Press(runtime, Key::Down);
        CHECK(ui->Focused() == ui->Q("#volume"));
        const float before = ui->Q("#volume")->value;
        Press(runtime, Key::Right);
        CHECK(ui->Focused() == ui->Q("#volume") && ui->Q("#volume")->value > before);
        Press(runtime, Key::Down);
        CHECK(ui->Focused() == ui->Q("#tela_cheia"));
        Press(runtime, Key::Space);  // Confirmar troca a caixa
        CHECK(ui->Q("#tela_cheia")->checked);
        Press(runtime, Key::Tab);
        CHECK(ui->Focused() == ui->Q("#nome"));
        Press(runtime, Key::Tab);  // a barra não pega foco: volta ao começo
        CHECK(ui->Focused() == ui->Q("#jogar"));
        Press(runtime, Key::Up);
        CHECK(ui->Focused() == ui->Q("#jogar"));  // nada acima
        ui->Q("#jogar")->visible = false;
        Frame(runtime, {0, 0}, false);
        Frame(runtime, {0, 0}, false);
        CHECK(ui->Focused() == nullptr);  // escondido perde o foco
        CHECK(!ui->Focus("#vida"));
        runtime.Stop();
    }

    SECTION("Segue entidade: projetado pela câmera do jogo, com deslocamento; atrás da câmera some");
    {
        Scene scene;
        Entity& camera = scene.CreateEntity("Camera");
        camera.Add<CameraComponent>();
        camera.transform.position = {0.0f, 0.0f, 10.0f};  // olhando para -Z
        Entity& target = scene.CreateEntity("Inimigo");
        target.transform.position = {0.0f, 0.0f, 0.0f};
        UiDocumentData doc;
        UiElement tag = Element(UiElementType::ProgressBar, "vida", {0, -40}, {100, 20});
        tag.follow = "Inimigo";
        UiElement label = UiElement::Make(UiElementType::Text);
        label.name = "rotulo";
        label.text = "Orc";
        tag.children = {label};
        UiElement fixed = Element(UiElementType::Panel, "fixo", {0, 0}, {50, 50});
        doc.elements = {tag, fixed};
        AddDocument(scene, "Etiquetas", doc);

        std::vector<UiNode> layout = ComputeUiLayout(scene, kScreen);
        const UiNode* node = NodeOf(layout, "vida");
        CHECK(node && Near(node->box.Center(), {960.0f, 540.0f - 40.0f}));
        const UiNode* child = NodeOf(layout, "rotulo");
        CHECK(child && node && child->box.min.y >= node->box.min.y - 0.5f);  // o filho vai junto

        scene.FindByName("Inimigo")->transform.position = {2.0f, 0.0f, 0.0f};  // à direita
        layout = ComputeUiLayout(scene, kScreen);
        node = NodeOf(layout, "vida");
        CHECK(node && node->box.Center().x > 1000.0f);
        if (UiDocument* d = scene.FindByName("Etiquetas")->Get<UiDocument>(); d && d->Tree()) {
            d->Tree()->elements[0].followOffset = {-2.0f, 0.0f, 0.0f};  // o deslocamento em metros volta ao centro
        }
        layout = ComputeUiLayout(scene, kScreen);
        node = NodeOf(layout, "vida");
        CHECK(node && Near(node->box.Center(), {960.0f, 500.0f}));

        scene.FindByName("Inimigo")->transform.position = {0.0f, 0.0f, 20.0f};  // atrás da câmera
        layout = ComputeUiLayout(scene, kScreen);
        CHECK(!NodeOf(layout, "vida") && !NodeOf(layout, "rotulo") && NodeOf(layout, "fixo"));
        for (const UiNode& n : layout) CHECK(n.parent < static_cast<int>(layout.size()));
        scene.DestroyEntity(scene.FindByName("Camera")->id);
        scene.FindByName("Inimigo")->transform.position = {0.0f, 0.0f, 0.0f};
        layout = ComputeUiLayout(scene, kScreen);
        CHECK(!NodeOf(layout, "vida") && NodeOf(layout, "fixo"));  // sem câmera: escondido
    }

    SECTION("Converter UI por entidades num documento: mesmas caixas, recusa o que não é UI");
    {
        Scene scene;
        Entity& menu = scene.CreateEntity("Menu");
        const EntityId menuId = menu.id;
        Entity& fundo = scene.CreateEntity("Fundo", menuId);
        fundo.Add<UiRect>().size = {600.0f, 400.0f};
        fundo.Add<UiImage>().color = {0.0f, 0.0f, 0.0f, 0.5f};
        const EntityId fundoId = fundo.id;
        Entity& jogar = scene.CreateEntity("Jogar", fundoId);
        UiRect& r = jogar.Add<UiRect>();
        r.anchor = UiAnchor::Bottom;
        r.position = {0.0f, -40.0f};
        r.order = 2;
        jogar.Add<UiButton>();
        jogar.Add<UiText>().text = "Jogar";
        Entity& titulo = scene.CreateEntity("Titulo", fundoId);
        titulo.Add<UiRect>().anchor = UiAnchor::Top;
        titulo.Add<UiText>().text = "Título";
        Entity& pontos = scene.CreateEntity("Pontos", menuId);
        pontos.Add<UiRect>().anchor = UiAnchor::TopLeft;
        pontos.Add<UiText>().text = "0";

        const std::vector<UiNode> before = ComputeUiLayout(scene, kScreen);
        AssetLibrary assets;
        UiConversion conversion;
        std::string error;
        CHECK(ConvertUiEntities(scene, menuId, assets, conversion, &error));
        CHECK(!conversion.rootIsUi && conversion.converted.size() == 4);
        const UiElement* button = conversion.document.Q("#Jogar");
        CHECK(button && button->type == UiElementType::Button && button->text == "Jogar");

        Scene converted;
        AddDocument(converted, "Menu", conversion.document);
        const std::vector<UiNode> after = ComputeUiLayout(converted, kScreen);
        for (const char* name : {"Fundo", "Jogar", "Titulo", "Pontos"}) {
            const UiNode* a = nullptr;
            for (const UiNode& n : before) {
                if (const Entity* e = scene.Find(n.entity); e && e->name == name) a = &n;
            }
            const UiNode* b = NodeOf(after, name);
            CHECK(a && b && Near(a->box.min, b->box.min) && Near(a->box.max, b->box.max));
        }

        Entity& odd = scene.CreateEntity("Cubo", fundoId);
        odd.Add<MeshRenderer>();
        UiConversion refused;
        CHECK(!ConvertUiEntities(scene, menuId, assets, refused, &error) && !error.empty());
        CHECK(!ConvertUiEntities(scene, kNullEntity, assets, refused, &error));
    }

    SECTION("Edição por JSON (MCP ui_document_edit e ui_style_edit): add, set, move, duplicate, delete, folhas");
    {
        UiDocumentData doc;
        std::string error;
        UiJsonArgs ops = UiJsonArgs::parse(R"([
            {"action": "add", "type": "Panel", "name": "menu"},
            {"action": "add", "type": "Slider", "parent": "menu", "name": "volume", "min": 0, "max": 10, "value": 30},
            {"action": "add", "type": "Toggle", "parent": "menu", "name": "som", "text": "Som", "checked": true},
            {"action": "add", "type": "Button", "parent": "menu", "index": 0, "name": "voltar", "classes": "menu fim"},
            {"action": "set", "element": "volume", "value": 4, "step": 2, "style": {"fillColor": [1, 0, 0, 1]}},
            {"action": "duplicate", "element": "som"},
            {"action": "move", "element": "voltar", "parent": [0], "index": 3},
            {"action": "add_sheet", "path": "Assets/UI/Tema.mstyle"}
        ])");
        CHECK(ApplyUiDocumentOps(doc, ops, &error));
        CHECK(error.empty());
        const UiElement* menu = doc.Q("#menu");
        CHECK(menu && menu->children.size() == 4);
        const UiElement* volume = doc.Q("#volume");
        CHECK(volume && Near(volume->maxValue, 10.0f) && Near(volume->value, 4.0f) && Near(volume->step, 2.0f));
        CHECK(volume && Near(volume->fillColor.r, 1.0f) && Near(volume->fillColor.g, 0.0f));
        CHECK(doc.Q("#som") && doc.Q("#som")->checked && doc.Query("Toggle").size() == 2);
        CHECK(menu && menu->children.back().name == "voltar" && doc.Query(".fim").size() == 1);
        CHECK(doc.styles.size() == 1);
        UiPath path{0};
        const UiJsonArgs summary = UiElementSummaryJson(*menu, path);
        CHECK(summary["children"].size() == 4 && summary["children"][0]["type"] == "Slider");

        UiDocumentData copy = doc;
        CHECK(!ApplyUiDocumentOps(copy, UiJsonArgs::parse(R"([{"action": "delete", "element": "volume"},
                                                              {"action": "set", "element": "naoexiste", "text": "x"}])"),
                                  &error));
        CHECK(error.find("naoexiste") != std::string::npos);
        CHECK(!ApplyUiDocumentOps(copy, UiJsonArgs::parse(R"([{"action": "add", "type": "Botao"}])"), &error));
        CHECK(ApplyUiDocumentOps(doc, UiJsonArgs::parse(R"([{"action": "delete", "element": "volume"}])"), &error));
        CHECK(!doc.Q("#volume"));

        UiStyleSheet sheet;
        CHECK(ApplyUiStyleOps(sheet, UiJsonArgs::parse(R"([
            {"action": "add_rule", "selector": "Button:focus", "set": {"borderWidth": 4}},
            {"action": "add_rule", "selector": ".menu", "set": {"fontSize": 30}},
            {"action": "set_rule", "rule": "Button:focus", "set": {"borderColor": [1, 1, 0, 1]}},
            {"action": "delete_rule", "rule": 1}
        ])"), &error));
        CHECK(sheet.rules.size() == 1 && sheet.rules[0].selector == "Button:focus");
        CHECK(!ApplyUiStyleOps(sheet, UiJsonArgs::parse(R"([{"action": "add_rule", "selector": "Button:", "set": {}}])"),
                               &error));
        CHECK(!ApplyUiStyleOps(sheet, UiJsonArgs::parse(R"([{"action": "add_rule", "selector": "Button",
                                                             "set": {"naoexiste": 1}}])"), &error));
    }

    return TestResult();
}
