#pragma once

// Malha de navegação (Fase 34): por onde um agente de um certo tamanho consegue andar. Gerada pelo Recast a partir
// de triângulos no mundo (NavGeometry) e consultada pelo Detour: busca de caminho, ponto mais próximo no navmesh e
// raio rente ao chão. Recast e Detour ficam escondidos no .cpp: scripts e editor só veem esta interface.
//
// Num script (o NavSystem gera o navmesh da cena no começo do Play, a partir do componente NavMeshSurface):
//   if (auto* nav = GetSystem<motor::NavSystem>()) {
//       std::vector<glm::vec3> caminho;
//       if (nav->FindPath(inicio, alvo, caminho)) { /* cantos do caminho, do começo ao fim */ }
//   }

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace motor {

// Parâmetros da geração. Medidas em metros e graus; os do agente decidem por onde ele cabe.
struct NavMeshBuildSettings {
    float agentRadius = 0.5f;     // o navmesh fica a essa distância das paredes
    float agentHeight = 2.0f;     // vão livre mínimo acima do chão
    float agentMaxClimb = 0.4f;   // degrau mais alto que o agente sobe
    float agentMaxSlope = 45.0f;  // rampa mais íngreme que o agente sobe
    // Lado da célula (voxel) no plano e altura dela: menores = navmesh mais fiel e geração mais lenta. A Recast
    // recomenda metade do raio do agente no plano e metade disso na altura.
    float cellSize = 0.25f;
    float cellHeight = 0.125f;
    float minRegionArea = 4.0f;   // m²: ilhas menores que isso somem (topo de mesa, de caixa)
    float maxEdgeLength = 12.0f;  // m: bordas mais longas são divididas
    float maxEdgeError = 1.3f;    // células: quanto o contorno pode se afastar da borda real
    float detailSampleDistance = 6.0f;  // células: amostras de altura (0 = sem detalhe de altura)
    float detailMaxError = 1.0f;        // células
};

// Ligação entre dois pontos do navmesh que não se ligam andando (pulo, escada; componente NavMeshLink), no mundo.
struct NavLink {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    float radius = 0.5f;        // as pontas podem ficar até essa distância do navmesh
    bool bidirectional = true;

    bool operator==(const NavLink&) const = default;
};

// Áreas: 0 é o chão comum; 1 a 15, as das caixas (componente NavMeshArea). Máscara: um bit por área.
inline constexpr uint16_t kNavAllAreas = 0xffff;

// Caixa (os oito cantos, no mundo) que marca o navmesh dentro dela com uma área.
struct NavAreaBox {
    std::array<glm::vec3, 8> corners{};
    int area = 1;

    bool operator==(const NavAreaBox&) const = default;
};

// Triângulos no mundo de onde o navmesh nasce (chão, paredes, rampas). Sentido anti-horário visto de fora, como
// nas malhas do Motor: a normal cross(b - a, c - a) aponta para fora, e só a face virada para cima vira chão.
struct NavGeometry {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;  // três por triângulo
    std::vector<NavLink> links;     // ligações (pontas a mais de `radius` do navmesh ficam de fora)
    std::vector<NavAreaBox> areas;  // caixas de área (a última vence onde se cruzam)

    void AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    bool Empty() const { return indices.size() < 3; }
    size_t TriangleCount() const { return indices.size() / 3; }
};

struct NavRaycastHit {
    glm::vec3 point{0.0f};   // onde o raio saiu do navmesh (parede, borda)
    glm::vec3 normal{0.0f};  // da borda atingida, no plano
    float fraction = 0.0f;   // 0..1 do caminho entre o início e o fim
};

class NavMesh {
public:
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // Gera o navmesh. nullptr se não houver onde andar ou se a Recast falhar (`error` diz por quê).
    static std::unique_ptr<NavMesh> Build(const NavGeometry& geometry, const NavMeshBuildSettings& settings,
                                          std::string* error = nullptr);

    // Arquivo .mnav (gerado no editor, lido no Play): os blocos do Detour, os parâmetros e a impressão digital da
    // geometria de onde saiu. Binário: "MNAV", versão (uint32) e o resto em little-endian. Load devolve nullptr se o
    // arquivo não abre, não é .mnav ou é de uma versão mais nova (`error` diz qual).
    bool Save(const std::filesystem::path& file, std::string* error = nullptr) const;
    static std::unique_ptr<NavMesh> Load(const std::filesystem::path& file, std::string* error = nullptr);

    // Impressão digital da entrada (triângulos e parâmetros): mudou o cenário, muda o número. Serve para avisar que
    // um navmesh salvo ficou velho.
    static uint64_t SourceHash(const NavGeometry& geometry, const NavMeshBuildSettings& settings);
    uint64_t Source() const { return m_source; }

    // Ponto do navmesh mais perto de `position`, procurando numa caixa de meia-medida `extents` em volta dele.
    std::optional<glm::vec3> SamplePosition(const glm::vec3& position,
                                            const glm::vec3& extents = glm::vec3(2.0f, 4.0f, 2.0f)) const;

    // Caminho de `start` até `end` (os dois levados ao navmesh por SamplePosition): os cantos, do começo ao fim,
    // já na ordem de andar. false se algum dos dois está longe do navmesh. Se o fim não é alcançável (outra ilha),
    // o caminho vai até o ponto alcançável mais perto dele e `partial` (se pedido) fica true.
    // Só passa pelas áreas de `areaMask` e pesa cada área pelo custo dela (SetAreaCost).
    bool FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& corners,
                  bool* partial = nullptr, uint16_t areaMask = kNavAllAreas) const;
    // Quanto vale andar 1 m na área (a partir de 1; o padrão é 1). Vale para as consultas deste navmesh e para a
    // multidão que anda nele (NavSystem), sem gerar de novo.
    void SetAreaCost(int area, float cost);
    float AreaCost(int area) const;
    // Área do polígono embaixo de `position` (-1 se está longe do navmesh).
    int AreaAt(const glm::vec3& position) const;

    // Anda em linha reta de `start` para `end` pelo navmesh. nullopt se chega sem sair dele; senão, onde parou.
    std::optional<NavRaycastHit> Raycast(const glm::vec3& start, const glm::vec3& end) const;

    // Comprimento de um caminho (soma dos trechos).
    static float PathLength(const std::vector<glm::vec3>& corners);

    int PolygonCount() const;
    // Ligações que entraram no navmesh (as de pontas longe dele ficam de fora).
    std::vector<NavLink> Links() const;
    // Triângulos dos polígonos (com o detalhe de altura), para desenhar o navmesh no editor.
    NavGeometry DebugTriangles() const;
    const NavMeshBuildSettings& Settings() const { return m_settings; }

private:
    NavMesh();
    friend class NavCrowd;  // a multidão do NavSystem anda sobre o mesmo navmesh do Detour
    struct Detour;
    std::unique_ptr<Detour> m_detour;
    NavMeshBuildSettings m_settings;
    uint64_t m_source = 0;  // SourceHash da entrada
};

} // namespace motor
