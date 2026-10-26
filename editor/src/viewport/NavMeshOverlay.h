#pragma once

#include <motor/renderer/Material.h>

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

namespace motor {
class Mesh;
class Renderer;
class SceneRuntime;
} // namespace motor

namespace editor {

struct EditorContext;

// Navmesh no viewport (Fase 34): a superfície azul translúcida por onde os agentes andam, com as arestas dos
// triângulos, o raio de cada agente (verde), as ligações (roxo, em arco; seta se só valem num sentido) e as caixas
// das Áreas de navegação (laranja, com nome e custo), e no Play
// os caminhos dos agentes (amarelo) e de quem cada um desvia (laranja, Fase 35). Fora do Play mostra o .mnav da Superfície de navegação (relido
// quando o arquivo muda, como depois do botão Gerar); no Play, o navmesh que o NavSystem está usando.
class NavMeshOverlay {
public:
    NavMeshOverlay();
    ~NavMeshOverlay();
    NavMeshOverlay(const NavMeshOverlay&) = delete;
    NavMeshOverlay& operator=(const NavMeshOverlay&) = delete;

    // Escolhe o navmesh a mostrar neste quadro e refaz a malha se ele mudou. `runtime`: o do Play (nulo fora dele).
    void Update(const EditorContext& ctx, motor::SceneRuntime* runtime);
    // A malha no quadro da cena (com profundidade: some atrás das paredes). Chamado entre BeginScene e EndScene.
    void Submit(motor::Renderer& renderer) const;
    // Raio dos agentes e ligações; no Play, também os caminhos e os vizinhos. Em linhas por cima da imagem.
    // `project`: mundo -> pixels do viewport.
    void DrawPaths(const EditorContext& ctx, motor::SceneRuntime* runtime, ImDrawList* dl, const ImVec2& origin,
                   const std::function<std::optional<ImVec2>(const glm::vec3&)>& project) const;
    bool HasMesh() const { return m_mesh != nullptr; }

private:
    void SetTriangles(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);

    std::shared_ptr<motor::Mesh> m_mesh;
    motor::Material m_material;  // guardado aqui: o Renderer só guarda a referência até o EndScene
    // O que está na malha agora: arquivo e data dele, ou runtime e versão do navmesh do Play.
    std::filesystem::path m_file;
    std::filesystem::file_time_type m_fileTime{};
    const motor::SceneRuntime* m_runtime = nullptr;
    uint64_t m_version = 0;
};

} // namespace editor
