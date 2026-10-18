// Interface do jogo (Fase 9, camada ui/): âncoras e layout, fontes SDF e texto, clique em botão pelo UiSystem,
// a API dos scripts (texto, mostrar/esconder, botão clicado, trocar de cena), a serialização dos componentes
// e o cache de geometria da lista de desenho. Sem janela nem OpenGL.
#include "TestUtil.h"

#include "play/PlayMode.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/Script.h>
#include <motor/ui/Ui.h>

#include <cmath>
#include <format>
#include <fstream>
#include <string>

using namespace motor;
namespace fs = std::filesystem;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const glm::vec2 kScreen{1920.0f, 1080.0f};  // escala 1: pixels da referência

bool Near(float a, float b, float eps = 0.01f) { return std::abs(a - b) < eps; }
bool Near(const glm::vec2& a, const glm::vec2& b, float eps = 0.01f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

Entity& AddElement(Scene& scene, const char* name, UiAnchor anchor, glm::vec2 position, glm::vec2 size,
                   EntityId parent = kNullEntity) {
    Entity& e = scene.CreateEntity(name, parent);
    UiRect& rect = e.Add<UiRect>();
    rect.anchor = anchor;
    rect.position = position;
    rect.size = size;
    return e;
}

const UiNode* NodeOf(const std::vector<UiNode>& layout, EntityId id) {
    for (const UiNode& n : layout) {
        if (n.entity == id) return &n;
    }
    return nullptr;
}

// Mouse do "viewport" para o UiSystem do runtime, e um frame do jogo.
void Frame(SceneRuntime& runtime, glm::vec2 mouse, bool down, glm::vec2 screen = kScreen) {
    if (auto* ui = runtime.GetSystem<UiSystem>()) ui->SetPointer({screen, mouse, true, down});
    runtime.FixedUpdate(kDt);
    runtime.Update(kDt);
}

void Click(SceneRuntime& runtime, glm::vec2 at) {
    Frame(runtime, at, true);
    Frame(runtime, at, false);
}

// --- Scripts do teste (registrados aqui, sem DLL) ------------------------------------

// HUD: mostra vida e pontos; a vida cai com o tempo e cada segundo vale um ponto.
struct HudTeste : Script {
    int vida = 3;
    int pontos = 0;
    float relogio = 0.0f;

    void OnStart() override { Atualizar(); }
    void OnUpdate(float dt) override {
        relogio += dt;
        if (relogio >= 1.0f) {
            relogio -= 1.0f;
            ++pontos;
            if (vida > 0) --vida;
            Atualizar();
        }
        if (auto* ui = GetSystem<UiSystem>(); ui && vida == 0) ui->SetVisible("Fim", true);
    }
    void Atualizar() {
        if (auto* ui = GetSystem<UiSystem>()) {
            ui->SetText("Vida", std::format("Vida: {}", vida));
            ui->SetText("Pontos", std::format("Pontos: {}", pontos));
        }
    }
};

// Menu: o botão "Jogar" troca de cena; "Sair" só conta.
struct MenuTeste : Script {
    int saidas = 0;
    void OnUpdate(float) override {
        auto* ui = GetSystem<UiSystem>();
        if (!ui) return;
        if (ui->WasClicked("Jogar")) LoadScene("Fase");
        if (ui->WasClicked("Sair")) ++saidas;
    }
};

void RegisterTestScripts() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<HudTeste>("HudTeste", "HUD de teste").Field("vida", "Vida", &HudTeste::vida);
    registry.Register<MenuTeste>("MenuTeste", "Menu de teste");
}

template <class T>
T* ScriptOf(Scene& scene, EntityId id) {
    Entity* e = scene.Find(id);
    return e ? e->Get<T>() : nullptr;
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_ui_tmp");
    RegisterTestScripts();

    SECTION("Componentes de UI registrados");
    for (const char* type : {"UiRect", "UiImage", "UiText", "UiButton"}) {
        const ComponentType* t = ComponentRegistry::Instance().Find(type);
        CHECK(t != nullptr);
    }
    CHECK(RegisteredRuntimeSystems().size() >= 1);
    {
        bool found = false;
        for (const RuntimeSystemInfo& s : RegisteredRuntimeSystems()) {
            if (*s.cppType == typeid(UiSystem)) found = s.order == kSystemOrderUi;
        }
        CHECK(found);
    }

    SECTION("Âncoras: cantos, centro e esticar");
    {
        const UiBox screen{{0.0f, 0.0f}, kScreen};
        auto box = [&](UiAnchor a, glm::vec2 pos, glm::vec2 size, float scale = 1.0f) {
            return ResolveUiRect(UiRect{a, pos, size, 0, true}, screen, scale);
        };
        CHECK(Near(box(UiAnchor::TopLeft, {40, 30}, {200, 60}).min, {40, 30}));
        CHECK(Near(box(UiAnchor::TopRight, {-40, 30}, {200, 60}).min, {1920 - 40 - 200, 30}));
        CHECK(Near(box(UiAnchor::BottomRight, {0, 0}, {200, 60}).max, {1920, 1080}));
        CHECK(Near(box(UiAnchor::Center, {0, 0}, {200, 60}).min, {860, 510}));
        CHECK(Near(box(UiAnchor::Bottom, {0, -20}, {300, 100}).min, {810, 960}));
        CHECK(Near(box(UiAnchor::Left, {10, 0}, {100, 100}).min, {10, 490}));
        // Esticar: posição = margens esquerda/cima, tamanho = direita/baixo.
        const UiBox stretched = box(UiAnchor::Stretch, {10, 20}, {30, 40});
        CHECK(Near(stretched.min, {10, 20}) && Near(stretched.max, {1890, 1040}));
        // Escala da tela: metade da altura de referência = metade de tudo, também dos deslocamentos.
        const UiBox half = ResolveUiRect(UiRect{UiAnchor::TopRight, {-40, 30}, {200, 60}, 0, true},
                                         UiBox{{0, 0}, {960, 540}}, UiScale({960, 540}));
        CHECK(Near(UiScale({960, 540}), 0.5f));
        CHECK(Near(half.min, {960 - 20 - 100, 15}) && Near(half.Size(), {100, 30}));
    }

    SECTION("Layout: filhos dentro do pai, ordem, esconder e entidades comuns no meio");
    {
        Scene scene;
        Entity& panel = AddElement(scene, "Painel", UiAnchor::Center, {0, 0}, {600, 400});
        panel.Add<UiImage>();
        Entity& child = AddElement(scene, "Filho", UiAnchor::TopLeft, {20, 10}, {100, 50}, panel.id);
        Entity& front = AddElement(scene, "Frente", UiAnchor::TopLeft, {0, 0}, {50, 50}, panel.id);
        front.Get<UiRect>()->order = 5;
        front.Add<UiImage>();
        Entity& back = AddElement(scene, "Atrás", UiAnchor::TopLeft, {0, 0}, {50, 50}, panel.id);
        Entity& group = scene.CreateEntity("Grupo comum", panel.id);  // sem UI: os filhos usam o painel
        Entity& grandchild = AddElement(scene, "Neto", UiAnchor::BottomRight, {0, 0}, {10, 10}, group.id);
        Entity& world = scene.CreateEntity("Cubo");  // fora da UI
        (void)world;

        std::vector<UiNode> layout = ComputeUiLayout(scene, kScreen);
        CHECK(layout.size() == 5);
        const UiNode* p = NodeOf(layout, panel.id);
        const UiNode* c = NodeOf(layout, child.id);
        const UiNode* g = NodeOf(layout, grandchild.id);
        CHECK(p && Near(p->box.min, {660, 340}));
        CHECK(c && Near(c->box.min, {680, 350}) && c->depth == 1);
        CHECK(g && Near(g->box.max, p->box.max));
        CHECK(!layout.empty() && layout.front().entity == panel.id);  // pai antes dos filhos
        // Irmãos por ordem: "Frente" (5) depois de "Atrás" (0), mesmo criada antes.
        size_t iFront = 0, iBack = 0;
        for (size_t i = 0; i < layout.size(); ++i) {
            if (layout[i].entity == front.id) iFront = i;
            if (layout[i].entity == back.id) iBack = i;
        }
        CHECK(iFront > iBack);

        panel.Get<UiRect>()->visible = false;  // esconde o painel e tudo dentro
        CHECK(ComputeUiLayout(scene, kScreen).empty());
        panel.Get<UiRect>()->visible = true;
        scene.Find(group.id)->active = false;  // entidade inativa também esconde a sub-árvore
        CHECK(ComputeUiLayout(scene, kScreen).size() == 4);
        scene.Find(group.id)->active = true;

        // Clique: o de cima ganha; texto sozinho não bloqueia.
        Entity& label = AddElement(scene, "Rótulo", UiAnchor::Center, {0, 0}, {600, 400});
        label.Add<UiText>();
        layout = ComputeUiLayout(scene, kScreen);
        CHECK(UiHitTest(scene, layout, {960, 540}) == panel.id);
        CHECK(UiHitTest(scene, layout, {960, 540}, UiHitFilter::Any) == label.id);
        CHECK(UiHitTest(scene, layout, {700, 360}) == front.id);
        CHECK(UiHitTest(scene, layout, {5, 5}) == kNullEntity);
    }

    SECTION("Fonte SDF: atlas, acentos e medidas");
    FontData font;
    {
        std::string error;
        CHECK(fs::exists(DefaultFontPath()));
        CHECK(font.LoadFile(DefaultFontPath(), &error));
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        CHECK(font.IsLoaded());
        CHECK(font.AtlasWidth() > 0 && font.AtlasHeight() > 0);
        CHECK(font.AtlasPixels().size() == static_cast<size_t>(font.AtlasWidth() * font.AtlasHeight() * 4));
        for (char32_t c : {U'A', U'z', U'ç', U'ã', U'É', U'?', U'…'}) CHECK(font.HasGlyph(c));
        CHECK(!font.HasGlyph(U'中'));
        CHECK(font.Glyph(U'中').advance == font.Glyph(U'?').advance);  // sem o glifo: '?'
        // A distância: dentro do glifo perto de 1, fora 0, e borda suave (valores intermediários).
        const FontGlyph& o = font.Glyph(U'O');
        CHECK(o.size.x > FontData::kSdfPadding * 2 && o.uvMax.x > o.uvMin.x);
        int intermediate = 0;
        for (size_t i = 3; i < font.AtlasPixels().size(); i += 4) {
            const uint8_t a = font.AtlasPixels()[i];
            if (a > 20 && a < 235) ++intermediate;
        }
        CHECK(intermediate > 1000);
        // Bloco branco opaco para os retângulos sólidos.
        const glm::vec2 w = font.WhiteUv();
        const int wx = static_cast<int>(w.x * font.AtlasWidth()), wy = static_cast<int>(w.y * font.AtlasHeight());
        CHECK(font.AtlasPixels()[(static_cast<size_t>(wy) * font.AtlasWidth() + wx) * 4 + 3] == 255);

        // Medidas escalam com o tamanho; duas linhas = mais alto; UTF-8 com acentos.
        const glm::vec2 small = MeasureText(font, "Pontuação", 20.0f);
        const glm::vec2 big = MeasureText(font, "Pontuação", 40.0f);
        CHECK(small.x > 0.0f && Near(big.x, small.x * 2.0f, 0.5f));
        CHECK(MeasureText(font, "A\nB", 20.0f).y > small.y * 1.5f);
        std::string_view bad = "\xC3";
        CHECK(NextCodepoint(bad) == 0xFFFD && bad.empty());
        std::string_view acc = "ção";
        CHECK(NextCodepoint(acc) == U'ç' && NextCodepoint(acc) == U'ã' && NextCodepoint(acc) == U'o');

        // Alinhamento dentro da caixa.
        const UiBox box{{100, 100}, {500, 200}};
        std::vector<TextQuad> quads;
        LayoutText(font, "Vida: 3", 30.0f, UiTextAlign::Left, box, quads);
        CHECK(quads.size() == 6);  // o espaço não tem quadrado
        CHECK(!quads.empty() && quads.front().min.x >= 95.0f && quads.front().min.x < 110.0f);
        LayoutText(font, "Vida: 3", 30.0f, UiTextAlign::Right, box, quads);
        CHECK(!quads.empty() && quads.back().max.x <= 505.0f && quads.back().max.x > 480.0f);
        LayoutText(font, "Vida: 3", 30.0f, UiTextAlign::Center, box, quads);
        float minY = 1e9f, maxY = -1e9f;
        for (const TextQuad& q : quads) {
            minY = std::min(minY, q.min.y);
            maxY = std::max(maxY, q.max.y);
        }
        CHECK(minY > 100.0f && maxY < 200.0f);  // centrado na vertical, dentro da caixa
    }

    SECTION("Lista de desenho: um lote e geometria refeita só quando muda");
    {
        AssetLibrary assets;
        Scene scene;
        Entity& panel = AddElement(scene, "Painel", UiAnchor::TopLeft, {20, 20}, {400, 120});
        panel.Add<UiImage>().color = {0, 0, 0, 0.5f};
        Entity& vida = AddElement(scene, "Vida", UiAnchor::TopLeft, {10, 10}, {380, 50}, panel.id);
        vida.Add<UiText>().text = "Vida: 3";
        Entity& button = AddElement(scene, "Jogar", UiAnchor::Center, {0, 0}, {320, 90});
        button.Add<UiButton>();
        button.Add<UiText>().text = "Jogar";

        UiCanvas canvas;
        CHECK(canvas.Build(scene, assets, kScreen));
        const uint64_t built = canvas.Stats().geometryRebuilds;
        CHECK(built == 3);
        // Fundo do painel, 5 glifos de "Vida: 3" (o espaço não desenha), fundo do botão e 5 glifos.
        CHECK(canvas.Stats().quads == 1 + 6 + 1 + 5);
        CHECK(canvas.Stats().batches == 1);  // retângulos sólidos e texto na mesma textura (atlas)

        bool changedAgain = false;
        for (int i = 0; i < 10; ++i) changedAgain = canvas.Build(scene, assets, kScreen) || changedAgain;
        CHECK(!changedAgain);
        CHECK(canvas.Stats().geometryRebuilds == built);  // nada mudou: nada refeito
        CHECK(canvas.Stats().listRebuilds == 1);

        vida.Get<UiText>()->text = "Vida: 2";
        CHECK(canvas.Build(scene, assets, kScreen));
        CHECK(canvas.Stats().geometryRebuilds == built + 1);  // só o texto que mudou

        panel.Get<UiRect>()->position.x = 40.0f;  // move o painel: ele e o filho
        CHECK(canvas.Build(scene, assets, kScreen));
        CHECK(canvas.Stats().geometryRebuilds == built + 3);

        CHECK(canvas.Build(scene, assets, {960, 540}));  // tela nova: tudo
        CHECK(canvas.Stats().geometryRebuilds == built + 6);
        CHECK(!canvas.Build(scene, assets, {960, 540}));
    }

    SECTION("Clique em botão pelo UiSystem");
    {
        Scene scene;
        Entity& button = AddElement(scene, "Jogar", UiAnchor::Center, {0, 0}, {320, 90});
        button.Add<UiButton>();
        const EntityId id = button.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui != nullptr);
        if (ui) {
            const glm::vec2 inside{960, 540}, outside{100, 100};
            Frame(runtime, inside, false);
            CHECK(ui->HoveredButton() == id && scene.Find(id)->Get<UiButton>()->hovered);
            Frame(runtime, inside, true);
            CHECK(scene.Find(id)->Get<UiButton>()->pressed && !ui->WasClicked(id));
            Frame(runtime, inside, false);  // soltou em cima: clique
            CHECK(ui->WasClicked(id) && ui->WasClicked("Jogar"));
            Frame(runtime, inside, false);
            CHECK(!ui->WasClicked(id));  // só um frame

            Frame(runtime, inside, true);  // aperta em cima e solta fora: não vale
            Frame(runtime, outside, false);
            CHECK(!ui->WasClicked(id));
            Frame(runtime, outside, true);  // aperta fora e solta em cima: não vale
            Frame(runtime, inside, false);
            CHECK(!ui->WasClicked(id));

            // Imagem por cima bloqueia; botão não clicável não clica.
            Entity& cover = AddElement(scene, "Capa", UiAnchor::Center, {0, 0}, {1000, 1000});
            cover.Add<UiImage>();
            Click(runtime, inside);
            CHECK(!ui->WasClicked(id) && ui->HoveredButton() == kNullEntity);
            scene.DestroyEntity(cover.id);
            scene.Find(id)->Get<UiButton>()->interactable = false;
            Click(runtime, inside);
            CHECK(!ui->WasClicked(id));
            scene.Find(id)->Get<UiButton>()->interactable = true;

            // Mouse fora da imagem do jogo (inside = false): nada.
            ui->SetPointer({kScreen, inside, false, true});
            runtime.Update(kDt);
            ui->SetPointer({kScreen, inside, false, false});
            runtime.Update(kDt);
            CHECK(!ui->WasClicked(id));

            // Tela menor (viewport do editor): o botão escala junto.
            Frame(runtime, {480, 270}, true, {960, 540});
            Frame(runtime, {480, 270}, false, {960, 540});
            CHECK(ui->WasClicked(id));
            UiBox rect;
            CHECK(ui->ScreenRect(id, rect) && Near(rect.Size(), {160, 45}));
        }
        runtime.Stop();
        CHECK(runtime.GetSystem<UiSystem>() == nullptr);
    }

    SECTION("API de script: vida e pontos no HUD, mostrar e esconder");
    {
        Scene scene;
        Entity& vida = AddElement(scene, "Vida", UiAnchor::TopLeft, {40, 40}, {300, 60});
        vida.Add<UiText>();
        Entity& pontos = AddElement(scene, "Pontos", UiAnchor::TopRight, {-40, 40}, {300, 60});
        pontos.Add<UiText>();
        Entity& fim = scene.CreateEntity("Fim");  // sem UiRect: SetVisible cria
        fim.Add<UiText>().text = "Fim de jogo";
        Entity& hud = scene.CreateEntity("HUD");
        hud.Add<HudTeste>();

        SceneRuntime runtime(scene);
        runtime.Start();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui && ui->GetText("Vida") == "Vida: 3" && ui->GetText("Pontos") == "Pontos: 0");
        if (ui) {
            CHECK(ui->SetVisible("Fim", false) && !ui->IsVisible("Fim"));
            CHECK(scene.Find(fim.id)->Has<UiRect>());
            for (int i = 0; i < 125; ++i) Frame(runtime, {0, 0}, false);  // ~2 s
            CHECK(ui->GetText("Vida") == "Vida: 1" && ui->GetText("Pontos") == "Pontos: 2");
            for (int i = 0; i < 60; ++i) Frame(runtime, {0, 0}, false);
            CHECK(ui->GetText("Vida") == "Vida: 0" && ui->IsVisible("Fim"));
            CHECK(!ui->SetText("Não existe", "x") && !ui->SetText("HUD", "sem UiText"));
            CHECK(!ui->WasClicked("Vida"));  // não é botão
        }
        runtime.Stop();
    }

    SECTION("Serialização dos componentes de UI (com fonte do projeto pelo GUID)");
    {
        const fs::path project = dir / "Jogo Ação";
        const fs::path fontFile = project / "Assets" / "Fontes" / "Título.ttf";
        fs::create_directories(fontFile.parent_path());
        fs::copy_file(DefaultFontPath(), fontFile);
        AssetLibrary assets;
        assets.SetRoot(project);
        assets.Database().SetAssetsDir(project / "Assets");
        const std::string guid = assets.Database().GuidFor(fontFile);
        CHECK(AssetTypeFromPath(fontFile) == AssetType::Font && AssetDatabase::IsValidGuid(guid));

        Scene scene;
        Entity& e = AddElement(scene, "Título", UiAnchor::BottomRight, {-12, -34}, {500, 80});
        e.Get<UiRect>()->order = 3;
        e.Get<UiRect>()->visible = false;
        UiText& text = e.Add<UiText>();
        text.text = "Olá, mundo!\nLinha 2";
        text.font = {guid, assets.KeyForFile(fontFile)};
        text.fontSize = 48.0f;
        text.color = {1, 0.5f, 0.25f, 1};
        text.align = UiTextAlign::Right;
        UiButton& button = e.Add<UiButton>();
        button.hoverColor = {0.1f, 0.2f, 0.3f, 0.4f};
        button.interactable = false;
        button.clicked = true;  // estado do jogo: não vai para o arquivo
        e.Add<UiImage>().color = {0.5f, 0.5f, 0.5f, 1};

        const std::string json = SceneSerializer::ToJson(scene, assets);
        CHECK(json.find("\"BottomRight\"") != std::string::npos && json.find("\"Right\"") != std::string::npos);
        CHECK(json.find(guid) != std::string::npos);
        CHECK(json.find("clicked") == std::string::npos);
        Scene loaded;
        std::string error;
        CHECK(SceneSerializer::FromJson(json, loaded, assets, &error));
        CHECK(loaded == scene);  // todos os campos registrados iguais (o desfazer compara assim)
        const Entity* le = loaded.FindByName("Título");
        CHECK(le && le->Get<UiText>() && le->Get<UiText>()->font.guid == guid);
        CHECK(le && le->Get<UiRect>()->anchor == UiAnchor::BottomRight && !le->Get<UiRect>()->visible);
        CHECK(le && !le->Get<UiButton>()->clicked);

        // A fonte mudou de pasta (com o .meta): o GUID acha e o caminho é atualizado ao ler.
        const fs::path moved = project / "Assets" / "Outra" / "Título.ttf";
        fs::create_directories(moved.parent_path());
        fs::rename(fontFile, moved);
        fs::rename(AssetDatabase::MetaPathFor(fontFile), AssetDatabase::MetaPathFor(moved));
        assets.Database().Refresh();
        Scene reloaded;
        CHECK(SceneSerializer::FromJson(json, reloaded, assets, &error));
        const Entity* re = reloaded.FindByName("Título");
        CHECK(re && re->Get<UiText>()->font.path == "Assets/Outra/Título.ttf");

        // A fonte do projeto carrega no canvas; uma que sumiu cai na padrão.
        UiCanvas canvas;
        const FontData* custom = canvas.FontFor(re->Get<UiText>()->font, assets);
        const FontData* fallback = canvas.FontFor(UiFont{"0123456789abcdef0123456789abcdef", "Assets/Nada.ttf"}, assets);
        CHECK(custom && custom->IsLoaded() && fallback && fallback->IsLoaded());
        CHECK(custom != fallback && fallback == canvas.FontFor(UiFont{}, assets));

        // Leitura tolerante: valores estranhos ficam no padrão.
        Scene odd;
        CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 5, "entities": [
            {"id": 1, "name": "X", "components": [
                {"type": "UiRect", "anchor": "Diagonal", "size": [10, 20]},
                {"type": "UiText", "align": 2, "font": 42}]}]})", odd, assets, &error));
        const Entity* oe = odd.FindByName("X");
        CHECK(oe && oe->Get<UiRect>()->anchor == UiAnchor::Center && Near(oe->Get<UiRect>()->size, {10, 20}));
        CHECK(oe && oe->Get<UiText>()->align == UiTextAlign::Right && oe->Get<UiText>()->font.IsDefault());

        // Só o caminho (o que um agente manda pelo MCP): o GUID vem do .meta.
        const CustomFieldType fontType = FieldTraits<UiFont>::Describe();
        UiFont byPath;
        CHECK(fontType.fromJson(R"("Assets/Outra/Título.ttf")", &byPath, assets, nullptr));
        CHECK(byPath.guid == guid && byPath.path == "Assets/Outra/Título.ttf");
        UiFont missing;
        CHECK(fontType.fromJson(R"("Assets/Nada.ttf")", &missing, assets, nullptr));
        CHECK(missing.guid.empty() && missing.path == "Assets/Nada.ttf");
        std::string fontErr;
        CHECK(!fontType.fromJson("123", &byPath, assets, &fontErr) && !fontErr.empty());
    }

    SECTION("Menu com botão Jogar troca de cena (LoadScene + PlayMode)");
    {
        const fs::path project = dir / "Jogo Menu";
        fs::create_directories(project / "Assets" / "Cenas");
        AssetLibrary assets;
        assets.SetRoot(project);
        assets.Database().SetAssetsDir(project / "Assets");

        // Fase: o HUD.
        Scene fase;
        AddElement(fase, "Vida", UiAnchor::TopLeft, {40, 40}, {300, 60}).Add<UiText>();
        AddElement(fase, "Pontos", UiAnchor::TopRight, {-40, 40}, {300, 60}).Add<UiText>();
        fase.CreateEntity("HUD").Add<HudTeste>();
        CHECK(SceneSerializer::Save(project / "Assets" / "Cenas" / "Fase.mscene", fase, assets));
        WriteText(project / "Assets" / "Cenas" / "Velha.txt", "não é cena");

        CHECK(FindSceneFile(assets, "Fase") == project / "Assets" / "Cenas" / "Fase.mscene");
        CHECK(FindSceneFile(assets, "Assets/Cenas/Fase.mscene") == project / "Assets" / "Cenas" / "Fase.mscene");
        CHECK(FindSceneFile(assets, "Assets/Cenas/Fase") == project / "Assets" / "Cenas" / "Fase.mscene");
        CHECK(FindSceneFile(assets, "Velha").empty() && FindSceneFile(assets, "").empty());

        // Menu: botões Jogar e Sair e o script do menu.
        Scene menu;
        Entity& jogar = AddElement(menu, "Jogar", UiAnchor::Center, {0, -60}, {320, 90});
        jogar.Add<UiButton>();
        AddElement(menu, "Texto", UiAnchor::Stretch, {0, 0}, {0, 0}, jogar.id).Add<UiText>().text = "Jogar";
        AddElement(menu, "Sair", UiAnchor::Center, {0, 60}, {320, 90}).Add<UiButton>();
        const EntityId menuScript = menu.CreateEntity("Menu").id;
        menu.Find(menuScript)->Add<MenuTeste>();
        const Scene menuBefore = menu;

        editor::PlayMode play;
        std::string lastRequest;
        play.SetSceneLoader([&](const std::string& request, Scene& out, std::string& error) {
            lastRequest = request;
            const fs::path file = FindSceneFile(assets, request);
            if (file.empty()) {
                error = "cena não encontrada";
                return false;
            }
            return SceneSerializer::Load(file, out, assets, &error);
        });
        EntityId selected = kNullEntity;
        play.Play(menu, selected);
        auto frame = [&](glm::vec2 mouse, bool down) {
            if (auto* ui = play.Runtime()->GetSystem<UiSystem>()) ui->SetPointer({kScreen, mouse, true, down});
            play.FixedUpdate(kDt);
            play.Update(kDt);
        };
        const glm::vec2 sair{960, 600}, botaoJogar{960, 480};
        frame(sair, true);
        frame(sair, false);
        frame(sair, false);  // o script lê no frame seguinte ao clique
        CHECK(ScriptOf<MenuTeste>(menu, menuScript) && ScriptOf<MenuTeste>(menu, menuScript)->saidas == 1);
        CHECK(play.ScenesLoaded() == 0);

        frame(botaoJogar, true);
        frame(botaoJogar, false);
        frame(botaoJogar, false);
        CHECK(lastRequest == "Fase" && play.ScenesLoaded() == 1);
        CHECK(menu.FindByName("HUD") != nullptr && menu.FindByName("Jogar") == nullptr);  // a cena do editor agora é a Fase
        for (int i = 0; i < 70; ++i) frame({0, 0}, false);
        UiSystem* ui = play.Runtime() ? play.Runtime()->GetSystem<UiSystem>() : nullptr;
        CHECK(ui && ui->GetText("Vida") == "Vida: 2" && ui->GetText("Pontos") == "Pontos: 1");

        // Pedido de uma cena que não existe: registra o erro e o jogo continua na cena atual.
        if (play.Runtime()) play.Runtime()->RequestSceneLoad("Inexistente");
        frame({0, 0}, false);
        CHECK(lastRequest == "Inexistente" && play.ScenesLoaded() == 1 && menu.FindByName("HUD") != nullptr);

        play.Stop(menu, selected);  // Parar volta ao menu de antes do Play
        CHECK(menu == menuBefore);
    }

    return TestResult();
}
