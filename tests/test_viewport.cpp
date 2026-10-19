// Viewport do editor sem janela: modo dos contornos de collider (Selecionado / Todos / Nenhum) e a gravação
// dele no editor.json.
#include "TestUtil.h"

#include "EditorSettings.h"
#include "viewport/ColliderOverlay.h"
#include "viewport/ViewportPrefs.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace editor;

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_viewport_tmp");

    SECTION("Quem tem o contorno desenhado em cada modo");
    // Selecionado: só o objeto selecionado, ativo ou não.
    CHECK(ShouldDrawCollider(ColliderOverlay::Selected, true, true));
    CHECK(ShouldDrawCollider(ColliderOverlay::Selected, true, false));
    CHECK(!ShouldDrawCollider(ColliderOverlay::Selected, false, true));
    // Todos: os ativos e o selecionado mesmo inativo.
    CHECK(ShouldDrawCollider(ColliderOverlay::All, false, true));
    CHECK(ShouldDrawCollider(ColliderOverlay::All, true, false));
    CHECK(!ShouldDrawCollider(ColliderOverlay::All, false, false));
    // Nenhum: nem o selecionado.
    CHECK(!ShouldDrawCollider(ColliderOverlay::None, true, true));

    SECTION("O botão alterna Selecionado, Todos e Nenhum");
    CHECK(NextColliderOverlay(ColliderOverlay::Selected) == ColliderOverlay::All);
    CHECK(NextColliderOverlay(ColliderOverlay::All) == ColliderOverlay::None);
    CHECK(NextColliderOverlay(ColliderOverlay::None) == ColliderOverlay::Selected);

    SECTION("Nomes no editor.json; desconhecido volta ao padrão");
    for (ColliderOverlay m : {ColliderOverlay::Selected, ColliderOverlay::All, ColliderOverlay::None}) {
        CHECK(ParseColliderOverlay(ColliderOverlayName(m)) == m);
    }
    CHECK(ParseColliderOverlay("") == ColliderOverlay::Selected);
    CHECK(ParseColliderOverlay("qualquer") == ColliderOverlay::Selected);

    SECTION("Lembrado entre aberturas do editor");
    const fs::path file = base / "Configuração Ação" / "editor.json";
    EditorSettings::UseFile(file);
    CHECK(EditorSettings::Load().viewport.colliders == ColliderOverlay::Selected);  // primeira execução
    {
        EditorSettings settings;
        settings.viewport.colliders = ColliderOverlay::None;
        settings.Save();
    }
    CHECK(EditorSettings::Load().viewport.colliders == ColliderOverlay::None);
    // editor.json de antes desta versão (sem o campo): padrão Selecionado.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << R"({"projectGridView": true})";
    const EditorSettings old = EditorSettings::Load();
    CHECK(old.viewport.colliders == ColliderOverlay::Selected);
    CHECK(old.projectGridView);

    SECTION("Encaixe e grade lembrados; valores absurdos voltam para a faixa");
    {
        EditorSettings settings;
        settings.viewport.snap = true;
        settings.viewport.snapMove = 0.25f;
        settings.viewport.snapRotate = 45.0f;
        settings.viewport.snapScale = 0.5f;
        settings.viewport.grid = false;
        settings.viewport.gridHeight = 2.0f;
        settings.viewport.gridFine = true;
        settings.playSideBySide = true;
        settings.Save();
        CHECK(EditorSettings::Load().playSideBySide);  // Cena e Jogo lado a lado
        const ViewportPrefs v = EditorSettings::Load().viewport;
        CHECK(v.snap && v.snapMove == 0.25f && v.snapRotate == 45.0f && v.snapScale == 0.5f);
        CHECK(!v.grid && v.gridHeight == 2.0f && v.gridFine);
    }
    std::ofstream(file, std::ios::binary | std::ios::trunc) << R"({"snapMove": -3, "snapRotate": 9999})";
    {
        const ViewportPrefs v = EditorSettings::Load().viewport;
        CHECK(v.snapMove == 0.01f && v.snapRotate == 180.0f);
        CHECK(!v.grid && !v.snap && !v.gridFine);  // campos ausentes: padrão (grade desligada)
    }

    SECTION("Mover com encaixe vai para a grade só nos eixos mexidos");
    {
        const glm::vec3 before{0.3f, 1.1f, -0.7f};
        // Arrastou só o X: X na grade de 0,5; Y e Z ficam onde estavam (fora da grade).
        const glm::vec3 x = SnapToGrid(before, {0.3f + 0.5f, 1.1f, -0.7f}, 0.5f, 0.0f);
        CHECK(x == glm::vec3(1.0f, 1.1f, -0.7f));
        // Y conta a partir da altura da grade.
        const glm::vec3 y = SnapToGrid(before, {0.3f, 1.6f, -0.7f}, 0.5f, 0.2f);
        CHECK(std::abs(y.y - 1.7f) < 1e-5f && y.x == 0.3f);
        CHECK(SnapToGrid(before, {-1.26f, 1.1f, 3.1f}, 0.25f, 0.0f) == glm::vec3(-1.25f, 1.1f, 3.0f));
        CHECK(SnapToGrid(before, before, 0.5f, 0.0f) == before);  // nada mexido
    }

    SECTION("Malha e posição da grade");
    {
        const auto mesh = CreateGridMesh(2);
        CHECK(mesh->IndexCount() == 5 * 2 * 3);  // 5 linhas em X e 5 em Z, um triângulo (a, b, b) cada
        CHECK(GridCenter({12.4f, 30.0f, -7.6f}, 5.0f, 1.0f) == glm::vec3(10.0f, 1.0f, -10.0f));
    }

    std::error_code ec;
    fs::remove_all(base, ec);
    return TestResult();
}
