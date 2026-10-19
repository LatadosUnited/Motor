// Viewport do editor sem janela: modo dos contornos de collider (Selecionado / Todos / Nenhum) e a gravação
// dele no editor.json.
#include "TestUtil.h"

#include "EditorSettings.h"
#include "viewport/ColliderOverlay.h"

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
    CHECK(EditorSettings::Load().viewportColliders == ColliderOverlay::Selected);  // primeira execução
    {
        EditorSettings settings;
        settings.viewportColliders = ColliderOverlay::None;
        settings.Save();
    }
    CHECK(EditorSettings::Load().viewportColliders == ColliderOverlay::None);
    // editor.json de antes desta versão (sem o campo): padrão Selecionado.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << R"({"projectGridView": true})";
    const EditorSettings old = EditorSettings::Load();
    CHECK(old.viewportColliders == ColliderOverlay::Selected);
    CHECK(old.projectGridView);

    std::error_code ec;
    fs::remove_all(base, ec);
    return TestResult();
}
