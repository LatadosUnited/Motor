#include "motor/scene/PrefabInstance.h"

#include "motor/assets/AssetLibrary.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace motor {

using json = nlohmann::ordered_json;

bool PrefabLink::HasOverride(std::string_view key) const {
    return std::find(overrides.begin(), overrides.end(), key) != overrides.end();
}

bool PrefabLink::AddOverride(std::string key) {
    if (HasOverride(key)) return false;
    overrides.push_back(std::move(key));
    return true;
}

CustomFieldType FieldTraits<PrefabLink>::Describe() {
    return {"PrefabLink",
            [](const void* a, const void* b) {
                const auto& la = *static_cast<const PrefabLink*>(a);
                const auto& lb = *static_cast<const PrefabLink*>(b);
                return la.guid == lb.guid && la.source == lb.source && la.overrides == lb.overrides;
            },
            [](const void* v, const AssetLibrary& assets) {
                const auto& link = *static_cast<const PrefabLink*>(v);
                json j;
                j["guid"] = link.guid;
                // Caminho só para leitura humana e plano B, como nas outras referências a assets.
                if (const auto file = assets.Database().PathFor(link.guid); !file.empty()) {
                    if (const std::string key = assets.KeyForFile(file); !key.empty()) j["path"] = key;
                }
                j["source"] = link.source;
                if (!link.overrides.empty()) j["overrides"] = link.overrides;
                return j.dump(-1, ' ', false, json::error_handler_t::replace);
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                auto& link = *static_cast<PrefabLink*>(v);
                const json j = json::parse(text, nullptr, false);
                if (!j.is_object()) {
                    if (error) *error = "esperado objeto {\"guid\": \"...\", \"source\": 1}";
                    return false;
                }
                link.guid = j.value("guid", std::string());
                const std::string path = j.value("path", std::string());
                if (link.guid.empty() || assets.Database().PathFor(link.guid).empty()) {
                    // .meta perdido: acha o prefab pelo caminho salvo.
                    if (const std::string guid = assets.Database().GuidFor(assets.FileForKey(path)); !guid.empty()) {
                        link.guid = guid;
                    }
                }
                if (auto it = j.find("source"); it != j.end() && it->is_number_unsigned()) {
                    link.source = it->get<uint32_t>();
                }
                link.overrides.clear();
                if (auto it = j.find("overrides"); it != j.end() && it->is_array()) {
                    for (const json& key : *it) {
                        if (key.is_string()) link.AddOverride(key.get<std::string>());
                    }
                }
                link.syncedRevision = 0;
                return true;
            }};
}

MOTOR_COMPONENT_MODULE(PrefabInstance) {
    // Criado pelo editor ao instanciar um prefab (não aparece em "Adicionar componente").
    registry.Register<PrefabInstance>("PrefabInstance", "Prefab").Field("prefab", "Prefab", &PrefabInstance::link);
}

} // namespace motor
