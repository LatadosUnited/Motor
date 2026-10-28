// Estilos da UI (Etapa 4 da fase Interface em documentos): o formato .mstyle, os seletores (tipo, classe, nome,
// estados, listas, especificidade), a cascata (padrão da engine < folhas < elemento) e a herança, o .mui versão 3 com
// folhas achadas pelo GUID num projeto com acento, :hover/:active/:disabled pelo UiSystem, as transições, o visual
// novo (cantos, borda, 9-slice, contorno, sombra, opacidade, escala) na lista de desenho, e um menu de 50 elementos e
// 10 regras que não refaz nada em quadros sem mudança. Sem janela nem OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/ui/Ui.h>

#include <cmath>
#include <format>
#include <fstream>
#include <memory>
#include <string>

using namespace motor;
namespace fs = std::filesystem;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const glm::vec2 kScreen{1920.0f, 1080.0f};  // escala 1

bool Near(float a, float b, float eps = 0.01f) { return std::abs(a - b) < eps; }
bool Near(const glm::vec2& a, const glm::vec2& b, float eps = 0.01f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }
bool Near(const glm::vec4& a, const glm::vec4& b, float eps = 0.01f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::shared_ptr<const UiStyleSheet> Sheet(const std::string& rules) {
    auto sheet = std::make_shared<UiStyleSheet>();
    std::string error;
    const bool ok = UiStyleSheetFromJson(R"({"format": "motor-style", "version": 1, "rules": [)" + rules + "]}", *sheet, &error);
    if (!ok) std::printf("  folha inválida: %s\n", error.c_str());
    return sheet;
}

// Documento lido do JSON (versão 3), com as folhas já carregadas e a cascata feita.
UiDocumentData Doc(const std::string& elements, std::vector<std::shared_ptr<const UiStyleSheet>> sheets = {},
                   int version = 3) {
    UiDocumentData doc;
    std::string error;
    if (!UiDocumentFromJson(std::format(R"({{"format": "motor-ui", "version": {}, "elements": [{}]}})", version, elements),
                            doc, &error)) {
        std::printf("  documento inválido: %s\n", error.c_str());
    }
    doc.sheets = std::move(sheets);
    ComputeUiStyles(doc);
    return doc;
}

Entity& AddDocument(Scene& scene, const UiDocumentData& data) {
    Entity& e = scene.CreateEntity("Menu");
    e.Add<UiDocument>().document.data = std::make_shared<UiDocumentData>(data);
    return e;
}

void Frame(SceneRuntime& runtime, glm::vec2 mouse, bool down) {
    if (auto* ui = runtime.GetSystem<UiSystem>()) ui->SetPointer({kScreen, mouse, true, down});
    runtime.FixedUpdate(kDt);
    runtime.Update(kDt);
}

size_t QuadsOf(const DrawList2D& list) { return list.QuadCount(); }

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_ui_estilo_tmp");

    SECTION(".mstyle gravado e lido de volta igual; leitura tolerante");
    {
        const auto sheet = Sheet(R"(
            {"selector": "Button", "radius": 12, "borderWidth": 2, "borderColor": [1, 1, 1, 0.5], "transition": 0.2},
            {"selector": "Button.menu:hover, #sair", "normalColor": [1, 0, 0, 1], "scale": 1.05},
            {"selector": ".titulo", "font": "Assets/Fontes/Título.ttf", "fontSize": 64, "textOutlineWidth": 2,
             "textShadowOffset": [3, 3], "textShadowColor": [0, 0, 0, 0.5], "slice": [8, 16], "width": "50%",
             "align": "Left", "opacity": 0.8})");
        CHECK(sheet->rules.size() == 3);
        CHECK(sheet->rules[1].selectors.size() == 2);
        const fs::path file = dir / "Jogo Ação" / "Assets" / "UI" / "Tema escuro.mstyle";
        fs::create_directories(file.parent_path());
        std::string error;
        CHECK(SaveUiStyleSheet(file, *sheet, &error));
        UiStyleSheet loaded;
        CHECK(LoadUiStyleSheet(file, loaded, &error));
        CHECK(loaded == *sheet);
        CHECK(AssetTypeFromPath(file) == AssetType::UiStyle);
        const std::string json = UiStyleSheetToJson(*sheet);
        CHECK(json.find("\"format\": \"motor-style\"") != std::string::npos);
        CHECK(json.find("\"50%\"") != std::string::npos && json.find("\"Left\"") != std::string::npos);
        const UiStyleValue* font = loaded.rules[2].declarations.Find(FindUiStyleProperty("font"));
        CHECK(font && std::get<UiFont>(*font).path == "Assets/Fontes/Título.ttf");

        UiStyleSheet tolerant;
        std::vector<std::string> warnings;
        CHECK(UiStyleSheetFromJson(R"({"format": "motor-style", "version": 1, "rules": [
            {"selector": "Panel", "color": [1, 0, 0], "gap": 4, "cor": 1},
            {"selector": "Panel .item", "gap": 2},
            {"selector": "Medidor", "gap": 2},
            {"selector": "Button:pressed", "gap": 2},
            {"gap": 2},
            {"selector": "*", "radius": "x"}]})", tolerant, &error, &warnings));
        CHECK(tolerant.rules.size() == 2);  // Panel e *
        CHECK(tolerant.rules[0].declarations.items.size() == 1);  // só o gap: a cor com 3 números e "cor" ficam fora
        CHECK(tolerant.rules[1].declarations.Empty());
        CHECK(warnings.size() == 7);
        CHECK(!UiStyleSheetFromJson(R"({"format": "motor-style", "version": 9})", tolerant, &error));
        CHECK(error.find("9") != std::string::npos);
        CHECK(!UiStyleSheetFromJson(R"({"format": "motor-ui"})", tolerant, &error));
        CHECK(!UiStyleSheetFromJson("{", tolerant, &error));
        CHECK(UiStyleSheetFromJson(R"({"format": "motor-style"})", tolerant, &error) && tolerant.rules.empty());
    }

    SECTION("Seletores: tipo, classe, nome, estados, lista e especificidade");
    {
        std::vector<UiSelector> list;
        std::string error;
        CHECK(ParseUiSelectors(" Button.menu.grande:hover:active , #sair,*, .título", list, &error));
        CHECK(list.size() == 4);
        CHECK(list[0].type == "Button" && list[0].classes.size() == 2 && list[0].states == (kUiStateHover | kUiStateActive));
        CHECK(list[1].name == "sair" && list[1].type.empty());
        CHECK(list[2].type.empty() && list[2].classes.empty() && list[2].name.empty());
        CHECK(list[3].classes.size() == 1 && list[3].classes[0] == "título");  // UTF-8 no nome
        CHECK(list[1].Specificity() > list[0].Specificity());  // #nome ganha de quatro classes e estados
        CHECK(list[0].Specificity() == 4 * 100 + 1);
        CHECK(list[2].Specificity() == 0);
        CHECK(!ParseUiSelectors("Panel .item", list, &error) && error.find("combinador") != std::string::npos);
        CHECK(!ParseUiSelectors("Panel > Text", list, &error));
        CHECK(!ParseUiSelectors("Button:pressed", list, &error));
        CHECK(!ParseUiSelectors("Medidor", list, &error));
        CHECK(!ParseUiSelectors("a,,b", list, &error));
        CHECK(!ParseUiSelectors("#a#b", list, &error));
        CHECK(!ParseUiSelectors("", list, &error));

        UiElement button = UiElement::Make(UiElementType::Button);
        button.name = "sair";
        button.classes = {"menu", "grande"};
        CHECK(ParseUiSelectors("Button.menu.grande:hover:active", list, &error));
        CHECK(!list[0].Matches(button, UiElementStates(button)));
        button.hovered = button.pressed = true;
        CHECK(list[0].Matches(button, UiElementStates(button)));
        button.classes = {"menu"};
        CHECK(!list[0].Matches(button, UiElementStates(button)));
        CHECK(ParseUiSelectors("Text, :disabled, #sair:focus", list, &error));
        CHECK(!list[0].Matches(button, 0) && !list[1].Matches(button, UiElementStates(button)));
        button.interactable = false;
        CHECK(list[1].Matches(button, UiElementStates(button)));
        button.focused = true;
        CHECK(list[2].Matches(button, UiElementStates(button)));
    }

    SECTION("Cascata: padrão < folha < elemento; especificidade, ordem e herança");
    {
        const auto tema = Sheet(R"(
            {"selector": "Button", "normalColor": [0, 0, 1, 1], "radius": 4, "fontSize": 20},
            {"selector": ".menu", "normalColor": [0, 1, 0, 1]},
            {"selector": "Button", "radius": 8},
            {"selector": "#jogar", "radius": 16},
            {"selector": "Panel", "textColor": [1, 1, 0, 1], "fontSize": 50, "align": "Right"})");
        const auto outra = Sheet(R"({"selector": ".menu", "normalColor": [1, 0, 1, 1]})");
        UiDocumentData doc = Doc(R"(
            {"type": "Panel", "name": "painel", "children": [
                {"type": "Button", "name": "jogar", "classes": ["menu"]},
                {"type": "Button", "name": "sair", "classes": ["menu"], "normalColor": [0.5, 0.5, 0.5, 1]},
                {"type": "Button", "name": "voltar"},
                {"type": "Text", "name": "texto", "textColor": [1, 1, 1, 1]}]})",
                                 {tema, outra});
        const UiElement* jogar = doc.Find("jogar");
        CHECK(jogar->normalColor == glm::vec4(1, 0, 1, 1));  // .menu da segunda folha ganha (mesma especificidade, depois)
        CHECK(jogar->radius == 16.0f);                       // #jogar ganha do tipo
        CHECK(doc.Find("voltar")->radius == 8.0f);           // a segunda regra "Button" vem depois
        CHECK(doc.Find("voltar")->normalColor == glm::vec4(0, 0, 1, 1));
        CHECK(doc.Find("sair")->normalColor == glm::vec4(0.5f, 0.5f, 0.5f, 1));  // no elemento ganha de tudo
        CHECK(jogar->padding == UiEdges::Symmetric(12, 24));  // da folha padrão da engine
        // Herança: o texto e o botão sem regra própria pegam do painel; o que o elemento ou uma regra dá vale mais.
        CHECK(doc.Find("texto")->fontSize == 50.0f && doc.Find("texto")->align == UiTextAlign::Right);
        CHECK(doc.Find("texto")->textColor == glm::vec4(1, 1, 1, 1));
        CHECK(jogar->textColor == glm::vec4(1, 1, 0, 1) && jogar->fontSize == 20.0f);
        CHECK(doc.Find("painel")->color.a == 0.0f);  // não herdada: o painel continua transparente

        // A folha padrão da engine dá o mesmo que UiElement::Make (o visual de sempre).
        for (UiElementType t : {UiElementType::Panel, UiElementType::Text, UiElementType::Image, UiElementType::Button}) {
            UiDocumentData single;
            single.elements = {UiElement::Make(t)};
            ComputeUiStyles(single);
            bool same = true;
            for (int p = 0; p < UiStylePropertyCount(); ++p) {
                same = same && GetUiStyleValue(single.elements[0], p) == GetUiStyleValue(UiElement::Make(t), p);
            }
            CHECK(same);
        }

        // SetStyle: escreve no elemento, muda o campo na hora e sobrevive ao próximo cálculo.
        UiElement* voltar = doc.Find("voltar");
        CHECK(voltar->SetStyle("radius", "30") && voltar->radius == 30.0f);
        voltar->color = {0, 0, 0, 1};  // campo mudado direto: só até o próximo cálculo
        CHECK(UpdateUiStyles(doc));
        CHECK(voltar->radius == 30.0f && voltar->color == glm::vec4(1.0f));
        CHECK(!UpdateUiStyles(doc));  // nada mudou
        CHECK(!voltar->SetStyle("raio", "1") && !voltar->SetStyle("radius", "\"x\""));
        CHECK(!voltar->SetStyleValue("radius", glm::vec4(1.0f)));  // tipo errado
        CHECK(voltar->ClearStyle("radius") && UpdateUiStyles(doc) && voltar->radius == 8.0f);

        // Gravado: só o que está no elemento (e o painel com o textColor da folha não ganha nada).
        const std::string json = UiDocumentToJson(doc);
        CHECK(json.find("\"version\": 4") != std::string::npos);
        CHECK(json.find("\"radius\"") == std::string::npos && json.find("\"normalColor\"") != std::string::npos);
        UiDocumentData back;
        std::string error;
        CHECK(UiDocumentFromJson(json, back, &error) && back.SameContent(doc));
        doc.Find("sair")->SetStyle("gap", "3");
        CHECK(!back.SameContent(doc));
    }

    SECTION("Versões 1 e 2 lidas como antes: sem herança, as chaves viram declarações do elemento");
    {
        const std::string elements = R"(
            {"type": "Panel", "textColor": [1, 0, 0, 1], "fontSize": 50, "children": [
                {"type": "Text", "name": "t", "children": [{"type": "Text", "name": "neto"}]},
                {"type": "Text", "name": "u", "fontSize": 20}]})";
        const UiDocumentData v2 = Doc(elements, {}, 2);
        CHECK(v2.Find("t")->textColor == glm::vec4(1.0f) && v2.Find("t")->fontSize == 36.0f);
        CHECK(v2.Find("neto")->textColor == glm::vec4(1.0f));
        CHECK(v2.Find("u")->fontSize == 20.0f);
        const UiDocumentData v3 = Doc(elements, {}, 3);
        CHECK(v3.Find("t")->textColor == glm::vec4(1, 0, 0, 1) && v3.Find("neto")->fontSize == 50.0f);
        // Gravado na versão 3 e lido de volta: o mesmo resultado da versão 2.
        UiDocumentData back;
        std::string error;
        CHECK(UiDocumentFromJson(UiDocumentToJson(v2), back, &error) && back.SameContent(v2));
        CHECK(back.Find("neto")->textColor == glm::vec4(1.0f));

        const UiDocumentData v1 = Doc(R"({"type": "Button", "name": "ok", "size": [300, 90]})", {}, 1);
        const UiElement* ok = v1.Find("ok");
        CHECK(ok->placement == UiPlacement::Absolute && ok->width == UiLength::Px(300) && ok->padding == UiEdges{});
    }

    SECTION(".mui com folhas num projeto: GUID, arquivo movido, Recarregar");
    {
        const fs::path project = dir / "Jogo Ação";
        const fs::path sheetFile = project / "Assets" / "UI" / "Tema.mstyle";
        const fs::path muiFile = project / "Assets" / "UI" / "Menu ação.mui";
        WriteText(sheetFile, R"({"format": "motor-style", "version": 1, "rules": [
            {"selector": "Button", "normalColor": [1, 0, 0, 1], "radius": 10}]})");
        WriteText(muiFile, R"({"format": "motor-ui", "version": 3, "styles": ["Assets/UI/Tema.mstyle", "Assets/UI/Nada.mstyle"],
            "elements": [{"type": "Button", "name": "jogar"}]})");
        AssetLibrary assets;
        assets.SetRoot(project);
        assets.Database().SetAssetsDir(project / "Assets");
        const std::string guid = assets.Database().GuidFor(sheetFile);
        CHECK(AssetDatabase::IsValidGuid(guid));

        Scene scene;
        Entity& e = scene.CreateEntity("HUD");
        UiDocument& component = e.Add<UiDocument>();
        SetUiDocumentFile(component.document, "Assets/UI/Menu ação.mui", assets);
        CHECK(component.document.data != nullptr);
        if (component.document.data) {
            const UiDocumentData& data = *component.document.data;
            CHECK(data.styles.size() == 2 && data.styles[0].guid == guid);
            CHECK(data.sheets.size() == 2 && data.sheets[0] && !data.sheets[1]);  // a que não existe: aviso
            CHECK(data.Find("jogar")->normalColor == glm::vec4(1, 0, 0, 1) && data.Find("jogar")->radius == 10.0f);
            const std::string json = UiDocumentToJson(data);
            CHECK(json.find(guid) != std::string::npos && json.find("\"styles\"") != std::string::npos);
        }
        CHECK(component.Tree()->Find("jogar")->radius == 10.0f);
        // Gravado (como o editor grava): a folha vai com o GUID, que acha o arquivo mesmo movido.
        if (component.document.data) CHECK(SaveUiDocument(muiFile, *component.document.data));

        // Folha movida (com o .meta): o GUID acha o lugar novo.
        const fs::path moved = project / "Assets" / "Temas" / "Escuro.mstyle";
        fs::create_directories(moved.parent_path());
        fs::rename(sheetFile, moved);
        fs::rename(AssetDatabase::MetaPathFor(sheetFile), AssetDatabase::MetaPathFor(moved));
        assets.Database().Refresh();
        // Mudou a folha fora do editor: Recarregar lê de novo o .mui e as folhas, e a árvore viva é refeita.
        WriteText(moved, R"({"format": "motor-style", "version": 1, "rules": [
            {"selector": "Button", "normalColor": [0, 1, 0, 1], "radius": 10}]})");
        CHECK(component.Tree()->Find("jogar")->normalColor == glm::vec4(1, 0, 0, 1));
        ReloadUiDocument(component.document, assets);
        CHECK(component.document.data && component.document.data->styles[0].path == "Assets/Temas/Escuro.mstyle");
        CHECK(component.Tree()->Find("jogar")->normalColor == glm::vec4(0, 1, 0, 1));
    }

    SECTION(":hover, :active e :disabled pelo UiSystem; a cascata só roda quando um estado muda");
    {
        const auto tema = Sheet(R"(
            {"selector": "Button", "normalColor": [0.2, 0.2, 0.2, 1], "hoverColor": [0.2, 0.2, 0.2, 1],
             "pressedColor": [0.2, 0.2, 0.2, 1]},
            {"selector": "Button:hover", "normalColor": [1, 0, 0, 1], "hoverColor": [1, 0, 0, 1], "textColor": [0, 0, 0, 1]},
            {"selector": "Button:hover:active", "pressedColor": [0, 0, 1, 1]},
            {"selector": "Button:disabled", "textColor": [0.5, 0.5, 0.5, 1]})");
        Scene scene;
        AddDocument(scene, Doc(R"(
            {"type": "Button", "name": "jogar", "placement": "Absolute", "anchor": "TopLeft", "position": [100, 100],
             "width": 200, "height": 60},
            {"type": "Button", "name": "sair", "placement": "Absolute", "anchor": "TopLeft", "position": [100, 300],
             "width": 200, "height": 60, "interactable": false})",
                               {tema}));
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui != nullptr);
        if (ui) {
            const glm::vec2 over{150, 130}, outside{1000, 900};
            Frame(runtime, outside, false);
            UiElement* jogar = ui->FindElement("jogar");
            UiElement* sair = ui->FindElement("sair");
            UiDocumentData* tree = scene.FindByName("Menu")->Get<UiDocument>()->Tree();
            CHECK(jogar && sair && tree);
            CHECK(jogar->textColor == glm::vec4(1.0f) && Near(jogar->BackgroundColor(), {0.2f, 0.2f, 0.2f, 1}));
            CHECK(sair->textColor == glm::vec4(0.5f, 0.5f, 0.5f, 1));  // :disabled desde o começo
            const uint64_t runs = tree->styleRuns;
            Frame(runtime, outside, false);
            Frame(runtime, outside, false);
            CHECK(tree->styleRuns == runs);  // nada mudou: nada recalculado

            Frame(runtime, over, false);
            CHECK(jogar->hovered && jogar->textColor == glm::vec4(0, 0, 0, 1));
            CHECK(Near(jogar->BackgroundColor(), {1, 0, 0, 1}));
            CHECK(tree->styleRuns == runs + 1);
            Frame(runtime, over, true);
            CHECK(jogar->pressed && Near(jogar->BackgroundColor(), {0, 0, 1, 1}));  // :hover:active
            Frame(runtime, over, false);
            CHECK(ui->WasClicked("jogar"));
            Frame(runtime, outside, false);
            CHECK(!jogar->hovered && jogar->textColor == glm::vec4(1.0f));
            Frame(runtime, {150, 330}, false);
            CHECK(!sair->hovered);  // não clicável: sem :hover
        }
        runtime.Stop();
    }

    SECTION("Transições: andam com o dt no Play e param no alvo; fora do Play pulam");
    {
        const auto tema = Sheet(R"(
            {"selector": "Button", "normalColor": [0, 0, 0, 1], "transition": 0.5},
            {"selector": "Button:hover", "hoverColor": [1, 1, 1, 1], "scale": 2, "opacity": 0.5})");
        const UiDocumentData data = Doc(R"(
            {"type": "Button", "name": "b", "placement": "Absolute", "anchor": "TopLeft", "position": [100, 100],
             "width": 200, "height": 60})",
                                        {tema});
        // No editor (sem UiSystem): o valor novo vale na hora.
        UiDocumentData editor = data;
        editor.Find("b")->hovered = true;
        CHECK(UpdateUiStyles(editor));
        CHECK(!editor.Find("b")->animation.active && editor.Find("b")->ShownVisual().scale == 2.0f);

        Scene scene;
        AddDocument(scene, data);
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        if (ui) {
            const glm::vec2 over{150, 130};
            Frame(runtime, {1000, 900}, false);
            UiElement* b = ui->FindElement("b");
            CHECK(b && !b->animation.active && Near(b->ShownVisual().background, {0, 0, 0, 1}));
            Frame(runtime, over, false);  // começa a transição (e anda um passo)
            CHECK(b->animation.active);
            const UiVisual first = b->ShownVisual();
            CHECK(first.background.r > 0.0f && first.background.r < 1.0f && first.scale > 1.0f && first.scale < 2.0f);
            CHECK(first.opacity < 1.0f && first.opacity > 0.5f);
            for (int i = 0; i < 10; ++i) Frame(runtime, over, false);
            const UiVisual middle = b->ShownVisual();
            CHECK(middle.background.r > first.background.r && middle.scale > first.scale);
            CHECK(Near(b->Visual().background, {1, 1, 1, 1}));  // o alvo já é o do :hover
            for (int i = 0; i < 30; ++i) Frame(runtime, over, false);
            CHECK(!b->animation.active && Near(b->ShownVisual().background, {1, 1, 1, 1}) && b->ShownVisual().scale == 2.0f);
            // Saiu do botão no meio da volta: a transição parte do valor mostrado, não do alvo antigo.
            Frame(runtime, {1000, 900}, false);
            for (int i = 0; i < 3; ++i) Frame(runtime, {1000, 900}, false);
            const float back = b->ShownVisual().scale;
            CHECK(back < 2.0f && back > 1.0f);
            Frame(runtime, over, false);
            CHECK(b->animation.active && b->animation.from.scale == back);
        }
        runtime.Stop();
    }

    SECTION("Visual: cantos, borda, 9-slice, contorno, sombra, opacidade e escala na lista de desenho");
    {
        // 9-slice direto: caixa 100x50 com bordas de 10 -> 9 quadrados, cantos fixos, uv das bordas.
        std::vector<Vertex2D> v;
        AppendUiNineSlice(v, {{0, 0}, {100, 50}}, UiEdges::All(10), {0, 1}, {1, 0}, UiEdges::All(0.25f), glm::vec4(1));
        CHECK(v.size() == 36);
        CHECK(v.size() == 36 && Near(v[0].position, {0, 0}) && Near(v[2].position, {10, 10}));  // canto de cima à esquerda
        CHECK(v.size() == 36 && Near(v[2].uv, {0.25f, 0.75f}));  // linha 0 da textura = base: o topo usa v = 1
        CHECK(v.size() == 36 && Near(v[16].position, {10, 10}) && Near(v[18].position, {90, 40}));  // o meio estica
        v.clear();
        AppendUiNineSlice(v, {{0, 0}, {16, 50}}, UiEdges::All(10), {0, 0}, {1, 1}, UiEdges::All(0.5f), glm::vec4(1));
        CHECK(v.size() == 24 && Near(v[2].position, {8, 10}));  // estreita: os cantos encolhem, a coluna do meio some
        const std::vector<uint8_t> circle = MakeUiCirclePixels(64);
        CHECK(circle.size() == 64 * 64 * 4);
        CHECK(circle[(32 * 64 + 32) * 4 + 3] == 255 && circle[3] == 0);  // centro opaco, canto transparente
        CHECK(circle[(32 * 64 + 0) * 4 + 3] >= 250);  // a borda do círculo encosta no lado da textura

        AssetLibrary assets;
        Scene scene;
        Entity& entity = AddDocument(scene, Doc(R"(
            {"type": "Panel", "name": "fundo", "placement": "Absolute", "anchor": "TopLeft", "width": 200, "height": 60,
             "color": [1, 1, 1, 1]})"));
        UiCanvas canvas;
        canvas.Build(scene, assets, kScreen);
        CHECK(QuadsOf(canvas.List()) == 1);
        UiElement* fundo = entity.Get<UiDocument>()->Tree()->Find("fundo");
        fundo->SetStyle("radius", "12");
        canvas.Build(scene, assets, kScreen);
        CHECK(QuadsOf(canvas.List()) == 9);  // cantos, bordas e meio
        const auto& verts = canvas.List().Vertices();
        CHECK(Near(verts[0].position, {0, 0}) && Near(verts[2].position, {12, 12}) && Near(verts[2].uv, {0.5f, 0.5f}));
        fundo->SetStyle("borderWidth", "2");
        fundo->SetStyle("borderColor", "[1, 0, 0, 1]");
        canvas.Build(scene, assets, kScreen);
        CHECK(QuadsOf(canvas.List()) == 18);  // a borda (forma inteira) e o fundo encolhido por cima
        CHECK(canvas.List().Vertices()[0].color == glm::vec4(1, 0, 0, 1));
        CHECK(Near(canvas.List().Vertices()[36].position, {2, 2}) && Near(canvas.List().Vertices()[38].position, {12, 12}));
        CHECK(canvas.Stats().batches == 1);  // sem GPU as texturas são nulas: tudo num lote
        fundo->SetStyle("radius", "0");
        canvas.Build(scene, assets, kScreen);
        CHECK(QuadsOf(canvas.List()) == 2);  // sem cantos: dois retângulos
        fundo->SetStyle("opacity", "0.5");
        fundo->SetStyle("scale", "2");
        canvas.Build(scene, assets, kScreen);
        CHECK(canvas.List().Vertices()[0].color.a == 0.5f);
        CHECK(Near(canvas.List().Vertices()[0].position, {-100, -30}) && Near(canvas.List().Vertices()[2].position, {300, 90}));

        // Texto com contorno (8 cópias) e sombra (uma), por baixo; a opacidade do pai desce para o filho.
        Scene textScene;
        Entity& textEntity = AddDocument(textScene, Doc(R"(
            {"type": "Panel", "name": "grupo", "opacity": 0.5, "children": [
                {"type": "Text", "name": "t", "text": "AB", "textColor": [1, 1, 1, 1]}]})"));
        canvas.Build(textScene, assets, kScreen);
        const size_t glyphs = QuadsOf(canvas.List());
        CHECK(glyphs == 2 && canvas.List().Vertices()[0].color.a == 0.5f);
        UiElement* t = textEntity.Get<UiDocument>()->Tree()->Find("t");
        const glm::vec2 plain = canvas.List().Vertices()[0].position;
        t->SetStyle("textOutlineWidth", "2");
        t->SetStyle("textOutlineColor", "[0, 0, 0, 1]");
        t->SetStyle("textShadowOffset", "[4, 6]");
        t->SetStyle("textShadowColor", "[0, 0, 0, 0.5]");
        canvas.Build(textScene, assets, kScreen);
        CHECK(QuadsOf(canvas.List()) == glyphs * 10);
        const auto& tv = canvas.List().Vertices();
        CHECK(Near(tv[0].position, plain + glm::vec2(4, 6)) && tv[0].color == glm::vec4(0, 0, 0, 0.25f));  // sombra
        CHECK(Near(tv[glyphs * 4].position, plain + glm::vec2(2, 0)));  // primeira cópia do contorno
        CHECK(Near(tv[glyphs * 9 * 4].position, plain) && tv[glyphs * 9 * 4].color.a == 0.5f);  // o texto por cima
    }

    SECTION("Menu com 50 elementos e 10 regras: quadros sem mudança não refazem estilo, layout nem geometria");
    {
        const auto tema = Sheet(R"(
            {"selector": "Panel", "gap": 4, "padding": 10},
            {"selector": "Button", "radius": 8, "borderWidth": 2, "borderColor": [1, 1, 1, 0.3]},
            {"selector": "Button:hover", "normalColor": [0.3, 0.4, 0.6, 1]},
            {"selector": "Button:active", "normalColor": [0.1, 0.1, 0.2, 1]},
            {"selector": "Button:disabled", "opacity": 0.5},
            {"selector": ".item", "height": 18},
            {"selector": ".par", "textColor": [1, 0.9, 0.6, 1]},
            {"selector": "#item0", "scale": 1.1},
            {"selector": "Text", "fontSize": 14},
            {"selector": "Button.item:hover", "transition": 0.2})");
        CHECK(tema->rules.size() == 10);
        std::string items;
        for (int i = 0; i < 49; ++i) {
            items += std::format(R"({}{{"type": "Button", "name": "item{}", "classes": ["item"{}], "text": "Item {}"}})",
                                 i ? "," : "", i, i % 2 ? "" : R"(, "par")", i);
        }
        Scene scene;
        Entity& entity = AddDocument(scene, Doc(std::format(R"({{"type": "Panel", "name": "menu", "placement": "Absolute",
            "anchor": "TopLeft", "width": 400, "height": 1080, "children": [{}]}})", items), {tema}));
        AssetLibrary assets;
        UiCanvas canvas;
        canvas.Build(scene, assets, kScreen);
        UiDocumentData* tree = entity.Get<UiDocument>()->Tree();
        CHECK(canvas.Layout().size() == 50);
        const UiCanvasStats first = canvas.Stats();
        const uint64_t styles = tree->styleRuns, layouts = tree->layoutRuns;
        for (int i = 0; i < 5; ++i) CHECK(!canvas.Build(scene, assets, kScreen));
        CHECK(canvas.Stats().geometryRebuilds == first.geometryRebuilds && canvas.Stats().listRebuilds == first.listRebuilds);
        CHECK(tree->styleRuns == styles && tree->layoutRuns == layouts);

        // No Play, com o mouse parado num botão: depois do :hover e da transição dele, nada mais.
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        if (ui) {
            UiDocumentData* live = scene.FindByName("Menu")->Get<UiDocument>()->Tree();
            const glm::vec2 over{100, 40};
            for (int i = 0; i < 20; ++i) Frame(runtime, over, false);  // o :hover e a transição dele (0,2 s)
            UiCanvas play;
            play.Build(scene, assets, kScreen);
            const UiCanvasStats settled = play.Stats();
            const uint64_t s = live->styleRuns, l = live->layoutRuns;
            for (int i = 0; i < 5; ++i) {
                Frame(runtime, over, false);
                CHECK(!play.Build(scene, assets, kScreen));
            }
            CHECK(live->styleRuns == s && live->layoutRuns == l);
            CHECK(play.Stats().geometryRebuilds == settled.geometryRebuilds);

            // Um texto mudou: um layout e só a geometria daquele elemento.
            CHECK(ui->SetText("item5", "Outro"));
            CHECK(play.Build(scene, assets, kScreen));
            CHECK(live->layoutRuns == l + 1 && live->styleRuns == s);
            CHECK(play.Stats().geometryRebuilds == settled.geometryRebuilds + 1);
        }
        runtime.Stop();
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    return TestResult();
}
