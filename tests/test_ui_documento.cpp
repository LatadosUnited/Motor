// Documento de UI (motor/ui/UiDocument.h): o formato .mui gravado e lido de volta, a árvore de elementos no layout,
// no hit test e na lista de desenho, o componente UiDocument na cena (com o arquivo pelo GUID) e os cliques e a API
// dos scripts pelo UiSystem, pelo nome do elemento. Sem janela nem OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/ui/Ui.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <string>

using namespace motor;
namespace fs = std::filesystem;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const glm::vec2 kScreen{1920.0f, 1080.0f};  // escala 1: pixels da referência

bool Near(const glm::vec2& a, const glm::vec2& b) { return std::abs(a.x - b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f; }

UiElement Element(UiElementType type, const char* name, UiAnchor anchor, glm::vec2 position, glm::vec2 size) {
    UiElement e = UiElement::Make(type);
    e.name = name;
    e.placement = UiPlacement::Absolute;  // como na Etapa 2: tudo por âncora
    e.anchor = anchor;
    e.position = position;
    e.width = UiLength::Px(size.x);
    e.height = UiLength::Px(size.y);
    return e;
}

// Menu de pausa: painel escuro no centro com título e dois botões; HUD de pontos no canto.
UiDocumentData Menu() {
    UiDocumentData doc;
    UiElement panel = Element(UiElementType::Panel, "menu", UiAnchor::Center, {0, 0}, {600, 400});
    panel.color = {0, 0, 0, 0.6f};
    panel.classes = {"tela", "escura"};
    UiElement title = Element(UiElementType::Text, "titulo", UiAnchor::Top, {0, 30}, {600, 80});
    title.text = "Pausa — Ação";
    title.fontSize = 56.0f;
    title.align = UiTextAlign::Left;
    UiElement play = Element(UiElementType::Button, "jogar", UiAnchor::Bottom, {-170, -40}, {300, 90});
    play.text = "Continuar";
    play.classes = {"menu"};
    play.hoverColor = {1, 0, 0, 1};
    UiElement quit = Element(UiElementType::Button, "sair", UiAnchor::Bottom, {170, -40}, {300, 90});
    quit.text = "Sair";
    quit.interactable = false;
    panel.children = {title, play, quit};
    UiElement points = Element(UiElementType::Text, "pontos", UiAnchor::TopLeft, {40, 40}, {400, 60});
    points.text = "Pontos: 0";
    points.textColor = {1, 0.8f, 0.2f, 1};
    doc.elements = {panel, points};
    return doc;
}

Entity& AddDocument(Scene& scene, const char* name, const UiDocumentData& data, EntityId parent = kNullEntity) {
    Entity& e = scene.CreateEntity(name, parent);
    e.Add<UiDocument>().document.data = std::make_shared<UiDocumentData>(data);
    return e;
}

const UiNode* NodeOf(const std::vector<UiNode>& layout, std::string_view element) {
    for (const UiNode& n : layout) {
        if (n.element && n.element->name == element) return &n;
    }
    return nullptr;
}

void Frame(SceneRuntime& runtime, glm::vec2 mouse, bool down) {
    if (auto* ui = runtime.GetSystem<UiSystem>()) ui->SetPointer({kScreen, mouse, true, down});
    runtime.FixedUpdate(kDt);
    runtime.Update(kDt);
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_ui_documento_tmp");

    SECTION(".mui gravado e lido de volta igual");
    {
        const UiDocumentData menu = Menu();
        const fs::path file = dir / "Jogo Ação" / "Assets" / "UI" / "Menu de pausa.mui";
        fs::create_directories(file.parent_path());
        std::string error;
        CHECK(SaveUiDocument(file, menu, &error));
        UiDocumentData loaded;
        CHECK(LoadUiDocument(file, loaded, &error));
        CHECK(loaded.SameContent(menu));
        CHECK(AssetTypeFromPath(file) == AssetType::UiDocument);

        const std::string json = UiDocumentToJson(menu);
        CHECK(json.find("\"format\": \"motor-ui\"") != std::string::npos);
        CHECK(json.find("\"hoverColor\"") != std::string::npos);    // diferente do padrão: gravado
        CHECK(json.find("\"normalColor\"") == std::string::npos);   // igual ao padrão: não
        CHECK(json.find("\"interactable\": false") != std::string::npos);
        CHECK(json.find("Pausa — Ação") != std::string::npos);
        CHECK(json.find("hovered") == std::string::npos);  // estado do jogo fica fora

        UiDocumentData changed = menu;
        changed.Find("jogar")->text = "Jogar";
        CHECK(!changed.SameContent(menu));
        changed = menu;
        changed.Find("jogar")->hovered = true;  // estado não conta
        CHECK(changed.SameContent(menu));
    }

    SECTION("Leitura tolerante: padrões, tipo desconhecido, versão e formato");
    {
        UiDocumentData doc;
        std::vector<std::string> warnings;
        std::string error;
        CHECK(UiDocumentFromJson(R"({"format": "motor-ui", "version": 1, "elements": [
            {"type": "Button", "name": "ok"},
            {"type": "Slider", "name": "volume", "children": [{"type": "Text"}]},
            {"type": "Image", "image": "Assets/Texturas/x.png", "color": [1, 0, 0]},
            42]})", doc, &error, &warnings));
        CHECK(doc.elements.size() == 3);
        const UiElement& ok = doc.elements[0];
        CHECK(ok.type == UiElementType::Button && ok.text == "Botão" &&
              ok.width == UiLength::Px(200) && ok.height == UiLength::Px(60));  // versão 1: 200x60 por âncora
        CHECK(ok.placement == UiPlacement::Absolute && !doc.elements[0].wrap);
        CHECK(ok.anchor == UiAnchor::Center && ok.interactable && ok.visible);
        const UiElement& slider = doc.elements[1];
        CHECK(slider.type == UiElementType::Panel && slider.color.a == 0.0f);  // desconhecido: painel transparente
        CHECK(slider.children.size() == 1 && slider.children[0].text == "Texto");
        CHECK(doc.elements[2].image.path == "Assets/Texturas/x.png");
        CHECK(doc.elements[2].color == glm::vec4(1.0f));  // cor com 3 números: ignorada
        CHECK(warnings.size() == 2);  // o tipo e o item que não é objeto

        CHECK(UiDocumentFromJson(R"({"format": "motor-ui"})", doc, &error));  // sem versão nem elementos
        CHECK(doc.elements.empty());
        CHECK(!UiDocumentFromJson(R"({"format": "motor-ui", "version": 99})", doc, &error));
        CHECK(error.find("99") != std::string::npos);
        CHECK(!UiDocumentFromJson(R"({"format": "motor-scene"})", doc, &error));
        CHECK(!UiDocumentFromJson("{", doc, &error));
    }

    SECTION("Layout: elementos dentro do pai, esconder, ordem e entidade do documento");
    {
        Scene scene;
        Entity& doc = AddDocument(scene, "Menu", Menu());
        std::vector<UiNode> layout = ComputeUiLayout(scene, kScreen);
        CHECK(layout.size() == 5);  // o documento em si não gera nó
        const UiNode* menu = NodeOf(layout, "menu");
        const UiNode* play = NodeOf(layout, "jogar");
        const UiNode* title = NodeOf(layout, "titulo");
        CHECK(menu && menu->entity == doc.id && Near(menu->box.min, {660, 340}) && Near(menu->box.max, {1260, 740}));
        CHECK(title && Near(title->box.min, {660, 370}) && title->depth == menu->depth + 1);
        // Embaixo, no centro do painel, 170 para a esquerda e 40 para cima.
        CHECK(play && Near(play->box.min, {660 + 300 - 170 - 150, 740 - 40 - 90}));
        CHECK(layout.back().element->name == "pontos");  // o último da lista desenha por cima

        layout = ComputeUiLayout(scene, {960, 540});  // metade da tela: tudo pela metade
        CHECK(Near(NodeOf(layout, "menu")->box.min, {330, 170}));

        doc.Get<UiDocument>()->Tree()->Find("menu")->visible = false;  // esconde o painel e os filhos
        CHECK(ComputeUiLayout(scene, kScreen).size() == 1);
        doc.Get<UiDocument>()->visible = false;
        CHECK(ComputeUiLayout(scene, kScreen).empty());
        doc.Get<UiDocument>()->visible = true;
        doc.active = false;
        CHECK(ComputeUiLayout(scene, kScreen).empty());
        doc.active = true;

        // Dois documentos: order maior por cima; filho de um UiRect ocupa o retângulo dele.
        Entity& frame = scene.CreateEntity("Moldura");
        UiRect& rect = frame.Add<UiRect>();
        rect.anchor = UiAnchor::TopLeft;
        rect.size = {960, 540};
        rect.order = 5;
        UiDocumentData small;
        small.elements = {Element(UiElementType::Image, "fundo", UiAnchor::Stretch, {0, 0}, {0, 0})};
        AddDocument(scene, "Dentro", small, frame.id);
        layout = ComputeUiLayout(scene, kScreen);
        const UiNode* fundo = NodeOf(layout, "fundo");
        CHECK(fundo && Near(fundo->box.min, {0, 0}) && Near(fundo->box.max, {960, 540}));
        CHECK(layout.back().element == fundo->element);  // a moldura tem ordem 5
        CHECK(IsUiEntity(doc));
    }

    SECTION("Hit test: botão e imagem bloqueiam; texto e painel transparente não");
    {
        Scene scene;
        UiDocumentData data = Menu();
        data.elements[0].color.a = 0.0f;  // painel só agrupa
        Entity& doc = AddDocument(scene, "Menu", data);
        const std::vector<UiNode> layout = ComputeUiLayout(scene, kScreen);
        const UiNode* hit = UiHitTestNode(scene, layout, {800, 670});  // em cima do "jogar"
        CHECK(hit && hit->element && hit->element->name == "jogar" && hit->entity == doc.id);
        CHECK(UiHitTestNode(scene, layout, {960, 410}) == nullptr);  // título sobre painel transparente
        CHECK(UiHitTest(scene, layout, {960, 410}, UiHitFilter::Any) == doc.id);  // o editor seleciona o documento
        CHECK(UiHitTestNode(scene, layout, {960, 410}, UiHitFilter::Any)->element->name == "titulo");
        doc.Get<UiDocument>()->Tree()->Find("menu")->color.a = 0.5f;
        const UiNode* panel = UiHitTestNode(scene, ComputeUiLayout(scene, kScreen), {960, 410});
        CHECK(panel && panel->element->name == "menu");
    }

    SECTION("Lista de desenho: documento no mesmo lote, geometria refeita só quando muda");
    {
        AssetLibrary assets;
        Scene scene;
        Entity& doc = AddDocument(scene, "Menu", Menu());
        UiCanvas canvas;
        CHECK(canvas.Build(scene, assets, kScreen));
        const uint64_t built = canvas.Stats().geometryRebuilds;
        CHECK(built == 5);
        CHECK(canvas.Stats().batches == 1);
        CHECK(!canvas.Build(scene, assets, kScreen));
        CHECK(canvas.Stats().geometryRebuilds == built);

        doc.Get<UiDocument>()->Tree()->Find("pontos")->text = "Pontos: 10";
        CHECK(canvas.Build(scene, assets, kScreen));
        CHECK(canvas.Stats().geometryRebuilds == built + 1);  // só o texto que mudou
        doc.Get<UiDocument>()->Tree()->Find("jogar")->hovered = true;  // cor do estado
        CHECK(canvas.Build(scene, assets, kScreen));
        CHECK(canvas.Stats().geometryRebuilds == built + 2);
    }

    SECTION("Cópia do componente (Play, desfazer) começa do documento, sem levar o estado");
    {
        Scene scene;
        Entity& doc = AddDocument(scene, "Menu", Menu());
        doc.Get<UiDocument>()->Tree()->Find("pontos")->text = "Pontos: 99";
        Scene copy = scene;
        const Entity* copied = copy.FindByName("Menu");
        CHECK(copied && copied->Get<UiDocument>()->Tree()->Find("pontos")->text == "Pontos: 0");
        copied->Get<UiDocument>()->Tree()->Find("pontos")->text = "Pontos: 5";
        CHECK(doc.Get<UiDocument>()->Tree()->Find("pontos")->text == "Pontos: 99");  // o original não muda
        CHECK(copy == scene);  // o estado não conta para o desfazer
    }

    SECTION("Clique e API dos scripts pelo nome do elemento");
    {
        Scene scene;
        AddDocument(scene, "Menu", Menu());
        Entity& legacy = scene.CreateEntity("pontos");  // entidade com o mesmo nome ganha do elemento
        legacy.Add<UiRect>();
        legacy.Add<UiText>().text = "Entidade";
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui != nullptr);
        if (ui) {
            const glm::vec2 play{800, 670}, quit{1120, 670}, outside{100, 1000};
            Frame(runtime, play, false);
            UiElement* jogar = ui->FindElement("jogar");
            CHECK(jogar && jogar->hovered && !jogar->pressed);
            CHECK(ui->HoveredButton() != kNullEntity);
            CHECK(runtime.PointerOverUi());  // o clique é da interface, não do jogo
            Frame(runtime, play, true);
            CHECK(jogar->pressed && !ui->WasClicked("jogar"));
            Frame(runtime, play, false);
            CHECK(ui->WasClicked("jogar") && jogar->clicked);
            Frame(runtime, play, false);
            CHECK(!ui->WasClicked("jogar"));  // vale um quadro

            // Começa no botão e solta fora: não é clique.
            Frame(runtime, play, true);
            Frame(runtime, outside, false);
            CHECK(!ui->WasClicked("jogar"));
            // Botão não clicável.
            Frame(runtime, quit, true);
            Frame(runtime, quit, false);
            CHECK(!ui->WasClicked("sair") && !ui->FindElement("sair")->hovered);

            CHECK(ui->GetText("pontos") == "Entidade");
            CHECK(ui->SetText("titulo", "Fim de jogo"));
            CHECK(ui->GetText("titulo") == "Fim de jogo");
            CHECK(!ui->SetText("nao-existe", "x"));
            CHECK(ui->IsVisible("jogar"));
            CHECK(ui->SetVisible("menu", false));
            Frame(runtime, outside, false);
            CHECK(!ui->IsVisible("jogar"));  // o pai escondido esconde o filho
            Frame(runtime, play, true);
            Frame(runtime, play, false);
            CHECK(!ui->WasClicked("jogar"));
            UiBox box;
            CHECK(!ui->ScreenRect(*ui->FindElement("jogar"), box));
        }
        runtime.Stop();
    }

    SECTION("UiDocument na cena: arquivo pelo GUID, lido junto com a cena e por caminho (MCP)");
    {
        const fs::path project = dir / "Projeto Ação";
        const fs::path file = project / "Assets" / "UI" / "Menu.mui";
        fs::create_directories(file.parent_path());
        CHECK(SaveUiDocument(file, Menu()));
        AssetLibrary assets;
        assets.SetRoot(project);
        assets.Database().SetAssetsDir(project / "Assets");
        const std::string guid = assets.Database().GuidFor(file);
        CHECK(AssetDatabase::IsValidGuid(guid));

        Scene scene;
        Entity& e = scene.CreateEntity("HUD");
        UiDocument& component = e.Add<UiDocument>();
        SetUiDocumentFile(component.document, "Assets/UI/Menu.mui", assets);
        CHECK(component.document.guid == guid && component.document.data);
        component.order = 2;

        const std::string json = SceneSerializer::ToJson(scene, assets);
        CHECK(json.find(guid) != std::string::npos && json.find("UiDocument") != std::string::npos);
        Scene loaded;
        std::string error;
        CHECK(SceneSerializer::FromJson(json, loaded, assets, &error));
        CHECK(loaded == scene);
        const UiDocument* back = loaded.FindByName("HUD")->Get<UiDocument>();
        CHECK(back && back->order == 2 && back->document.data && back->document.data->SameContent(Menu()));

        // Arquivo movido (com o .meta): o GUID acha o lugar novo.
        const fs::path moved = project / "Assets" / "Telas" / "Pausa.mui";
        fs::create_directories(moved.parent_path());
        fs::rename(file, moved);
        fs::rename(AssetDatabase::MetaPathFor(file), AssetDatabase::MetaPathFor(moved));
        assets.Database().Refresh();  // varre de novo
        Scene relocated;
        CHECK(SceneSerializer::FromJson(json, relocated, assets, &error));
        const UiDocument* r = relocated.FindByName("HUD")->Get<UiDocument>();
        CHECK(r && r->document.path == "Assets/Telas/Pausa.mui" && r->document.data);

        // Pelo caminho só (como o component_set do MCP) e arquivo que não existe (aviso, sem dados).
        UiDocumentRef ref;
        const CustomFieldType traits = FieldTraits<UiDocumentRef>::Describe();
        CHECK(traits.fromJson("\"Assets/Telas/Pausa.mui\"", &ref, assets, &error));
        CHECK(ref.guid == guid && ref.data);
        CHECK(traits.fromJson("\"Assets/UI/Nada.mui\"", &ref, assets, &error));
        CHECK(!ref.data && !ref.IsEmpty());
        CHECK(traits.fromJson("null", &ref, assets, &error) && ref.IsEmpty());
        CHECK(!traits.fromJson("5", &ref, assets, &error));

        // Mudou o arquivo: Recarregar lê de novo, e a árvore viva é refeita.
        UiDocumentData edited = Menu();
        edited.Find("titulo")->text = "Novo";
        CHECK(SaveUiDocument(moved, edited));
        UiDocument& live = *relocated.FindByName("HUD")->Get<UiDocument>();
        CHECK(live.Tree()->Find("titulo")->text == "Pausa — Ação");
        ReloadUiDocument(live.document, assets);
        CHECK(live.Tree()->Find("titulo")->text == "Novo");

        WriteText(project / "Assets" / "UI" / "Quebrado.mui", "não é json");
        SetUiDocumentFile(ref, "Assets/UI/Quebrado.mui", assets);
        CHECK(!ref.data && ref.path == "Assets/UI/Quebrado.mui");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    return TestResult();
}
