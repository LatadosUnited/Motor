#pragma once

// Importação de modelos glTF (Fase 11) e FBX: o arquivo .gltf/.glb/.fbx do projeto vira um prefab que dá para arrastar
// para a cena, com os materiais do glTF como materiais do Motor (.mmat) e as texturas como arquivos do projeto.
//
// Para "Assets/Personagens/Robo.glb" são gerados:
//   Assets/Personagens/Robo.mprefab                  raiz "Robo" (com Animator se o modelo tiver clipes ou pele)
//   Assets/Personagens/Robo_materiais/<material>.mmat
//   Assets/Personagens/Robo_materiais/<imagem>.png    texturas embutidas no glTF (as externas são usadas onde estão)
//
// As entidades do prefab seguem os nós do glTF (posição, rotação, escala); os ossos não viram entidades (o
// esqueleto é do Animator) e as malhas com pele ficam como filhas diretas da raiz. As malhas e os clipes são
// partes do arquivo do modelo ("<guid>#<nome>", ver AssetLibrary). Importar de novo (o modelo mudou)
// sobrescreve o prefab e os .mmat, mantendo os GUIDs: as instâncias na cena seguem o prefab novo.

#include <cstddef>
#include <filesystem>
#include <string>

namespace motor {

class AssetLibrary;

struct ModelImportResult {
    std::filesystem::path prefab;  // .mprefab gerado
    std::filesystem::path folder;  // pasta dos materiais e das texturas (pode não existir)
    size_t entities = 0;
    size_t materials = 0;
    size_t textures = 0;           // imagens gravadas na pasta
};

// Prefab gerado para o modelo ("<pasta>/<nome>.mprefab").
std::filesystem::path ModelPrefabPath(const std::filesystem::path& modelFile);
// O prefab ainda não existe ou é mais antigo que o modelo.
bool ModelNeedsImport(const std::filesystem::path& modelFile);

// Lê o modelo (de dentro do projeto do `assets`) e grava o prefab, os materiais e as texturas.
bool ImportModel(const std::filesystem::path& modelFile, AssetLibrary& assets, ModelImportResult* result = nullptr,
                 std::string* error = nullptr);

// Importa os modelos do projeto que precisam (ModelNeedsImport). Devolve quantos foram importados.
size_t ImportModelsInProject(AssetLibrary& assets);

} // namespace motor
