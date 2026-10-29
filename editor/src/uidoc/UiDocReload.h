#pragma once

// Recarga dos documentos de UI (.mui) e das folhas (.mstyle) mudados fora do editor ou gravados pelo Editor de UI:
// como os materiais (AssetLibrary::ReloadChangedMaterials), pela data de modificação dos arquivos. Sem janela nem
// OpenGL (tests/test_ui_editor.cpp).

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace motor {
class AssetLibrary;
class Scene;
struct UiFont;
} // namespace motor

namespace editor {

// Arquivo de um documento ou folha pelo GUID (arquivo movido) ou pelo caminho. Vazio se não achar.
std::filesystem::path UiAssetFile(const std::string& guid, const std::string& path, motor::AssetLibrary& assets);

class UiDocReloader {
public:
    // Passa cada cena que guarda componentes UiDocument (a aberta, a de antes do Play, as fotos do desfazer...).
    using SceneVisitor = std::function<void(const std::function<void(motor::Scene&)>&)>;

    // Confere as datas dos .mui usados em `scene` e das folhas deles. Na primeira vez que vê um arquivo, só anota.
    // Os documentos que mudaram (ou cuja folha mudou) são relidos uma vez e postos em todos os UiDocument que os usam
    // nas cenas de `forEach` (que deve incluir `scene`): a árvore viva de cada um é refeita, também no Play. Devolve os
    // arquivos que mudaram.
    std::vector<std::filesystem::path> Update(motor::Scene& scene, motor::AssetLibrary& assets, const SceneVisitor& forEach);
    void Clear() { m_times.clear(); }  // outro projeto

private:
    std::map<std::filesystem::path, std::filesystem::file_time_type> m_times;
};

} // namespace editor
