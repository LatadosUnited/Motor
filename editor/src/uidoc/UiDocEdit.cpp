#include "uidoc/UiDocEdit.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

namespace editor {

using namespace motor;

UiElement* UiElementAt(std::vector<UiElement>& roots, const UiPath& path) {
    if (path.empty()) return nullptr;
    std::vector<UiElement>* list = &roots;
    UiElement* e = nullptr;
    for (const int i : path) {
        if (i < 0 || i >= static_cast<int>(list->size())) return nullptr;
        e = &(*list)[static_cast<size_t>(i)];
        list = &e->children;
    }
    return e;
}

const UiElement* UiElementAt(const std::vector<UiElement>& roots, const UiPath& path) {
    return UiElementAt(const_cast<std::vector<UiElement>&>(roots), path);
}

std::vector<UiElement>* UiChildrenAt(std::vector<UiElement>& roots, const UiPath& path) {
    if (path.empty()) return &roots;
    UiElement* e = UiElementAt(roots, path);
    return e ? &e->children : nullptr;
}

namespace {

template <class Match>
bool FindPath(const std::vector<UiElement>& list, const Match& match, UiPath& path) {
    for (size_t i = 0; i < list.size(); ++i) {
        path.push_back(static_cast<int>(i));
        if (match(list[i]) || FindPath(list[i].children, match, path)) return true;
        path.pop_back();
    }
    return false;
}

void CollectNames(const std::vector<UiElement>& list, std::set<std::string>& out) {
    for (const UiElement& e : list) {
        if (!e.name.empty()) out.insert(e.name);
        CollectNames(e.children, out);
    }
}

std::string UniqueName(const std::set<std::string>& used, std::string_view base) {
    std::string stem(base.empty() ? std::string_view("elemento") : base);
    if (!used.contains(stem)) return stem;
    // "botao2" duplicado vira "botao3", não "botao22".
    while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back()))) stem.pop_back();
    if (stem.empty()) stem = "elemento";
    for (int n = 2;; ++n) {
        std::string name = stem + std::to_string(n);
        if (!used.contains(name)) return name;
    }
}

// Nomes únicos na cópia (ela e os filhos), contando os que já existem e os que ela mesma vai recebendo.
void RenameCopy(UiElement& e, std::set<std::string>& used) {
    if (!e.name.empty()) {
        e.name = UniqueName(used, e.name);
        used.insert(e.name);
    }
    for (UiElement& c : e.children) RenameCopy(c, used);
}

float Rounded(float v) { return std::round(v); }

} // namespace

bool FindUiElementPath(const std::vector<UiElement>& roots, const UiElement* target, UiPath& out) {
    out.clear();
    if (!target) return false;
    return FindPath(roots, [&](const UiElement& e) { return &e == target; }, out);
}

bool FindUiElementPath(const std::vector<UiElement>& roots, std::string_view name, UiPath& out) {
    out.clear();
    if (name.empty()) return false;
    return FindPath(roots, [&](const UiElement& e) { return e.name == name; }, out);
}

bool IsUiPathPrefix(const UiPath& prefix, const UiPath& path) {
    return prefix.size() <= path.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

std::string UniqueUiElementName(const std::vector<UiElement>& roots, std::string_view base) {
    std::set<std::string> used;
    CollectNames(roots, used);
    return UniqueName(used, base);
}

UiElement NewPaletteElement(UiElementType type, const std::vector<UiElement>& roots) {
    UiElement e = UiElement::Make(type);
    const char* base = "painel";
    switch (type) {
        case UiElementType::Panel:
            // Painel vazio teria altura 0 e seria transparente: com fundo e altura, aparece onde caiu.
            e.SetStyleValue("height", UiLength::Px(120.0f));
            e.SetStyleValue("color", glm::vec4(0.10f, 0.12f, 0.16f, 0.85f));
            break;
        case UiElementType::Text: base = "texto"; break;
        case UiElementType::Image:
            base = "imagem";
            e.SetStyleValue("width", UiLength::Px(100.0f));
            e.SetStyleValue("height", UiLength::Px(100.0f));
            break;
        case UiElementType::Button: base = "botao"; break;
        case UiElementType::ProgressBar:
            base = "barra";
            e.value = 0.5f;  // meio cheia: aparece na prévia
            break;
        case UiElementType::Slider:
            base = "slider";
            e.value = 0.5f;
            break;
        case UiElementType::Toggle: base = "selecao"; break;
        case UiElementType::TextField:
            base = "campo";
            e.placeholder = "Digite aqui";
            break;
    }
    e.name = UniqueUiElementName(roots, base);
    return e;
}

UiPath InsertUiElement(std::vector<UiElement>& roots, const UiPath& parent, int index, UiElement element) {
    std::vector<UiElement>* list = UiChildrenAt(roots, parent);
    if (!list) return {};
    const int size = static_cast<int>(list->size());
    if (index < 0 || index > size) index = size;
    list->insert(list->begin() + index, std::move(element));
    UiPath path = parent;
    path.push_back(index);
    return path;
}

bool RemoveUiElement(std::vector<UiElement>& roots, const UiPath& path) {
    if (path.empty()) return false;
    std::vector<UiElement>* list = UiChildrenAt(roots, UiPath(path.begin(), path.end() - 1));
    const int i = path.back();
    if (!list || i < 0 || i >= static_cast<int>(list->size())) return false;
    list->erase(list->begin() + i);
    return true;
}

UiPath DuplicateUiElement(std::vector<UiElement>& roots, const UiPath& path) {
    const UiElement* original = UiElementAt(roots, path);
    if (!original) return {};
    UiElement copy = *original;
    std::set<std::string> used;
    CollectNames(roots, used);
    RenameCopy(copy, used);
    return InsertUiElement(roots, UiPath(path.begin(), path.end() - 1), path.back() + 1, std::move(copy));
}

UiPath MoveUiElement(std::vector<UiElement>& roots, const UiPath& from, const UiPath& newParent, int index) {
    const UiElement* source = UiElementAt(roots, from);
    if (!source || IsUiPathPrefix(from, newParent) || !UiChildrenAt(roots, newParent)) return {};
    UiElement moving = *source;
    // O caminho do pai novo muda se ele vem depois do elemento entre os irmãos de um ancestral.
    UiPath parent = newParent;
    const size_t depth = from.size() - 1;
    if (parent.size() > depth && IsUiPathPrefix(UiPath(from.begin(), from.end() - 1), parent) &&
        parent[depth] > from.back()) {
        --parent[depth];
    }
    RemoveUiElement(roots, from);
    return InsertUiElement(roots, parent, index, std::move(moving));
}

std::vector<std::string> ParseUiClassList(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    auto flush = [&] {
        if (!current.empty() && std::find(out.begin(), out.end(), current) == out.end()) out.push_back(current);
        current.clear();
    };
    for (const char c : text) {
        if (c == ' ' || c == ',' || c == '.' || c == '\t' || c == ';') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return out;
}

std::string JoinUiClassList(const std::vector<std::string>& classes) {
    std::string out;
    for (const std::string& c : classes) {
        if (!out.empty()) out += ' ';
        out += c;
    }
    return out;
}

void ResizeUiElement(UiElement& element, const UiBox& from, const UiBox& to) {
    const glm::vec2 dmin = to.min - from.min;
    const glm::vec2 dmax = to.max - from.max;
    const glm::vec2 oldSize = from.Size();
    const glm::vec2 newSize = glm::max(to.Size(), glm::vec2(0.0f));
    const bool sizeChanged[2] = {std::abs(newSize.x - oldSize.x) > 0.01f, std::abs(newSize.y - oldSize.y) > 0.01f};
    const char* sizeProps[2] = {"width", "height"};
    const UiLength* lengths[2] = {&element.width, &element.height};

    if (element.placement == UiPlacement::Absolute && element.anchor == UiAnchor::Stretch) {
        // Margens: position = esquerda e cima, width/height = direita e baixo (andam ao contrário do lado).
        glm::vec2 position = element.position + dmin;
        for (int i = 0; i < 2; ++i) {
            if (std::abs(dmax[i]) <= 0.01f) continue;
            const float margin = lengths[i]->unit == UiUnit::Px ? lengths[i]->value : 0.0f;
            element.SetStyleValue(sizeProps[i], UiLength::Px(Rounded(margin - dmax[i])));
        }
        if (std::abs(dmin.x) > 0.01f || std::abs(dmin.y) > 0.01f) {
            element.SetStyleValue("position", glm::vec2(Rounded(position.x), Rounded(position.y)));
        }
        return;
    }
    for (int i = 0; i < 2; ++i) {
        if (sizeChanged[i]) element.SetStyleValue(sizeProps[i], UiLength::Px(Rounded(newSize[i])));
    }
    if (element.placement == UiPlacement::Absolute) {
        // O ponto do elemento preso na âncora anda na fração dela entre os dois lados.
        const glm::vec2 a = UiAnchorPoint(element.anchor);
        const glm::vec2 delta = dmin * (glm::vec2(1.0f) - a) + dmax * a;
        if (std::abs(delta.x) > 0.01f || std::abs(delta.y) > 0.01f) {
            const glm::vec2 p = element.position + delta;
            element.SetStyleValue("position", glm::vec2(Rounded(p.x), Rounded(p.y)));
        }
    }
}

bool SetUiRuleSelector(UiStyleRule& rule, std::string_view text, std::string* error) {
    std::vector<UiSelector> selectors;
    if (!ParseUiSelectors(text, selectors, error)) return false;
    rule.selector = std::string(text);
    rule.selectors = std::move(selectors);
    return true;
}

bool UiEditState::SameContent(const UiEditState& other) const {
    return sheets == other.sheets && doc.SameContent(other.doc);
}

void ApplyUiEditSheets(UiEditState& state) {
    state.doc.sheets.clear();
    for (const UiEditSheet& s : state.sheets) {
        state.doc.sheets.push_back(s.file.empty() ? nullptr : std::make_shared<const UiStyleSheet>(s.sheet));
    }
    ComputeUiStyles(state.doc);
}

void UiEditHistory::Reset(const UiEditState& state) {
    m_current = state;
    m_undo.clear();
    m_redo.clear();
    m_version = m_nextVersion++;
}

bool UiEditHistory::Commit(const UiEditState& state) {
    if (state.SameContent(m_current)) {
        m_current.selected = state.selected;  // a seleção não vira passo, mas o desfazer volta para ela
        return false;
    }
    m_undo.push_back({std::move(m_current), m_version});
    if (m_undo.size() > kMaxSteps) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_current = state;
    m_version = m_nextVersion++;
    return true;
}

bool UiEditHistory::Undo(UiEditState& state) {
    Commit(state);  // o que mudou e ainda não virou passo também se desfaz
    if (m_undo.empty()) return false;
    m_redo.push_back({std::move(m_current), m_version});
    m_current = std::move(m_undo.back().state);
    m_version = m_undo.back().version;
    m_undo.pop_back();
    state = m_current;
    return true;
}

bool UiEditHistory::Redo(UiEditState& state) {
    if (Commit(state)) return false;  // mudou algo depois do desfazer: o refazer acabou
    if (m_redo.empty()) return false;
    m_undo.push_back({std::move(m_current), m_version});
    m_current = std::move(m_redo.back().state);
    m_version = m_redo.back().version;
    m_redo.pop_back();
    state = m_current;
    return true;
}

} // namespace editor
