#include "panels/HierarchyFilter.h"

#include <cstdint>

namespace editor {

std::string FoldForSearch(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            continue;
        }
        // Letras acentuadas do Latin-1 (U+00C0 a U+00FF) vêm em UTF-8 como 0xC3 seguido de 0x80 a 0xBF.
        if (c == 0xC3 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            const unsigned code = 0xC0u + (next & 0x3Fu);  // U+00C0 + resto
            // Tabela de U+00C0 a U+00FF: a letra sem acento, ou 0 para manter o caractere como está.
            static constexpr char kBase[64] = {
                'a', 'a', 'a', 'a', 'a', 'a', 0,   'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',  // C0-CF
                0,   'n', 'o', 'o', 'o', 'o', 'o', 0,   0,   'u', 'u', 'u', 'u', 'y', 0,   0,    // D0-DF
                'a', 'a', 'a', 'a', 'a', 'a', 0,   'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',  // E0-EF
                0,   'n', 'o', 'o', 'o', 'o', 'o', 0,   0,   'u', 'u', 'u', 'u', 'y', 0,   'y'}; // F0-FF
            if (const char base = kBase[code - 0xC0u]) {
                out += base;
                ++i;
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

HierarchyFilter HierarchyFilter::Parse(std::string_view text) {
    HierarchyFilter filter;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        size_t end = i;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t') ++end;
        std::string word = FoldForSearch(text.substr(i, end - i));
        i = end;
        if (word.empty()) continue;
        if (word.starts_with("t:")) {
            if (word.size() > 2) filter.types.push_back(word.substr(2));  // "t:" sozinho ainda está sendo digitado
        } else {
            filter.names.push_back(std::move(word));
        }
    }
    return filter;
}

bool HierarchyFilter::Matches(const motor::Entity& entity) const {
    const std::string name = FoldForSearch(entity.name);
    for (const std::string& word : names) {
        if (name.find(word) == std::string::npos) return false;
    }
    for (const std::string& word : types) {
        bool found = false;
        for (const motor::ComponentPtr& component : entity.components) {
            const motor::ComponentType& type = component.Type();
            if (FoldForSearch(type.name).find(word) != std::string::npos ||
                FoldForSearch(type.label).find(word) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

namespace {
void Collect(const motor::Scene& scene, motor::EntityId parent, const HierarchyFilter& filter,
             std::vector<motor::EntityId>& out) {
    for (motor::EntityId id : scene.Children(parent)) {
        if (filter.Matches(*scene.Find(id))) out.push_back(id);
        Collect(scene, id, filter, out);
    }
}
} // namespace

std::vector<motor::EntityId> SearchHierarchy(const motor::Scene& scene, const HierarchyFilter& filter) {
    std::vector<motor::EntityId> out;
    Collect(scene, motor::kNullEntity, filter, out);
    return out;
}

std::string ParentPath(const motor::Scene& scene, motor::EntityId id) {
    std::string path;
    const motor::Entity* e = scene.Find(id);
    for (motor::EntityId p = e ? e->parent : motor::kNullEntity; p != motor::kNullEntity;) {
        const motor::Entity* parent = scene.Find(p);
        if (!parent) break;
        path = path.empty() ? parent->name : parent->name + " / " + path;
        p = parent->parent;
    }
    return path;
}

} // namespace editor
