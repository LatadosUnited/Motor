// Níveis de detalhe (Fase 24): a malha certa para cada tamanho na tela, níveis fora de ordem e as primitivas com os
// níveis gerados (menos triângulos a cada nível, mesmos limites). Sem janela: a Mesh só vai para a GPU no Draw.
#include "TestUtil.h"

#include <motor/renderer/Mesh.h>

using namespace motor;

int main() {
    SECTION("escolha pelo tamanho na tela");
    {
        auto base = Mesh::CreateCube();
        auto media = Mesh::CreateCube();
        auto baixa = Mesh::CreateCube();
        base->SetLods({{baixa, 0.1f}, {media, 0.4f}, {nullptr, 0.05f}});  // fora de ordem, um sem malha
        CHECK(base->Lods().size() == 2);
        CHECK(&base->ForScreenSize(1.0f) == base.get());
        CHECK(&base->ForScreenSize(0.4f) == base.get());  // o nível vale abaixo do valor
        CHECK(&base->ForScreenSize(0.39f) == media.get());
        CHECK(&base->ForScreenSize(0.1f) == media.get());
        CHECK(&base->ForScreenSize(0.05f) == baixa.get());
        CHECK(&base->ForScreenSize(0.0f) == baixa.get());
        CHECK(&media->ForScreenSize(0.0f) == media.get());  // sem níveis: sempre ela
    }

    SECTION("primitivas com níveis gerados");
    for (const auto& mesh : {Mesh::CreateSphere(), Mesh::CreateCylinder(), Mesh::CreateCapsule(), Mesh::CreateCone()}) {
        CHECK(mesh->Lods().size() == 2);
        uint32_t last = mesh->IndexCount();
        for (const Mesh::Lod& lod : mesh->Lods()) {
            CHECK(lod.mesh->IndexCount() < last);
            CHECK(lod.mesh->Lods().empty());
            CHECK(glm::length(lod.mesh->BoundsMax() - mesh->BoundsMax()) < 0.05f);
            last = lod.mesh->IndexCount();
        }
    }
    CHECK(Mesh::CreateCube()->Lods().empty());
    CHECK(Mesh::CreateSphere(16, 8)->Lods().size() == 0);  // 8 divisões já é o mínimo: sem níveis

    return TestResult();
}
