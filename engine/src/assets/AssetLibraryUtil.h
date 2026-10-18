#pragma once

// Peças internas do AssetLibrary (AssetLibrary.cpp, AssetLibraryMaterials.cpp): busca nas listas de assets
// carregados e mensagens de erro. Não é API pública.

#include "motor/assets/AssetLibrary.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace motor::asset_library {

template <class T>
std::shared_ptr<T> FindBuiltIn(const std::vector<AssetLibrary::Entry<T>>& list, std::string_view name) {
    for (const auto& e : list) {
        if (e.guid.empty() && e.name == name) return e.asset;
    }
    return nullptr;
}

template <class T>
std::shared_ptr<T> FindByGuid(const std::vector<AssetLibrary::Entry<T>>& list, std::string_view guid) {
    for (const auto& e : list) {
        if (!e.guid.empty() && e.guid == guid) return e.asset;
    }
    return nullptr;
}

inline void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

// "arquivo#parte" -> ("arquivo", "parte"); sem '#', a parte fica vazia.
inline std::pair<std::string_view, std::string_view> SplitSubAsset(std::string_view text) {
    const size_t hash = text.find(AssetLibrary::kSubAssetSeparator);
    if (hash == std::string_view::npos) return {text, {}};
    return {text.substr(0, hash), text.substr(hash + 1)};
}

} // namespace motor::asset_library
