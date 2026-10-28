// Editor de UI (editor/src/uidoc): a parte sem janela. Caminhos dos elementos, inserir/mover/excluir/duplicar com nomes
// únicos, classes digitadas, redimensionar pelas alças (no fluxo, por âncora e esticado), seletor das regras, o desfazer
// por fotos do estado (com a versão de "não salvo") e a recarga dos .mui e .mstyle mudados no disco nas entidades da
// cena (também na cópia de antes do Play). Sem janela nem OpenGL.
#include "TestUtil.h"

#include "uidoc/UiDocEdit.h"
#include "uidoc/UiDocReload.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Scene.h>
#include <motor/ui/Ui.h>

#include <chrono>
#include <cmath>
#include <fstream>

using namespace motor;
using namespace editor;
namespace fs = std::filesystem;

namespace {

UiElement Named(UiElementType type, const char* name) {
    UiElement e = UiElement::Make(type);
    e.name = name;
    return e;
}

// menu (painel) > titulo, botoes (painel) > jogar, sair; e o hud solto.
UiDocumentData Menu() {
    UiDocumentData doc;
    UiElement menu = Named(UiElementType::Panel, "menu");
    UiElement buttons = Named(UiElementType::Panel, "botoes");
    buttons.children = {Named(UiElementType::Button, "jogar"), Named(UiElementType::Button, "sair")};
    menu.children = {Named(UiElementType::Text, "titulo"), buttons};
    doc.elements = {menu, Named(UiElementType::Text, "hud")};
    ComputeUiStyles(doc);
    return doc;
}

void WriteText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

// Data de modificação adiante (o sistema de arquivos pode ter resolução de segundos).
void Touch(const fs::path& file, int seconds) {
    std::error_code ec;
    fs::last_write_time(file, fs::last_write_time(file, ec) + std::chrono::seconds(seconds), ec);
}

bool Near(float a, float b) { return std::abs(a - b) < 0.01f; }

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_ui_editor_tmp");

    SECTION("Caminhos e nomes");
    {
        UiDocumentData doc = Menu();
        UiPath path;
        CHECK(FindUiElementPath(doc.elements, "sair", path) && path == (UiPath{0, 1, 1}));
        CHECK(UiElementAt(doc.elements, path)->name == "sair");
        CHECK(FindUiElementPath(doc.elements, UiElementAt(doc.elements, {0, 0}), path) && path == (UiPath{0, 0}));
        CHECK(!FindUiElementPath(doc.elements, "nada", path));
        CHECK(!UiElementAt(doc.elements, {5}) && !UiElementAt(doc.elements, {}));
        CHECK(UiChildrenAt(doc.elements, {}) == &doc.elements);
        CHECK(IsUiPathPrefix({0}, {0, 1, 1}) && IsUiPathPrefix({0, 1}, {0, 1}) && !IsUiPathPrefix({1}, {0, 1}));
        CHECK(UniqueUiElementName(doc.elements, "novo") == "novo");
        CHECK(UniqueUiElementName(doc.elements, "jogar") == "jogar2");
        const UiElement b = NewPaletteElement(UiElementType::Button, doc.elements);
        CHECK(b.type == UiElementType::Button && b.name == "botao" && b.text == "Botão");
        const UiElement p = NewPaletteElement(UiElementType::Panel, doc.elements);
        CHECK(p.inlineStyle.Find(FindUiStyleProperty("height")) && p.inlineStyle.Find(FindUiStyleProperty("color")));
        CHECK(p.name == "painel");
    }

    SECTION("Inserir, excluir, duplicar e mover");
    {
        UiDocumentData doc = Menu();
        UiPath added = InsertUiElement(doc.elements, {0, 1}, 1, NewPaletteElement(UiElementType::Button, doc.elements));
        CHECK(added == (UiPath{0, 1, 1}) && UiElementAt(doc.elements, {0, 1, 2})->name == "sair");
        CHECK(InsertUiElement(doc.elements, {}, -1, Named(UiElementType::Image, "fim")) == (UiPath{2}));
        CHECK(InsertUiElement(doc.elements, {9}, 0, UiElement{}).empty());  // pai que não existe

        // Duplicar dá nomes novos na cópia e nos filhos dela.
        const UiPath copy = DuplicateUiElement(doc.elements, {0, 1});
        CHECK(copy == (UiPath{0, 2}));
        const UiElement* dup = UiElementAt(doc.elements, copy);
        CHECK(dup && dup->name == "botoes2" && dup->children.size() == 3);
        CHECK(dup->children[0].name == "jogar2" && dup->children[2].name == "sair2");
        UiPath found;
        CHECK(FindUiElementPath(doc.elements, "jogar", found) && found == (UiPath{0, 1, 0}));

        CHECK(RemoveUiElement(doc.elements, copy) && doc.elements[0].children.size() == 2);
        CHECK(!RemoveUiElement(doc.elements, {}) && !RemoveUiElement(doc.elements, {7}));

        // Mover para outro pai, entre irmãos e para dentro de si (recusado).
        UiPath moved = MoveUiElement(doc.elements, {1}, {0, 1}, 0);  // hud para dentro dos botões
        CHECK(moved == (UiPath{0, 1, 0}) && UiElementAt(doc.elements, moved)->name == "hud");
        CHECK(doc.elements.size() == 2 && doc.elements[1].name == "fim");
        moved = MoveUiElement(doc.elements, {0, 1, 0}, {0, 1}, 3);  // para o fim da mesma lista
        CHECK(moved == (UiPath{0, 1, 3}) && doc.elements[0].children[1].children[0].name == "jogar");
        CHECK(MoveUiElement(doc.elements, {0}, {0, 1}, 0).empty());  // para dentro de si mesmo
        // O pai novo vem depois do movido entre os irmãos: o caminho dele anda um para trás.
        moved = MoveUiElement(doc.elements, {0, 0}, {0, 1}, -1);  // titulo para dentro de botoes
        CHECK(moved == (UiPath{0, 0, 4}) && UiElementAt(doc.elements, moved)->name == "titulo");
        moved = MoveUiElement(doc.elements, {1}, {}, 0);  // fim para o começo da raiz
        CHECK(moved == (UiPath{0}) && doc.elements[0].name == "fim" && doc.elements[1].name == "menu");
    }

    SECTION("Classes digitadas");
    {
        CHECK(ParseUiClassList("menu .grande, destaque  menu") == (std::vector<std::string>{"menu", "grande", "destaque"}));
        CHECK(ParseUiClassList("   ").empty());
        CHECK(JoinUiClassList({"a", "b"}) == "a b");
    }

    SECTION("Redimensionar pelas alças");
    {
        // No fluxo: só o tamanho, no eixo que mudou.
        UiElement flow = UiElement::Make(UiElementType::Button);
        ResizeUiElement(flow, {{100, 100}, {300, 160}}, {{100, 100}, {350.4f, 160}});
        CHECK(flow.width == UiLength::Px(250.0f) && flow.height.IsAuto());
        CHECK(flow.inlineStyle.Find(FindUiStyleProperty("width")) && !flow.inlineStyle.Find(FindUiStyleProperty("height")));

        // Por âncora no centro: puxar o lado direito 40 px anda o centro 20 (o lado esquerdo fica).
        UiElement centered = UiElement::Make(UiElementType::Panel);
        centered.placement = UiPlacement::Absolute;
        centered.anchor = UiAnchor::Center;
        centered.position = {10, 0};
        ResizeUiElement(centered, {{800, 500}, {1000, 600}}, {{800, 500}, {1040, 600}});
        CHECK(centered.width == UiLength::Px(240.0f) && Near(centered.position.x, 30.0f) && Near(centered.position.y, 0.0f));
        // Âncora em cima à esquerda: puxar o lado de cima 30 px para cima anda a posição 30 para cima.
        UiElement corner = UiElement::Make(UiElementType::Image);
        corner.placement = UiPlacement::Absolute;
        corner.anchor = UiAnchor::TopLeft;
        corner.position = {40, 40};
        ResizeUiElement(corner, {{40, 40}, {140, 140}}, {{40, 10}, {140, 140}});
        CHECK(corner.height == UiLength::Px(130.0f) && Near(corner.position.y, 10.0f) && Near(corner.position.x, 40.0f));
        // Esticado: position = margens esquerda e cima, width/height = direita e baixo.
        UiElement stretch = UiElement::Make(UiElementType::Panel);
        stretch.placement = UiPlacement::Absolute;
        stretch.anchor = UiAnchor::Stretch;
        stretch.position = {20, 20};
        stretch.width = UiLength::Px(20.0f);
        ResizeUiElement(stretch, {{20, 20}, {1900, 1060}}, {{50, 20}, {1850, 1060}});
        CHECK(Near(stretch.position.x, 50.0f) && stretch.width == UiLength::Px(70.0f) && stretch.height.IsAuto());

        // Gravado e lido de volta: as declarações escritas pelo redimensionar vão para o arquivo.
        UiDocumentData doc;
        doc.elements = {centered};
        ComputeUiStyles(doc);
        UiDocumentData back;
        CHECK(UiDocumentFromJson(UiDocumentToJson(doc), back));
        CHECK(back.elements.size() == 1 && back.elements[0].width == UiLength::Px(240.0f) &&
              Near(back.elements[0].position.x, 30.0f));
    }

    SECTION("Seletor das regras");
    {
        UiStyleRule rule;
        std::string error;
        CHECK(SetUiRuleSelector(rule, "Button.menu:hover, #sair", &error) && rule.selectors.size() == 2);
        CHECK(rule.selector == "Button.menu:hover, #sair");
        CHECK(!SetUiRuleSelector(rule, "Panel .item", &error) && !error.empty());
        CHECK(rule.selector == "Button.menu:hover, #sair" && rule.selectors.size() == 2);  // ficou como estava
    }

    SECTION("Folhas editadas valem na prévia");
    {
        UiEditState state;
        state.doc = Menu();
        state.doc.styles = {UiFont{"", "Assets/UI/Tema.mstyle"}};
        UiEditSheet sheet;
        sheet.key = "Assets/UI/Tema.mstyle";
        sheet.file = dir / "Tema.mstyle";
        UiStyleRule rule;
        SetUiRuleSelector(rule, "#jogar");
        rule.declarations.Set("radius", "14");
        sheet.sheet.rules.push_back(rule);
        state.sheets.push_back(sheet);
        ApplyUiEditSheets(state);
        CHECK(state.doc.Find("jogar")->radius == 14.0f && state.doc.Find("sair")->radius == 0.0f);
        state.sheets[0].sheet.rules[0].declarations.Set("radius", "3");
        ApplyUiEditSheets(state);
        CHECK(state.doc.Find("jogar")->radius == 3.0f);
    }

    SECTION("Desfazer do Editor de UI");
    {
        UiEditState state;
        state.doc = Menu();
        UiEditHistory history;
        history.Reset(state);
        const uint64_t saved = history.Version();
        CHECK(!history.CanUndo() && !history.Commit(state));  // nada mudou

        state.selected = {1};
        CHECK(!history.Commit(state));  // a seleção não vira passo
        state.doc.Find("hud")->text = "Pontos";
        CHECK(history.Commit(state) && history.CanUndo() && history.Version() != saved);
        InsertUiElement(state.doc.elements, {}, -1, NewPaletteElement(UiElementType::Image, state.doc.elements));
        CHECK(history.Commit(state));
        const uint64_t two = history.Version();

        CHECK(history.Undo(state) && state.doc.elements.size() == 2 && state.doc.Find("hud")->text == "Pontos");
        CHECK(history.Undo(state) && state.doc.Find("hud")->text != "Pontos");
        CHECK(history.Version() == saved);  // de volta ao gravado: "não salvo" some
        CHECK(!history.Undo(state));
        CHECK(history.Redo(state) && history.Redo(state) && state.doc.elements.size() == 3 && history.Version() == two);
        CHECK(!history.CanRedo());

        // Mudança ainda não registrada entra no desfazer: desfazer volta para antes dela.
        CHECK(history.Undo(state) && state.doc.elements.size() == 2);
        state.doc.Find("titulo")->text = "Outro";
        CHECK(history.Undo(state) && state.doc.Find("titulo")->text != "Outro" && history.CanRedo());
        // Mudar depois de desfazer acaba com o refazer.
        state.doc.Find("titulo")->classes = {"grande"};
        CHECK(history.Commit(state) && !history.CanRedo());

        // Folha mudada também é passo.
        UiEditSheet sheet;
        sheet.key = "Assets/UI/Tema.mstyle";
        state.sheets.push_back(sheet);
        CHECK(history.Commit(state));
        UiStyleRule rule;
        SetUiRuleSelector(rule, "Button");
        state.sheets[0].sheet.rules.push_back(rule);
        CHECK(history.Commit(state));
        CHECK(history.Undo(state) && state.sheets[0].sheet.rules.empty());
    }

    SECTION("Recarga dos .mui e .mstyle mudados no disco");
    {
        const fs::path project = dir / "Projeto Ação";
        const fs::path ui = project / "Assets" / "UI";
        fs::create_directories(ui);
        const fs::path menuFile = ui / "Menu.mui";
        const fs::path sheetFile = ui / "Tema.mstyle";
        WriteText(sheetFile, R"({"format": "motor-style", "version": 1, "rules": [{"selector": "#jogar", "radius": 8}]})");
        WriteText(menuFile, R"({"format": "motor-ui", "version": 3, "styles": ["Assets/UI/Tema.mstyle"], "elements": [
            {"type": "Button", "name": "jogar", "text": "Jogar"}]})");
        AssetLibrary assets;
        assets.SetRoot(project);
        assets.Database().SetAssetsDir(project / "Assets");
        CHECK(UiAssetFile("", "Assets/UI/Menu.mui", assets) == menuFile.lexically_normal());

        Scene scene;
        UiDocument& doc = scene.CreateEntity("Menu").Add<UiDocument>();
        SetUiDocumentFile(doc.document, "Assets/UI/Menu.mui", assets);
        CHECK(doc.Tree() && doc.Tree()->Find("jogar")->radius == 8.0f);
        Scene backup = scene;  // como a cena de antes do Play
        UiDocument& backupDoc = *backup.FindByName("Menu")->Get<UiDocument>();
        auto forEach = [&](const std::function<void(Scene&)>& fn) {
            fn(scene);
            fn(backup);
        };

        UiDocReloader reloader;
        CHECK(reloader.Update(scene, assets, forEach).empty());  // primeira vez: só anota as datas
        CHECK(reloader.Update(scene, assets, forEach).empty());

        // O .mui muda: a árvore viva é refeita do arquivo novo, nas duas cenas, lido uma vez só.
        doc.Tree()->Find("jogar")->text = "mudado pelo jogo";
        WriteText(menuFile, R"({"format": "motor-ui", "version": 3, "styles": ["Assets/UI/Tema.mstyle"], "elements": [
            {"type": "Button", "name": "jogar", "text": "Começar"}, {"type": "Text", "name": "dica"}]})");
        Touch(menuFile, 5);
        auto changed = reloader.Update(scene, assets, forEach);
        CHECK(changed.size() == 1 && changed[0] == menuFile.lexically_normal());
        CHECK(doc.Tree()->Find("jogar")->text == "Começar" && doc.Tree()->Find("dica"));
        CHECK(backupDoc.document.data == doc.document.data);

        // A folha muda: o documento que a usa relê.
        WriteText(sheetFile, R"({"format": "motor-style", "version": 1, "rules": [{"selector": "#jogar", "radius": 20}]})");
        Touch(sheetFile, 10);
        changed = reloader.Update(scene, assets, forEach);
        CHECK(changed.size() == 1 && doc.Tree()->Find("jogar")->radius == 20.0f);
        CHECK(reloader.Update(scene, assets, forEach).empty());
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    return TestResult();
}
