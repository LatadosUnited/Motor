#include "uidoc/UiDocReload.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/core/Log.h>
#include <motor/scene/Scene.h>
#include <motor/ui/UiDocument.h>

#include <algorithm>
#include <memory>
#include <set>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

fs::path UiAssetFile(const std::string& guid, const std::string& path, AssetLibrary& assets) {
    fs::path file;
    if (!guid.empty()) file = assets.Database().PathFor(guid);
    if (file.empty() && !path.empty()) file = assets.FileForKey(path);
    return file.empty() ? file : file.lexically_normal();
}

std::vector<fs::path> UiDocReloader::Update(Scene& scene, AssetLibrary& assets, const SceneVisitor& forEach) {
    // Os arquivos de cada documento usado na cena: o .mui e as folhas dele.
    std::map<fs::path, std::vector<fs::path>> documents;
    for (const auto& e : scene.Entities()) {
        const UiDocument* component = e->Get<UiDocument>();
        if (!component || component->document.IsEmpty()) continue;
        const fs::path file = UiAssetFile(component->document.guid, component->document.path, assets);
        if (file.empty() || documents.contains(file)) continue;
        std::vector<fs::path>& files = documents[file];
        files.push_back(file);
        if (const auto& data = component->document.data) {
            for (const UiFont& sheet : data->styles) {
                if (fs::path f = UiAssetFile(sheet.guid, sheet.path, assets); !f.empty()) files.push_back(std::move(f));
            }
        }
    }

    std::vector<fs::path> changed;
    std::set<fs::path> checked;
    std::set<fs::path> stale;  // documentos a reler
    for (const auto& [document, files] : documents) {
        for (const fs::path& file : files) {
            if (checked.insert(file).second) {
                std::error_code ec;
                const auto modified = fs::last_write_time(file, ec);
                if (ec) continue;  // apagado ou preso: confere de novo depois
                const auto it = m_times.find(file);
                if (it == m_times.end()) {
                    m_times[file] = modified;
                } else if (it->second != modified) {
                    it->second = modified;
                    changed.push_back(file);
                }
            }
            if (std::find(changed.begin(), changed.end(), file) != changed.end()) stale.insert(document);
        }
    }
    if (stale.empty()) return changed;

    // Relido uma vez por documento; as outras entidades (e as outras cenas) ficam com o mesmo conteúdo.
    std::map<fs::path, std::shared_ptr<const UiDocumentData>> reloaded;
    forEach([&](Scene& s) {
        for (const auto& e : s.Entities()) {
            UiDocument* component = e->Get<UiDocument>();
            if (!component || component->document.IsEmpty()) continue;
            const fs::path file = UiAssetFile(component->document.guid, component->document.path, assets);
            if (!stale.contains(file)) continue;
            if (const auto it = reloaded.find(file); it != reloaded.end()) {
                component->document.data = it->second;
                continue;
            }
            ReloadUiDocument(component->document, assets);
            reloaded[file] = component->document.data;
            if (component->document.data) log::Info("UI: documento relido: {}", component->document.path);
        }
    });
    return changed;
}

} // namespace editor
