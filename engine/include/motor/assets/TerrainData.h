#pragma once

// Fase 28: dados do terreno (mapa de altura em blocos). Sem OpenGL: dá para criar, mudar, gravar e ler sem janela.
//
// O terreno é uma grade de blocos quadrados de `blockSize` metros (32 = a célula da SpatialGrid), cada um com
// `resolution` x `resolution` alturas igualmente espaçadas. Os blocos vizinhos repetem a linha de amostras da borda:
// cada bloco fica completo sozinho, e o streaming (Mundo grande) pode carregar um sem os outros. O bloco (x, z) cobre,
// no espaço do terreno, de (x, z) * blockSize a (x + 1, z + 1) * blockSize; a altura é o Y.
//
// Arquivos: "Ilha.mterrain" (cabeçalho JSON: tamanho do bloco, resolução e a lista de blocos) e a pasta
// "Ilha.blocos" ao lado, com um arquivo por bloco ("3_-2.mtbloco": as alturas, binário). A pasta acompanha o
// .mterrain no painel Projeto e na exportação (AssetCompanions, AssetLoaders.h).

#include <glm/vec3.hpp>

#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace motor {

class Mesh;

inline constexpr const char* kTerrainFileExtension = ".mterrain";
inline constexpr const char* kTerrainBlocksFolderSuffix = ".blocos";
inline constexpr const char* kTerrainBlockExtension = ".mtbloco";

struct TerrainBlockKey {
    int x = 0;
    int z = 0;
    auto operator<=>(const TerrainBlockKey&) const = default;
};

class TerrainData {
public:
    static constexpr float kDefaultBlockSize = 32.0f;
    static constexpr int kDefaultResolution = 33;  // 1 m entre amostras com blocos de 32 m
    static constexpr int kFormatVersion = 1;

    explicit TerrainData(float blockSize = kDefaultBlockSize, int resolution = kDefaultResolution);

    float BlockSize() const { return m_blockSize; }
    int Resolution() const { return m_resolution; }
    float Spacing() const { return m_blockSize / static_cast<float>(m_resolution - 1); }

    // Bloco plano na altura dada; false se já existe.
    bool AddBlock(TerrainBlockKey key, float height = 0.0f);
    bool HasBlock(TerrainBlockKey key) const { return m_blocks.contains(key); }
    std::vector<TerrainBlockKey> Blocks() const;  // em ordem (z, depois x)
    size_t BlockCount() const { return m_blocks.size(); }

    // Amostras pela posição na grade de todas: (gx, gz), com gx = x * (resolution - 1) + i. Uma amostra da borda é
    // de dois (ou quatro) blocos: SetSample muda em todos. Fora dos blocos: nullopt / nada.
    std::optional<float> Sample(int gx, int gz) const;
    void SetSample(int gx, int gz, float height);
    // Alturas de um bloco, linha a linha (z, depois x): resolution * resolution valores.
    const std::vector<float>* BlockHeights(TerrainBlockKey key) const;

    // Altura no ponto (x, z) do espaço do terreno, interpolada entre as 4 amostras; nullopt fora dos blocos.
    std::optional<float> HeightAt(float x, float z) const;
    // Normal na amostra (gx, gz), por diferenças centrais com as vizinhas (de qualquer bloco); na borda do terreno, a
    // própria amostra faz o papel da que falta. É a normal dos vértices da malha.
    glm::vec3 NormalAt(int gx, int gz) const;
    // Ponto onde o raio (espaço do terreno) encontra a superfície vindo de cima, até `maxDistance` (em unidades da
    // direção normalizada); nullopt se não encontra. Anda em passos de meia amostra e refina por bisseção: serve ao
    // pincel do editor, que não pode parar em outros objetos na frente.
    std::optional<glm::vec3> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;
    // Menor e maior altura de um bloco (a caixa dele no mundo).
    std::pair<float, float> BlockHeightRange(TerrainBlockKey key) const;

    // Malha de um bloco, com os vértices relativos ao canto dele ((x, z) * blockSize) e as normais tiradas das
    // amostras vizinhas (também dos blocos ao lado: sem emenda de luz). Refeita só quando o bloco ou um vizinho
    // mudou. Nula se o bloco não existe.
    std::shared_ptr<Mesh> BlockMesh(TerrainBlockKey key) const;

    // Sobe a cada mudança (quem guarda algo tirado das alturas compara).
    uint64_t Version() const { return m_version; }
    bool IsDirty() const;  // há bloco mudado desde o último Save ou Load

    // Grava o cabeçalho e os blocos mudados desde a última gravação (todos, se o arquivo é outro). Atômico por
    // arquivo (WriteFileAtomic). Blocos que saíram ficam no disco, mas fora da lista do cabeçalho.
    bool Save(const std::filesystem::path& file, std::string* error = nullptr);
    // Lê o cabeçalho e os blocos. Bloco que falta ou está estragado vira plano (com aviso em `error` e no log), sem
    // impedir o resto; cabeçalho ilegível ou de versão mais nova: nullptr.
    static std::shared_ptr<TerrainData> Load(const std::filesystem::path& file, std::string* error = nullptr);
    // Como Load, mas quem abre o mesmo arquivo recebe o mesmo objeto enquanto alguém o usar (cópias da cena para o
    // desfazer e o Play dividem as alturas).
    static std::shared_ptr<TerrainData> Open(const std::filesystem::path& file, std::string* error = nullptr);

    static std::filesystem::path BlocksFolder(const std::filesystem::path& file);  // "Ilha.blocos" ao lado
    static std::string BlockFileName(TerrainBlockKey key);                         // "3_-2.mtbloco"

private:
    struct Block {
        std::vector<float> heights;
        bool dirty = true;
        uint64_t version = 1;
        mutable std::shared_ptr<Mesh> mesh;
        mutable uint64_t meshVersion = 0;
    };

    Block* Find(TerrainBlockKey key);
    const Block* Find(TerrainBlockKey key) const;
    void Touch(int gx, int gz);  // a amostra mudou: blocos dela e vizinhos refazem a malha

    float m_blockSize;
    int m_resolution;
    std::map<TerrainBlockKey, Block> m_blocks;
    uint64_t m_version = 1;
    std::filesystem::path m_savedFile;  // para onde foi o último Save (ou de onde veio o Load)
};

} // namespace motor
