#pragma once

// Pesos das camadas de todos os blocos de um terreno (Fase 28): uma textura de array com uma camada por bloco
// (resolução x resolução, RGBA8 = peso das camadas 1 a 4, com mipmaps) e um índice que diz a camada de cada bloco.
// Com os pesos de todos numa textura só, os blocos dividem um material e saem num draw indireto (antes era um
// material e um draw por bloco, por causa da textura de pesos de cada um).
//
// Criada e mudada sem OpenGL (TerrainData, testes); os Handle sobem para a GPU o que mudou, na thread do render.

#include <glm/vec4.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace motor {

class TerrainWeights {
public:
    explicit TerrainWeights(int resolution);
    ~TerrainWeights();
    TerrainWeights(const TerrainWeights&) = delete;
    TerrainWeights& operator=(const TerrainWeights&) = delete;

    int Resolution() const { return m_resolution; }
    size_t BlockCount() const { return m_layers.size(); }

    // Pesos do bloco (x, z): resolução² * 4 bytes, linha a linha (z, depois x). Bloco novo ganha uma camada (a de um
    // que saiu, se houver); um que já existe só é marcado para subir de novo.
    void SetBlock(int x, int z, std::span<const uint8_t> rgba);
    void RemoveBlock(int x, int z);
    bool HasBlock(int x, int z) const { return m_layers.contains({x, z}); }
    std::vector<std::pair<int, int>> BlockKeys() const;
    // Camada do bloco no array; -1 se não existe (o mesmo valor que o índice guarda para ele).
    int LayerOf(int x, int z) const;
    // Retângulo do índice, em blocos: (x0, z0, largura, altura). Cobre todos os blocos.
    glm::ivec4 IndexRect() const;

    // OpenGL (thread do render): sobem o que mudou desde a última vez. Array: sampler2DArray linear com mipmaps;
    // índice: R32F com a camada de cada bloco do retângulo (-1 onde não há bloco), lido com texelFetch.
    unsigned int ArrayHandle() const;
    unsigned int IndexHandle() const;

private:
    void Upload() const;
    void RebuildIndex() const;

    int m_resolution;
    std::map<std::pair<int, int>, int> m_layers;  // (x, z) -> camada
    std::vector<std::vector<uint8_t>> m_pixels;   // por camada (vazia = livre)
    std::vector<int> m_free;                      // camadas livres para os próximos blocos
    mutable std::vector<int> m_dirtyLayers;       // camadas para subir
    mutable bool m_indexDirty = true;
    mutable unsigned int m_array = 0, m_index = 0;
    mutable int m_arrayCapacity = 0;              // camadas alocadas na GPU
    mutable glm::ivec4 m_rect{0, 0, 1, 1};
    mutable bool m_warnedLimit = false;
};

} // namespace motor
