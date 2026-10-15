#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace motor {

// Identidade fixa dos arquivos de asset. Cada asset (cena, textura, malha) dentro de Assets/ tem ao lado um
// arquivo "<nome>.meta" com um GUID que não muda quando o arquivo é renomeado ou movido (desde que o .meta
// vá junto). As cenas referenciam assets por esse GUID.
//
// Refresh() varre a pasta: cria o .meta de quem não tem e resolve GUIDs repetidos (arquivo copiado junto com
// o .meta) dando um GUID novo à cópia.
class AssetDatabase {
public:
    static constexpr const char* kMetaExtension = ".meta";

    // assetsDir: pasta Assets do projeto (vazia = desliga). Faz um Refresh().
    void SetAssetsDir(const std::filesystem::path& assetsDir);
    const std::filesystem::path& AssetsDir() const { return m_assetsDir; }

    void Refresh();

    // GUID do arquivo; se ele ainda não tiver .meta, cria agora. "" se não for um asset dentro de Assets/.
    std::string GuidFor(const std::filesystem::path& file);
    std::string FindGuid(const std::filesystem::path& file) const;      // só consulta
    std::filesystem::path PathFor(std::string_view guid) const;        // vazio se desconhecido

    size_t Count() const { return m_pathByGuid.size(); }

    static std::filesystem::path MetaPathFor(const std::filesystem::path& asset);
    static bool IsMetaFile(const std::filesystem::path& path);
    static bool IsTrackedAsset(const std::filesystem::path& path);  // tipos que ganham .meta
    static std::string NewGuid();                                    // 32 dígitos hexadecimais
    static bool IsValidGuid(std::string_view text);

private:
    struct CachedMeta {
        std::filesystem::file_time_type modified{};
        std::string guid;
    };

    std::string ReadOrCreateMeta(const std::filesystem::path& asset);
    bool WriteMeta(const std::filesystem::path& asset, const std::string& guid);

    std::filesystem::path m_assetsDir;
    std::map<std::string, std::filesystem::path, std::less<>> m_pathByGuid;
    std::map<std::filesystem::path, std::string> m_guidByPath;      // caminhos normalizados
    std::map<std::filesystem::path, CachedMeta> m_metaCache;        // evita reler .meta que não mudou
};

} // namespace motor
