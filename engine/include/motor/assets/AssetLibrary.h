#pragma once

#include "motor/assets/AssetDatabase.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/renderer/Environment.h"
#include "motor/renderer/Material.h"


#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class Mesh;
class Texture2D;
struct AnimationClip;
struct Model;

// Como uma cena aponta para um asset.
//   - embutido (guid vazio): `name` é o nome registrado pelo código ("Cubo", "Xadrez");
//   - arquivo: `guid` é a identidade (do .meta) e `name` é o caminho atual relativo ao projeto
//     ("Assets/Modelos/carro.obj"), útil para mostrar e como plano B se o .meta se perder.
//   - parte de um arquivo (malha ou clipe de um modelo glTF): o GUID e o caminho do arquivo com "#<nome>" no
//     fim ("<guid>#Corpo", "Assets/Modelos/robo.glb#Corpo").
struct AssetRef {
    std::string name;
    std::string guid;
};

// Texturas de um material pela referência (sem precisar carregá-las): usado pelo importador de modelos.
struct MaterialTextureRefs {
    std::optional<AssetRef> albedo;
    std::optional<AssetRef> normal;
    std::optional<AssetRef> metallicRoughness;
    std::optional<AssetRef> occlusion;
    std::optional<AssetRef> emissive;
};

// Registro de malhas, texturas e materiais. Assets de arquivo são carregados do disco na primeira vez que
// alguém pede e ficam em cache, identificados pelo GUID: renomear/mover o arquivo (com o .meta) não quebra nada.
//
// Materiais (.mmat) são compartilhados: todos os Mesh Renderers que usam o mesmo arquivo recebem o mesmo
// objeto Material, então editar o material (ou recarregá-lo do disco) muda todos de uma vez.
class AssetLibrary {
public:
    void AddMesh(std::string name, std::shared_ptr<Mesh> mesh);
    void AddTexture(std::string name, std::shared_ptr<Texture2D> texture);

    // Pasta do projeto. Trocar a raiz descarrega os assets de arquivo do cache
    // (quem ainda usa um deles continua com a sua cópia).
    void SetRoot(const std::filesystem::path& root);
    const std::filesystem::path& Root() const { return m_root; }
    AssetDatabase& Database() { return m_database; }
    const AssetDatabase& Database() const { return m_database; }

    // Cria a textura a partir da imagem lida do arquivo. Padrão: Texture2D (OpenGL). Testes sem janela trocam
    // por uma fábrica que não usa a GPU.
    using TextureFactory = std::function<std::shared_ptr<Texture2D>(const ImageData& image)>;
    void SetTextureFactory(TextureFactory factory) { m_textureFactory = std::move(factory); }

    // `key`: nome embutido ou caminho relativo ao projeto. nullptr (e `error`) se não existir ou falhar.
    std::shared_ptr<Mesh> GetMesh(std::string_view key, std::string* error = nullptr);
    std::shared_ptr<Texture2D> GetTexture(std::string_view key, std::string* error = nullptr);
    std::shared_ptr<Mesh> GetMeshByGuid(std::string_view guid, std::string* error = nullptr);
    std::shared_ptr<Texture2D> GetTextureByGuid(std::string_view guid, std::string* error = nullptr);

    // Materiais (.mmat). Não há materiais embutidos: `key` é sempre um caminho relativo ao projeto.
    std::shared_ptr<Material> GetMaterial(std::string_view key, std::string* error = nullptr);
    std::shared_ptr<Material> GetMaterialByGuid(std::string_view guid, std::string* error = nullptr);
    // Grava o material no arquivo de onde ele veio (false se ele não veio desta biblioteca).
    bool SaveMaterial(const Material* material, std::string* error = nullptr);
    // Cria (ou sobrescreve) um .mmat com estes valores. O arquivo ganha .meta se estiver dentro de Assets/.
    bool WriteMaterialFile(const std::filesystem::path& file, const Material& material, std::string* error = nullptr);
    // Relê os .mmat em cache que mudaram no disco (editados fora do editor), no mesmo objeto. Devolve quantos.
    size_t ReloadChangedMaterials();

    // Mesmo, com as texturas dadas pela referência (os campos de textura de `material` são ignorados).
    bool WriteMaterialFile(const std::filesystem::path& file, const Material& material,
                           const MaterialTextureRefs& textures, std::string* error = nullptr);

    // Modelos glTF (.gltf/.glb, motor/assets/Model.h). As malhas e os clipes do modelo também são achados por
    // GetMesh/GetMeshByGuid e GetClip/GetClipByGuid com "#<nome>" no fim da chave ou do GUID.
    std::shared_ptr<Model> GetModel(std::string_view key, std::string* error = nullptr);
    // Relê o arquivo se ele mudou desde a última leitura (malhas e clipes novos; os antigos seguem vivos com quem os usa).
    std::shared_ptr<Model> GetModelByGuid(std::string_view guid, std::string* error = nullptr);
    std::shared_ptr<AnimationClip> GetClip(std::string_view key, std::string* error = nullptr);
    std::shared_ptr<AnimationClip> GetClipByGuid(std::string_view guid, std::string* error = nullptr);

    // Céus HDR (.hdr, AssetType::EnvironmentMap): a imagem na CPU; o renderer sobe para a GPU.
    std::shared_ptr<const HdrImage> GetEnvironmentMap(std::string_view key, std::string* error = nullptr);
    std::shared_ptr<const HdrImage> GetEnvironmentMapByGuid(std::string_view guid, std::string* error = nullptr);

    // Conteúdo de um .mmat. Texturas embutidas pelo nome; as de arquivo pelo GUID (com o caminho junto).
    // Versão 2 (Fase 7): PBR (metal, rugosidade, mapas). A versão 1 (Blinn-Phong) é lida e convertida.
    std::string MaterialToJson(const Material& material) const;
    std::string MaterialToJson(const Material& material, const MaterialTextureRefs& textures) const;
    // Campos ausentes ficam com o valor padrão; textura não encontrada vira aviso no log.
    bool MaterialFromJson(std::string_view json, Material& out, std::string* error = nullptr);

    // Só embutidos (não carrega nada).
    std::shared_ptr<Mesh> FindMesh(std::string_view name) const;
    std::shared_ptr<Texture2D> FindTexture(std::string_view name) const;

    // Referência de um asset em uso (nullopt se não veio desta biblioteca).
    std::optional<AssetRef> RefOf(const Mesh* mesh) const;
    std::optional<AssetRef> RefOf(const Texture2D* texture) const;
    std::optional<AssetRef> RefOf(const Material* material) const;
    std::optional<AssetRef> RefOf(const Model* model) const;
    std::optional<AssetRef> RefOf(const AnimationClip* clip) const;
    std::optional<AssetRef> RefOf(const HdrImage* image) const;

    // Separador entre o arquivo e a parte dele ("robo.glb#Corpo").
    static constexpr char kSubAssetSeparator = '#';

    static bool IsFileKey(std::string_view key) { return key.find('/') != std::string_view::npos; }
    std::string KeyForFile(const std::filesystem::path& file) const;  // "" se estiver fora da raiz
    std::filesystem::path FileForKey(std::string_view key) const;     // vazio se não for chave de arquivo

    // Chaves (caminhos relativos) de todos os arquivos desse tipo no projeto, em ordem alfabética.
    std::vector<std::string> ScanFiles(AssetType type) const;

    template <class T>
    struct Entry {
        std::string name;  // embutidos: nome; arquivos: vazio (o caminho vem do banco de GUIDs)
        std::string guid;  // vazio para embutidos
        std::shared_ptr<T> asset;
    };
    const std::vector<Entry<Mesh>>& Meshes() const { return m_meshes; }
    const std::vector<Entry<Texture2D>>& Textures() const { return m_textures; }
    const std::vector<Entry<Material>>& Materials() const { return m_materials; }
    const std::vector<Entry<Model>>& Models() const { return m_models; }

    // Referências das texturas que o material usa (as que não vieram desta biblioteca ficam vazias, com aviso).
    MaterialTextureRefs TextureRefsOf(const Material& material) const;

private:
    template <class T>
    std::optional<AssetRef> RefIn(const std::vector<Entry<T>>& list, const T* asset) const;

    std::filesystem::path m_root;
    AssetDatabase m_database;
    std::vector<Entry<Mesh>> m_meshes;
    std::vector<Entry<Texture2D>> m_textures;
    std::vector<Entry<Material>> m_materials;  // só de arquivo
    std::vector<Entry<Model>> m_models;
    TextureFactory m_textureFactory;  // vazia = Texture2D
    std::vector<Entry<AnimationClip>> m_clips;  // GUID "<modelo>#<clipe>"
    std::vector<Entry<const HdrImage>> m_environmentMaps;
    std::map<std::string, std::filesystem::file_time_type, std::less<>> m_modelTimes;  // GUID -> data do arquivo lido
    std::map<std::string, std::filesystem::file_time_type, std::less<>> m_materialTimes;  // GUID -> data do arquivo lido
};

} // namespace motor
