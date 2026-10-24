#pragma once

// Superfície de navegação (Fase 34): liga o navmesh da cena e diz o tamanho do agente para o qual ele é gerado.
// Só dados: quem gera é o NavSystem (motor/ai/NavSystem.h), a partir dos Colliders estáticos (sem Rigidbody e sem
// trigger) das entidades ativas. Uma por cena; se houver mais, vale a primeira.
//
// O botão "Gerar navmesh" do Inspector grava o navmesh num arquivo .mnav do projeto e o liga ao campo `navMesh`;
// no Play, o NavSystem usa esse arquivo. Sem arquivo, o navmesh é gerado no começo de cada Play.

#include "motor/scene/Component.h"

#include <filesystem>
#include <string>

namespace motor {

// Navmesh salvo: um arquivo .mnav do projeto, pelo GUID (com o caminho junto, como plano B).
struct NavMeshData {
    std::string guid;
    std::string path;  // relativo ao projeto ("Assets/Navegacao/Navmesh.mnav")
    // Arquivo no disco, resolvido pela AssetLibrary ao ler a cena (ou ao gerar no editor). Não vai para a cena nem
    // entra na comparação: é só onde o NavSystem acha o arquivo no Play.
    std::filesystem::path file;

    bool IsEmpty() const { return guid.empty() && path.empty(); }
    bool operator==(const NavMeshData& other) const { return guid == other.guid && path == other.path; }
};

struct NavMeshSurface {
    float agentRadius = 0.5f;     // distância que o navmesh mantém das paredes
    float agentHeight = 2.0f;     // vão livre mínimo
    float agentMaxClimb = 0.4f;   // degrau mais alto
    float agentMaxSlope = 45.0f;  // graus
    float cellSize = 0.25f;       // lado do voxel: menor = mais fiel e mais lento
    float cellHeight = 0.125f;
    float minRegionArea = 4.0f;   // m²: ilhas menores somem
    NavMeshData navMesh;          // vazio: gerado a cada Play

    bool operator==(const NavMeshSurface&) const = default;
};

template <> struct FieldTraits<NavMeshData> {
    static CustomFieldType Describe();
};

} // namespace motor
