// Layout dos documentos de UI (Etapa 3 da fase Interface em documentos): coluna e linha, gap, grow e shrink,
// porcentagem, justify e align, margem e padding, posição absoluta no pai, tamanho automático pelo texto (com quebra
// de linha), recorte dos filhos e lista com rolagem pela roda do mouse. Em 1920x1080 e em retrato (1080x1920), sem
// janela nem OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Input.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/ui/Ui.h>

#include <cmath>
#include <memory>
#include <string>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const glm::vec2 kScreen{1920.0f, 1080.0f};    // escala 1
const glm::vec2 kPortrait{1080.0f, 1920.0f};  // escala 1920/1080

bool Near(float a, float b, float eps = 0.05f) { return std::abs(a - b) < eps; }
bool Near(const glm::vec2& a, const glm::vec2& b, float eps = 0.05f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

UiElement Make(UiElementType type, const char* name) {
    UiElement e = UiElement::Make(type);
    e.name = name;
    return e;
}

UiElement Box(const char* name, float w, float h) {
    UiElement e = Make(UiElementType::Panel, name);
    e.color = {1, 1, 1, 1};
    e.width = UiLength::Px(w);
    e.height = UiLength::Px(h);
    return e;
}

// Painel preso no canto superior esquerdo da tela, com tamanho fixo: o pai dos casos de teste.
UiElement Root(float w, float h) {
    UiElement root = Make(UiElementType::Panel, "raiz");
    root.placement = UiPlacement::Absolute;
    root.anchor = UiAnchor::TopLeft;
    root.width = UiLength::Px(w);
    root.height = UiLength::Px(h);
    return root;
}

struct Doc {
    Scene scene;
    Entity* entity = nullptr;

    explicit Doc(std::vector<UiElement> elements) {
        UiDocumentData data;
        data.elements = std::move(elements);
        entity = &scene.CreateEntity("Documento");
        entity->Add<UiDocument>().document.data = std::make_shared<UiDocumentData>(std::move(data));
    }
    UiDocumentData& Tree() { return *entity->Get<UiDocument>()->Tree(); }
    std::vector<UiNode> Layout(glm::vec2 screen = kScreen) { return ComputeUiLayout(scene, screen); }
};

const UiNode* NodeOf(const std::vector<UiNode>& layout, std::string_view name) {
    for (const UiNode& n : layout) {
        if (n.element && n.element->name == name) return &n;
    }
    return nullptr;
}

glm::vec2 MinOf(const std::vector<UiNode>& layout, std::string_view name) {
    const UiNode* n = NodeOf(layout, name);
    return n ? n->box.min : glm::vec2(-9999.0f);
}
glm::vec2 SizeOf(const std::vector<UiNode>& layout, std::string_view name) {
    const UiNode* n = NodeOf(layout, name);
    return n ? n->box.Size() : glm::vec2(-9999.0f);
}

} // namespace

int main() {
    SECTION("Coluna com padding e gap: menu centrado com três botões, também em retrato");
    {
        UiElement menu = Make(UiElementType::Panel, "menu");
        menu.placement = UiPlacement::Absolute;
        menu.anchor = UiAnchor::Center;
        menu.width = UiLength::Px(600);
        menu.padding = UiEdges::All(30);
        menu.gap = 16;
        for (const char* name : {"jogar", "opcoes", "sair"}) {
            UiElement b = Make(UiElementType::Button, name);
            b.height = UiLength::Px(90);
            menu.children.push_back(b);
        }
        Doc doc({menu});
        auto layout = doc.Layout();
        // Altura automática: 30 + 3 x 90 + 2 x 16 + 30 = 362.
        CHECK(Near(SizeOf(layout, "menu"), {600, 362}));
        CHECK(Near(MinOf(layout, "menu"), {660, 359}));
        CHECK(Near(MinOf(layout, "jogar"), {690, 389}));
        CHECK(Near(SizeOf(layout, "jogar"), {540, 90}));  // Stretch: a largura do conteúdo do pai
        CHECK(Near(MinOf(layout, "opcoes").y, 389 + 106));
        CHECK(Near(MinOf(layout, "sair").y, 389 + 212));
        CHECK(Near(NodeOf(layout, "jogar")->content.min.x, 690 + 24));  // padding padrão do botão

        layout = doc.Layout(kPortrait);  // retrato: tudo vezes 1920/1080, no centro da tela
        const float s = 1920.0f / 1080.0f;
        CHECK(Near(SizeOf(layout, "menu"), glm::vec2(600, 362) * s, 0.1f));
        CHECK(Near(NodeOf(layout, "menu")->box.Center(), {540, 960}, 0.1f));
        CHECK(Near(SizeOf(layout, "jogar"), glm::vec2(540, 90) * s, 0.1f));
        CHECK(Near(MinOf(layout, "sair").y - MinOf(layout, "jogar").y, 212 * s, 0.1f));
    }

    SECTION("Linha: grow reparte a sobra, porcentagem do pai, gap");
    {
        UiElement root = Root(1000, 100);
        root.direction = UiDirection::Row;
        UiElement a = Box("a", 100, 50);
        UiElement b = Box("b", 0, 50);
        b.width = {};  // automático: só o que crescer
        b.grow = 1;
        UiElement c = Box("c", 0, 50);
        c.width = {};
        c.grow = 3;
        root.children = {a, b, c};
        Doc doc({root});
        auto layout = doc.Layout();
        CHECK(Near(SizeOf(layout, "b").x, 225) && Near(SizeOf(layout, "c").x, 675));
        CHECK(Near(MinOf(layout, "c").x, 325));

        doc.Tree().Find("raiz")->gap = 20;  // 40 a menos para repartir
        layout = doc.Layout();
        CHECK(Near(SizeOf(layout, "b").x, 215) && Near(MinOf(layout, "b").x, 120));

        UiElement half = Box("metade", 0, 0);
        half.width = UiLength::Percent(50);
        half.height = UiLength::Percent(25);
        UiElement parent = Root(800, 400);
        parent.children = {half};
        Doc pct({parent});
        CHECK(Near(SizeOf(pct.Layout(), "metade"), {400, 100}));
        CHECK(Near(SizeOf(pct.Layout(kPortrait), "metade"), glm::vec2(400, 100) * (1920.0f / 1080.0f), 0.1f));
    }

    SECTION("Shrink tira a falta na proporção do tamanho; shrink 0 não encolhe");
    {
        UiElement root = Root(300, 100);
        root.direction = UiDirection::Row;
        root.children = {Box("a", 200, 50), Box("b", 200, 50)};
        Doc doc({root});
        CHECK(Near(SizeOf(doc.Layout(), "a").x, 150) && Near(SizeOf(doc.Layout(), "b").x, 150));
        doc.Tree().Find("a")->shrink = 0;
        CHECK(Near(SizeOf(doc.Layout(), "a").x, 200) && Near(SizeOf(doc.Layout(), "b").x, 100));
    }

    SECTION("Justify e align: centro, fim, espaço entre, margem");
    {
        UiElement root = Root(1000, 200);
        root.direction = UiDirection::Row;
        root.justify = UiJustify::Center;
        root.alignItems = UiAlign::Center;
        root.children = {Box("a", 100, 50), Box("b", 100, 80)};
        Doc doc({root});
        auto layout = doc.Layout();
        CHECK(Near(MinOf(layout, "a"), {400, 75}) && Near(MinOf(layout, "b"), {500, 60}));

        UiElement& r = *doc.Tree().Find("raiz");
        r.justify = UiJustify::End;
        r.alignItems = UiAlign::End;
        layout = doc.Layout();
        CHECK(Near(MinOf(layout, "a"), {800, 150}) && Near(MinOf(layout, "b"), {900, 120}));

        r.justify = UiJustify::SpaceBetween;
        r.alignItems = UiAlign::Stretch;
        doc.Tree().Find("a")->height = {};  // automático e Stretch: a altura do pai
        doc.Tree().Find("b")->alignSelf = UiAlign::Start;
        layout = doc.Layout();
        CHECK(Near(MinOf(layout, "a"), {0, 0}) && Near(SizeOf(layout, "a").y, 200));
        CHECK(Near(MinOf(layout, "b"), {900, 0}) && Near(SizeOf(layout, "b").y, 80));

        r.justify = UiJustify::SpaceAround;  // 800 de sobra: 200 em volta de cada um
        layout = doc.Layout();
        CHECK(Near(MinOf(layout, "a").x, 200) && Near(MinOf(layout, "b").x, 700));

        r.justify = UiJustify::Start;
        doc.Tree().Find("a")->margin = {10, 20, 10, 30};  // cima, direita, baixo, esquerda
        layout = doc.Layout();
        CHECK(Near(MinOf(layout, "a"), {30, 10}) && Near(SizeOf(layout, "a").y, 180));
        CHECK(Near(MinOf(layout, "b").x, 30 + 100 + 20));
    }

    SECTION("Absolute: preso no pai por âncora, fora do fluxo dos irmãos");
    {
        UiElement root = Root(400, 300);
        root.padding = UiEdges::All(20);
        UiElement badge = Box("selo", 50, 40);
        badge.placement = UiPlacement::Absolute;
        badge.anchor = UiAnchor::TopRight;
        badge.position = {-10, 10};
        root.children = {badge, Box("a", 100, 50)};
        Doc doc({root});
        const auto layout = doc.Layout();
        CHECK(Near(MinOf(layout, "selo"), {400 - 10 - 50, 10}));  // no pai inteiro, sem o padding
        CHECK(Near(MinOf(layout, "a"), {20, 20}));                // o primeiro do fluxo
        CHECK(Near(SizeOf(layout, "a").x, 100));                  // largura fixa: não estica
    }

    SECTION("Texto: tamanho automático pela fonte, quebra na largura, alinhamento vertical");
    {
        const FontData* font = DefaultUiFont();
        CHECK(font != nullptr);
        if (font) {
            UiElement title = Make(UiElementType::Text, "titulo");
            title.text = "Pontuação";
            title.fontSize = 40;
            title.wrap = false;
            title.placement = UiPlacement::Absolute;
            title.anchor = UiAnchor::TopLeft;
            Doc doc({title});
            const glm::vec2 measured = MeasureText(*font, "Pontuação", 40);
            CHECK(Near(SizeOf(doc.Layout(), "titulo"), measured));
            CHECK(Near(SizeOf(doc.Layout(kPortrait), "titulo"), MeasureText(*font, "Pontuação", 40 * 1920.0f / 1080.0f)));

            const std::string longText = "Um texto comprido que precisa de mais de uma linha para caber";
            UiElement column = Root(300, 600);
            UiElement para = Make(UiElementType::Text, "paragrafo");
            para.text = longText;
            para.fontSize = 30;
            column.children = {para, Box("depois", 10, 10)};
            Doc wrapped({column});
            const auto layout = wrapped.Layout();
            const glm::vec2 oneLine = MeasureText(*font, longText, 30);
            const glm::vec2 lines = MeasureText(*font, longText, 30, 300);
            CHECK(lines.y > oneLine.y * 2.5f && lines.x <= 300.0f);
            CHECK(Near(SizeOf(layout, "paragrafo"), {300, lines.y}));
            CHECK(Near(MinOf(layout, "depois").y, lines.y));  // o próximo vem logo abaixo

            std::vector<TextQuad> top, bottom;
            const UiBox box{{0, 0}, {300, 600}};
            LayoutText(*font, longText, 30, UiTextAlign::Left, box, top, 300, UiVerticalAlign::Top);
            LayoutText(*font, longText, 30, UiTextAlign::Left, box, bottom, 300, UiVerticalAlign::Bottom);
            CHECK(!top.empty() && top.size() == bottom.size());
            float maxX = 0.0f;
            for (const TextQuad& q : top) maxX = std::max(maxX, q.max.x);
            CHECK(maxX <= 302.0f);  // quebrou dentro da caixa
            CHECK(top.front().min.y < 30.0f && bottom.back().max.y > 570.0f);

            // Linha com texto e botão automáticos: o botão mede o rótulo mais o padding.
            UiElement row = Root(1000, 200);
            row.direction = UiDirection::Row;
            row.alignItems = UiAlign::Start;
            UiElement ok = Make(UiElementType::Button, "ok");
            ok.text = "OK";
            row.children = {ok};
            Doc buttons({row});
            const glm::vec2 label = MeasureText(*font, "OK", 36);
            CHECK(Near(SizeOf(buttons.Layout(), "ok"), label + glm::vec2(48, 24)));
        }
    }

    SECTION("Tamanho automático do painel pelos filhos");
    {
        UiElement bar = Make(UiElementType::Panel, "barra");
        bar.placement = UiPlacement::Absolute;
        bar.anchor = UiAnchor::BottomLeft;
        bar.direction = UiDirection::Row;
        bar.gap = 10;
        bar.padding = UiEdges::Symmetric(5, 8);
        bar.alignItems = UiAlign::Start;
        bar.children = {Box("a", 40, 40), Box("b", 60, 20), Box("c", 30, 30)};
        Doc doc({bar});
        const auto layout = doc.Layout();
        CHECK(Near(SizeOf(layout, "barra"), {8 + 40 + 10 + 60 + 10 + 30 + 8, 5 + 40 + 5}));
        CHECK(Near(MinOf(layout, "barra"), {0, 1080 - 50}));
    }

    SECTION("Recorte: overflow Hidden corta os filhos no desenho e no clique");
    {
        UiElement window = Root(200, 100);
        window.overflow = UiOverflow::Hidden;
        UiElement big = Make(UiElementType::Button, "grande");
        big.width = UiLength::Px(400);
        big.height = UiLength::Px(300);
        big.alignSelf = UiAlign::Start;
        big.shrink = 0;  // passa do pai de verdade (senão encolheria para caber)
        window.children = {big};
        Doc doc({window});
        const auto layout = doc.Layout();
        const UiNode* node = NodeOf(layout, "grande");
        CHECK(node && node->clipped && node->clip == (UiBox{{0, 0}, {200, 100}}));
        CHECK(!NodeOf(layout, "raiz")->clipped);  // o próprio painel não é recortado, só os filhos
        CHECK(UiHitTestNode(doc.scene, layout, {150, 50}) == node);
        CHECK(UiHitTestNode(doc.scene, layout, {300, 200}) == nullptr);  // no botão, mas fora do recorte

        AssetLibrary assets;
        UiCanvas canvas;
        canvas.Build(doc.scene, assets, kScreen);
        bool inside = true;
        for (const Vertex2D& v : canvas.List().Vertices()) {
            if (v.position.x > 200.01f || v.position.y > 100.01f) inside = false;
        }
        // Só o fundo, cortado em 200x100: o rótulo fica no centro do botão (200, 150), fora do recorte.
        CHECK(inside && canvas.Stats().quads == 1);
    }

    SECTION("Lista com rolagem: scrollMax, scroll preso na faixa e roda do mouse pelo UiSystem");
    {
        UiElement list = Root(400, 300);
        list.overflow = UiOverflow::Scroll;
        for (int i = 0; i < 10; ++i) {
            UiElement item = Make(UiElementType::Button, "");
            item.name = "item" + std::to_string(i);
            item.height = UiLength::Px(100);
            list.children.push_back(item);
        }
        Doc doc({list});
        auto layout = doc.Layout();
        UiElement& l = *doc.Tree().Find("raiz");
        CHECK(Near(l.scrollMax, 700));
        l.scroll = 250;
        layout = doc.Layout();
        CHECK(Near(MinOf(layout, "item0").y, -250) && Near(MinOf(layout, "item3").y, 50));
        CHECK(NodeOf(layout, "item0")->clipped);
        l.scroll = 9999;
        layout = doc.Layout();
        CHECK(Near(l.scroll, 700) && Near(MinOf(layout, "item9").y, 200));

        l.scroll = 0;
        SceneRuntime runtime(doc.scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui != nullptr);
        if (ui) {
            UiElement* live = ui->FindElement("raiz");
            auto frame = [&](glm::vec2 mouse, float wheel) {
                Input::BeginFrame(kDt);
                if (wheel != 0.0f) Input::AddScroll(wheel);
                ui->SetPointer({kScreen, mouse, true, false});
                runtime.FixedUpdate(kDt);
                runtime.Update(kDt);
            };
            frame({200, 150}, 0);
            frame({200, 150}, -2);  // dois entalhes para baixo
            CHECK(live && Near(live->scroll, 2 * kUiScrollStep));
            frame({200, 150}, 1);
            CHECK(Near(live->scroll, kUiScrollStep));
            frame({600, 150}, -5);  // fora da lista: nada
            CHECK(Near(live->scroll, kUiScrollStep));
            frame({200, 150}, -100);
            CHECK(Near(live->scroll, 700));  // não passa do fim
            frame({200, 150}, 0);
            CHECK(ui->IsVisible("item9"));
            Input::BeginFrame(kDt);
        }
        runtime.Stop();
    }

    SECTION("Formato: campos novos gravados e lidos; versão 1 continua por âncora");
    {
        UiElement e = Make(UiElementType::Panel, "p");
        e.width = UiLength::Percent(50);
        e.height = UiLength::Px(120);
        e.margin = {1, 2, 3, 4};
        e.padding = UiEdges::All(8);
        e.grow = 2;
        e.shrink = 0;
        e.alignSelf = UiAlign::Center;
        e.direction = UiDirection::Row;
        e.justify = UiJustify::SpaceBetween;
        e.alignItems = UiAlign::End;
        e.gap = 6;
        e.overflow = UiOverflow::Scroll;
        UiElement t = Make(UiElementType::Text, "t");
        t.verticalAlign = UiVerticalAlign::Bottom;
        t.wrap = false;
        e.children = {t};
        UiDocumentData doc;
        doc.elements = {e};
        const std::string json = UiDocumentToJson(doc);
        CHECK(json.find("\"version\": 2") != std::string::npos);
        CHECK(json.find("\"50%\"") != std::string::npos && json.find("\"padding\": 8") != std::string::npos);
        CHECK(json.find("\"margin\": [") != std::string::npos);
        CHECK(json.find("\"anchor\"") == std::string::npos);  // no fluxo: sem âncora
        UiDocumentData back;
        std::string error;
        CHECK(UiDocumentFromJson(json, back, &error));
        CHECK(back.SameContent(doc));

        CHECK(UiDocumentFromJson(R"({"format": "motor-ui", "version": 2, "elements": [
            {"type": "Panel", "width": "25%", "height": "auto", "padding": [10, 20], "margin": "x"},
            {"type": "Button", "width": "300px", "size": [1, 2]}]})", back, &error));
        CHECK(back.elements[0].width == UiLength::Percent(25) && back.elements[0].height.IsAuto());
        CHECK(back.elements[0].padding == UiEdges::Symmetric(10, 20) && back.elements[0].margin == UiEdges{});
        CHECK(back.elements[1].width == UiLength::Px(300) && back.elements[1].height == UiLength::Px(2));
        CHECK(back.elements[1].placement == UiPlacement::Flow);

        // Versão 1: o painel de 600x400 no centro e o botão embaixo, como na Etapa 2.
        CHECK(UiDocumentFromJson(R"({"format": "motor-ui", "version": 1, "elements": [
            {"type": "Panel", "name": "menu", "anchor": "Center", "size": [600, 400], "color": [0, 0, 0, 1],
             "children": [{"type": "Button", "name": "jogar", "anchor": "Bottom", "position": [0, -40],
                           "size": [320, 90], "text": "Jogar"}]}]})", back, &error));
        Doc v1(back.elements);
        const auto layout = v1.Layout();
        CHECK(Near(MinOf(layout, "menu"), {660, 340}) && Near(SizeOf(layout, "menu"), {600, 400}));
        CHECK(Near(MinOf(layout, "jogar"), {800, 740 - 40 - 90}) && Near(SizeOf(layout, "jogar"), {320, 90}));
    }

    return TestResult();
}
