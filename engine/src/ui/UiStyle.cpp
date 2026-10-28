#include "motor/ui/UiStyle.h"

#include "UiStyleJson.h"

#include "motor/common/FileRead.h"
#include "motor/common/FileWrite.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/ui/UiDocument.h"

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <span>
#include <type_traits>

namespace motor {

namespace {

enum class Kind { Bool, Float, Vec2, Vec4, Length, Edges, Enum, File };  // mesma ordem do UiStyleValue

constexpr const char* kPlacementNames[] = {"Flow", "Absolute"};
constexpr const char* kAnchorNames[] = {"TopLeft", "Top",        "TopRight", "Left",        "Center",
                                        "Right",   "BottomLeft", "Bottom",   "BottomRight", "Stretch"};
constexpr const char* kAlignNames[] = {"Auto", "Start", "Center", "End", "Stretch"};
constexpr const char* kDirectionNames[] = {"Column", "Row"};
constexpr const char* kJustifyNames[] = {"Start", "Center", "End", "SpaceBetween", "SpaceAround"};
constexpr const char* kOverflowNames[] = {"Visible", "Hidden", "Scroll"};
constexpr const char* kTextAlignNames[] = {"Left", "Center", "Right"};
constexpr const char* kVerticalAlignNames[] = {"Top", "Middle", "Bottom"};

template <auto M> UiStyleValue GetField(const UiElement& e) {
    using T = std::remove_cvref_t<decltype(e.*M)>;
    if constexpr (std::is_enum_v<T>) return static_cast<int>(e.*M);
    else return e.*M;
}

template <auto M> void SetField(UiElement& e, const UiStyleValue& v) {
    using T = std::remove_cvref_t<decltype(e.*M)>;
    if constexpr (std::is_enum_v<T>) {
        if (const int* i = std::get_if<int>(&v)) e.*M = static_cast<T>(*i);
    } else {
        if (const T* x = std::get_if<T>(&v)) e.*M = *x;
    }
}

struct Property {
    const char* name;
    Kind kind;
    bool inherited;
    std::span<const char* const> names;  // nomes do enum, na ordem dos valores
    UiStyleValue (*get)(const UiElement&);
    void (*set)(UiElement&, const UiStyleValue&);
};

#define MOTOR_UI_PROP(field, kind, inherited, names) \
    Property { #field, Kind::kind, inherited, names, &GetField<&UiElement::field>, &SetField<&UiElement::field> }

// A tabela: a ordem é a da gravação. Herdadas: as de texto, como no CSS.
const Property kProperties[] = {
    MOTOR_UI_PROP(placement, Enum, false, kPlacementNames),
    MOTOR_UI_PROP(anchor, Enum, false, kAnchorNames),
    MOTOR_UI_PROP(position, Vec2, false, {}),
    MOTOR_UI_PROP(width, Length, false, {}),
    MOTOR_UI_PROP(height, Length, false, {}),
    MOTOR_UI_PROP(margin, Edges, false, {}),
    MOTOR_UI_PROP(grow, Float, false, {}),
    MOTOR_UI_PROP(shrink, Float, false, {}),
    MOTOR_UI_PROP(alignSelf, Enum, false, kAlignNames),
    MOTOR_UI_PROP(direction, Enum, false, kDirectionNames),
    MOTOR_UI_PROP(justify, Enum, false, kJustifyNames),
    MOTOR_UI_PROP(alignItems, Enum, false, kAlignNames),
    MOTOR_UI_PROP(gap, Float, false, {}),
    MOTOR_UI_PROP(padding, Edges, false, {}),
    MOTOR_UI_PROP(overflow, Enum, false, kOverflowNames),
    MOTOR_UI_PROP(color, Vec4, false, {}),
    MOTOR_UI_PROP(font, File, true, {}),
    MOTOR_UI_PROP(fontSize, Float, true, {}),
    MOTOR_UI_PROP(textColor, Vec4, true, {}),
    MOTOR_UI_PROP(align, Enum, true, kTextAlignNames),
    MOTOR_UI_PROP(verticalAlign, Enum, false, kVerticalAlignNames),
    MOTOR_UI_PROP(wrap, Bool, false, {}),
    MOTOR_UI_PROP(normalColor, Vec4, false, {}),
    MOTOR_UI_PROP(hoverColor, Vec4, false, {}),
    MOTOR_UI_PROP(pressedColor, Vec4, false, {}),
    MOTOR_UI_PROP(radius, Float, false, {}),
    MOTOR_UI_PROP(borderWidth, Float, false, {}),
    MOTOR_UI_PROP(borderColor, Vec4, false, {}),
    MOTOR_UI_PROP(slice, Edges, false, {}),
    MOTOR_UI_PROP(opacity, Float, false, {}),
    MOTOR_UI_PROP(textOutlineWidth, Float, false, {}),
    MOTOR_UI_PROP(textOutlineColor, Vec4, false, {}),
    MOTOR_UI_PROP(textShadowOffset, Vec2, false, {}),
    MOTOR_UI_PROP(textShadowColor, Vec4, false, {}),
    MOTOR_UI_PROP(scale, Float, false, {}),
    MOTOR_UI_PROP(transition, Float, false, {}),
};
#undef MOTOR_UI_PROP

constexpr int kPropertyCount = static_cast<int>(std::size(kProperties));

const Property* PropertyAt(int property) {
    return property >= 0 && property < kPropertyCount ? &kProperties[property] : nullptr;
}

// Tamanho: número = pixels, "50%" = porcentagem do pai, "300px", "auto".
bool ReadLength(const UiJson& json, UiLength& out) {
    if (json.is_number()) {
        out = UiLength::Px(json.get<float>());
        return true;
    }
    if (!json.is_string()) return false;
    std::string text = json.get<std::string>();
    if (text == "auto") {
        out = {};
        return true;
    }
    UiUnit unit = UiUnit::Px;
    if (text.ends_with('%')) {
        unit = UiUnit::Percent;
        text.pop_back();
    } else if (text.ends_with("px")) {
        text.resize(text.size() - 2);
    }
    try {
        size_t used = 0;
        const float v = std::stof(text, &used);
        if (used != text.size()) return false;
        out = {v, unit};
        return true;
    } catch (...) {
        return false;
    }
}

UiJson LengthJson(const UiLength& l) {
    switch (l.unit) {
        case UiUnit::Auto: return "auto";
        case UiUnit::Px: return l.value;
        case UiUnit::Percent: return std::format("{}%", l.value);
    }
    return "auto";
}

bool AllNumbers(const UiJson& json, size_t count) {
    if (!json.is_array() || json.size() != count) return false;
    return std::all_of(json.begin(), json.end(), [](const UiJson& v) { return v.is_number(); });
}

// Lados: um número, [vertical, horizontal] ou [cima, direita, baixo, esquerda].
bool ReadEdges(const UiJson& json, UiEdges& out) {
    if (json.is_number()) {
        out = UiEdges::All(json.get<float>());
        return true;
    }
    if (AllNumbers(json, 2)) {
        out = UiEdges::Symmetric(json[0].get<float>(), json[1].get<float>());
        return true;
    }
    if (AllNumbers(json, 4)) {
        out = {json[0].get<float>(), json[1].get<float>(), json[2].get<float>(), json[3].get<float>()};
        return true;
    }
    return false;
}

UiJson EdgesJson(const UiEdges& e) {
    if (e.top == e.right && e.top == e.bottom && e.top == e.left) return e.top;
    return UiJson::array({e.top, e.right, e.bottom, e.left});
}

template <class V> bool ReadVec(const UiJson& json, V& out) {
    if (!AllNumbers(json, static_cast<size_t>(V::length()))) return false;
    for (int i = 0; i < V::length(); ++i) out[i] = json[static_cast<size_t>(i)].template get<float>();
    return true;
}

bool IsIdentChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool ParseCompound(std::string_view text, UiSelector& out, std::string* error) {
    out = {};
    size_t i = 0;
    auto ident = [&]() {
        const size_t start = i;
        while (i < text.size() && IsIdentChar(text[i])) ++i;
        return std::string(text.substr(start, i - start));
    };
    if (i < text.size() && text[i] == '*') {
        ++i;
    } else if (i < text.size() && IsIdentChar(text[i])) {
        out.type = ident();
        bool known = false;
        for (UiElementType t : {UiElementType::Panel, UiElementType::Text, UiElementType::Image, UiElementType::Button}) {
            known = known || out.type == UiElementTypeName(t);
        }
        if (!known) {
            SetError(error, std::format("tipo \"{}\" desconhecido", out.type));
            return false;
        }
    }
    while (i < text.size()) {
        const char c = text[i];
        if (c == '.' || c == '#' || c == ':') {
            ++i;
            const std::string id = ident();
            if (id.empty()) {
                SetError(error, std::format("nome faltando depois de '{}'", c));
                return false;
            }
            if (c == '.') {
                out.classes.push_back(id);
            } else if (c == '#') {
                if (!out.name.empty()) {
                    SetError(error, "dois #nome no mesmo seletor");
                    return false;
                }
                out.name = id;
            } else if (id == "hover") {
                out.states |= kUiStateHover;
            } else if (id == "active") {
                out.states |= kUiStateActive;
            } else if (id == "disabled") {
                out.states |= kUiStateDisabled;
            } else if (id == "focus") {
                out.states |= kUiStateFocus;
            } else {
                SetError(error, std::format("estado \":{}\" desconhecido", id));
                return false;
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '>' || c == '+' || c == '~') {
            SetError(error, "combinadores (espaço, >, +, ~) não são suportados: use um seletor composto");
            return false;
        }
        SetError(error, std::format("caractere '{}' inesperado", c));
        return false;
    }
    if (text.empty()) {
        SetError(error, "seletor vazio");
        return false;
    }
    return true;
}

UiStyleSheet ParseDefaultSheet() {
    // O visual de sempre (os mesmos valores de UiElement::Make), num lugar só para mudar o tema da engine.
    constexpr const char* kJson = R"({"format": "motor-style", "version": 1, "rules": [
        {"selector": "Panel", "color": [0, 0, 0, 0]},
        {"selector": "Text", "wrap": true},
        {"selector": "Image", "width": 100, "height": 100},
        {"selector": "Button", "padding": [12, 24], "normalColor": [0.18, 0.22, 0.30, 0.92],
         "hoverColor": [0.26, 0.33, 0.46, 0.96], "pressedColor": [0.12, 0.15, 0.21, 1]}]})";
    UiStyleSheet sheet;
    std::string error;
    if (!UiStyleSheetFromJson(kJson, sheet, &error)) log::Error("UI: folha padrão: {}", error);
    return sheet;
}

const UiElement& TypeDefault(UiElementType type) {
    static const std::array<UiElement, 4> defaults = {UiElement::Make(UiElementType::Panel),
                                                      UiElement::Make(UiElementType::Text),
                                                      UiElement::Make(UiElementType::Image),
                                                      UiElement::Make(UiElementType::Button)};
    return defaults[static_cast<size_t>(type)];
}

UiVisual Lerp(const UiVisual& a, const UiVisual& b, float t) {
    return {glm::mix(a.background, b.background, t), glm::mix(a.textColor, b.textColor, t),
            glm::mix(a.borderColor, b.borderColor, t), a.opacity + (b.opacity - a.opacity) * t,
            a.scale + (b.scale - a.scale) * t};
}

struct FlatRule {
    const UiSelector* selector;
    const UiStyleDeclarations* declarations;
    int origin;  // 0: folha da engine; 1: folhas do documento (sempre ganham da engine)
    int order;
};

struct Cascade {
    UiDocumentData& doc;
    std::vector<FlatRule> rules;
    std::vector<const UiStyleValue*> chosen;
    struct Match {
        int origin, specificity, order;
        const UiStyleDeclarations* declarations;
    };
    std::vector<Match> matches;  // reaproveitado

    std::shared_ptr<const FontData> FontOf(const UiFont& font) const {
        if (font.IsDefault()) return nullptr;
        if (!font.guid.empty()) {
            if (const auto it = doc.fonts.find("g:" + font.guid); it != doc.fonts.end()) return it->second;
        }
        if (!font.path.empty()) {
            if (const auto it = doc.fonts.find("p:" + font.path); it != doc.fonts.end()) return it->second;
        }
        return nullptr;
    }

    void Element(UiElement& e, const UiElement* parent) {
        const uint8_t states = UiElementStates(e);
        matches.clear();
        for (const FlatRule& r : rules) {
            if (r.selector->Matches(e, states)) matches.push_back({r.origin, r.selector->Specificity(), r.order, r.declarations});
        }
        std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
            if (a.origin != b.origin) return a.origin < b.origin;
            if (a.specificity != b.specificity) return a.specificity < b.specificity;
            return a.order < b.order;
        });
        std::fill(chosen.begin(), chosen.end(), nullptr);
        for (const Match& m : matches) {
            for (const UiStyleDeclaration& d : m.declarations->items) chosen[static_cast<size_t>(d.property)] = &d.value;
        }
        for (const UiStyleDeclaration& d : e.inlineStyle.items) chosen[static_cast<size_t>(d.property)] = &d.value;

        const UiElement& def = TypeDefault(e.type);
        for (int p = 0; p < kPropertyCount; ++p) {
            const Property& prop = kProperties[p];
            if (const UiStyleValue* v = chosen[static_cast<size_t>(p)]) prop.set(e, *v);
            else if (prop.inherited && parent) prop.set(e, prop.get(*parent));
            else prop.set(e, prop.get(def));
        }
        e.fontData = FontOf(e.font);  // carregada por ResolveUiDocumentAssets; nula = a padrão
        e.styleStates = states;
        e.styleDirty = false;

        // Transição: o alvo mudou e há tempo para chegar nele (só no Play; no editor, e no primeiro cálculo, pula).
        UiTransitionState& a = e.animation;
        const UiVisual target = e.Visual();
        if (!a.started || !(target == a.to)) {
            if (doc.animate && a.started && e.transition > 0.0f) {
                a.from = a.active ? a.now : a.to;
                a.now = a.from;
                a.time = 0.0f;
                a.duration = e.transition;
                a.active = true;
            } else {
                a.active = false;
            }
            a.to = target;
            a.started = true;
        }
        for (UiElement& child : e.children) Element(child, &e);
    }
};

bool AnyStyleChange(const std::vector<UiElement>& elements) {
    for (const UiElement& e : elements) {
        if (e.styleDirty || e.styleStates != UiElementStates(e) || AnyStyleChange(e.children)) return true;
    }
    return false;
}

void Advance(std::vector<UiElement>& elements, float dt) {
    for (UiElement& e : elements) {
        UiTransitionState& a = e.animation;
        if (a.active) {
            a.time += dt;
            const float k = a.duration > 0.0f ? std::min(1.0f, a.time / a.duration) : 1.0f;
            const float eased = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);  // ease-out: rápido no começo
            a.now = Lerp(a.from, a.to, eased);
            if (k >= 1.0f) {
                a.now = a.to;
                a.active = false;
            }
        }
        Advance(e.children, dt);
    }
}

} // namespace

// --- Tabela ----------------------------------------------------------------------------------------------------------

int UiStylePropertyCount() { return kPropertyCount; }

int FindUiStyleProperty(std::string_view name) {
    for (int i = 0; i < kPropertyCount; ++i) {
        if (name == kProperties[i].name) return i;
    }
    return -1;
}

const char* UiStylePropertyName(int property) {
    const Property* p = PropertyAt(property);
    return p ? p->name : "";
}

bool UiStylePropertyInherited(int property) {
    const Property* p = PropertyAt(property);
    return p && p->inherited;
}

bool ReadUiStyleJson(int property, const UiJson& json, UiStyleValue& out) {
    const Property* p = PropertyAt(property);
    if (!p) return false;
    switch (p->kind) {
        case Kind::Bool:
            if (!json.is_boolean()) return false;
            out = json.get<bool>();
            return true;
        case Kind::Float:
            if (!json.is_number()) return false;
            out = json.get<float>();
            return true;
        case Kind::Vec2: {
            glm::vec2 v{0.0f};
            if (!ReadVec(json, v)) return false;
            out = v;
            return true;
        }
        case Kind::Vec4: {
            glm::vec4 v{0.0f};
            if (!ReadVec(json, v)) return false;
            out = v;
            return true;
        }
        case Kind::Length: {
            UiLength l;
            if (!ReadLength(json, l)) return false;
            out = l;
            return true;
        }
        case Kind::Edges: {
            UiEdges e;
            if (!ReadEdges(json, e)) return false;
            out = e;
            return true;
        }
        case Kind::Enum: {
            if (!json.is_string()) return false;
            const std::string text = json.get<std::string>();
            for (size_t i = 0; i < p->names.size(); ++i) {
                if (text == p->names[i]) {
                    out = static_cast<int>(i);
                    return true;
                }
            }
            return false;
        }
        case Kind::File: {
            // Fonte: {"guid": "...", "path": "Assets/..."} ou só o caminho (como escreve um agente pelo MCP).
            UiFont ref;
            if (json.is_string()) {
                ref.path = json.get<std::string>();
            } else if (json.is_object()) {
                ref.guid = json.value("guid", std::string());
                ref.path = json.value("path", std::string());
            } else if (!json.is_null()) {
                return false;
            }
            out = ref;
            return true;
        }
    }
    return false;
}

UiJson UiStyleValueJson(int property, const UiStyleValue& value) {
    const Property* p = PropertyAt(property);
    if (!p) return nullptr;
    return std::visit(
        [&](const auto& v) -> UiJson {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, float>) {
                return v;
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                return UiJson::array({v.x, v.y});
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return UiJson::array({v.x, v.y, v.z, v.w});
            } else if constexpr (std::is_same_v<T, UiLength>) {
                return LengthJson(v);
            } else if constexpr (std::is_same_v<T, UiEdges>) {
                return EdgesJson(v);
            } else if constexpr (std::is_same_v<T, int>) {
                return v >= 0 && static_cast<size_t>(v) < p->names.size() ? UiJson(p->names[static_cast<size_t>(v)])
                                                                         : UiJson(v);
            } else {
                return UiJson{{"guid", v.guid}, {"path", v.path}};
            }
        },
        value);
}

bool ParseUiStyleValue(int property, std::string_view json, UiStyleValue& out) {
    const UiJson parsed = UiJson::parse(json, nullptr, false);
    return !parsed.is_discarded() && ReadUiStyleJson(property, parsed, out);
}

std::string UiStyleValueToJson(int property, const UiStyleValue& value) { return UiStyleValueJson(property, value).dump(); }

UiStyleValue GetUiStyleValue(const UiElement& element, int property) {
    const Property* p = PropertyAt(property);
    return p ? p->get(element) : UiStyleValue{};
}

void SetUiStyleValue(UiElement& element, int property, const UiStyleValue& value) {
    if (const Property* p = PropertyAt(property)) p->set(element, value);
}

bool IsUiStyleKey(std::string_view key) { return key == "size" || FindUiStyleProperty(key) >= 0; }

void ReadUiStyleDeclarations(const UiJson& object, UiStyleDeclarations& out, std::vector<std::string>* invalid) {
    if (!object.is_object()) return;
    if (const auto it = object.find("size"); it != object.end()) {  // atalho (e o da versão 1): pixels
        glm::vec2 size{0.0f};
        if (ReadVec(*it, size)) {
            out.Set(FindUiStyleProperty("width"), UiLength::Px(size.x));
            out.Set(FindUiStyleProperty("height"), UiLength::Px(size.y));
        } else if (invalid) {
            invalid->push_back("size");
        }
    }
    for (int p = 0; p < kPropertyCount; ++p) {
        const auto it = object.find(kProperties[p].name);
        if (it == object.end()) continue;
        UiStyleValue value;
        if (ReadUiStyleJson(p, *it, value)) out.Set(p, value);
        else if (invalid) invalid->push_back(kProperties[p].name);
    }
}

void WriteUiStyleDeclarations(const UiStyleDeclarations& declarations, UiJson& object) {
    for (const UiStyleDeclaration& d : declarations.items) {
        if (const Property* p = PropertyAt(d.property)) object[p->name] = UiStyleValueJson(d.property, d.value);
    }
}

// --- Declarações -----------------------------------------------------------------------------------------------------

void UiStyleDeclarations::Set(int property, const UiStyleValue& value) {
    if (!PropertyAt(property)) return;
    const auto it = std::lower_bound(items.begin(), items.end(), property,
                                     [](const UiStyleDeclaration& d, int p) { return d.property < p; });
    if (it != items.end() && it->property == property) it->value = value;
    else items.insert(it, {property, value});
}

bool UiStyleDeclarations::Set(std::string_view property, std::string_view json) {
    const int p = FindUiStyleProperty(property);
    UiStyleValue value;
    if (p < 0 || !ParseUiStyleValue(p, json, value)) return false;
    Set(p, value);
    return true;
}

bool UiStyleDeclarations::Remove(std::string_view property) {
    const int p = FindUiStyleProperty(property);
    return std::erase_if(items, [p](const UiStyleDeclaration& d) { return d.property == p; }) > 0;
}

const UiStyleValue* UiStyleDeclarations::Find(int property) const {
    for (const UiStyleDeclaration& d : items) {
        if (d.property == property) return &d.value;
    }
    return nullptr;
}

bool UiElement::SetStyleValue(std::string_view property, const UiStyleValue& value) {
    const int p = FindUiStyleProperty(property);
    if (p < 0 || value.index() != static_cast<size_t>(kProperties[p].kind)) return false;
    inlineStyle.Set(p, value);
    kProperties[p].set(*this, value);  // na hora (também num documento montado em código)
    styleDirty = true;
    return true;
}

bool UiElement::SetStyle(std::string_view property, std::string_view json) {
    const int p = FindUiStyleProperty(property);
    UiStyleValue value;
    return p >= 0 && ParseUiStyleValue(p, json, value) && SetStyleValue(property, value);
}

bool UiElement::ClearStyle(std::string_view property) {
    if (!inlineStyle.Remove(property)) return false;
    styleDirty = true;
    return true;
}

// --- Seletores -------------------------------------------------------------------------------------------------------

uint8_t UiElementStates(const UiElement& e) {
    uint8_t s = 0;
    if (e.hovered) s |= kUiStateHover;
    if (e.pressed) s |= kUiStateActive;
    if (!e.interactable) s |= kUiStateDisabled;
    if (e.focused) s |= kUiStateFocus;
    return s;
}

int UiSelector::Specificity() const {
    const int ids = name.empty() ? 0 : 1;
    const int classLike = static_cast<int>(classes.size()) + std::popcount(states);
    return ids * 10000 + classLike * 100 + (type.empty() ? 0 : 1);
}

bool UiSelector::Matches(const UiElement& e, uint8_t elementStates) const {
    if (!type.empty() && type != UiElementTypeName(e.type)) return false;
    if (!name.empty() && name != e.name) return false;
    if ((elementStates & states) != states) return false;
    for (const std::string& c : classes) {
        if (std::find(e.classes.begin(), e.classes.end(), c) == e.classes.end()) return false;
    }
    return true;
}

bool ParseUiSelectors(std::string_view text, std::vector<UiSelector>& out, std::string* error) {
    std::vector<UiSelector> list;
    while (true) {
        const size_t comma = text.find(',');
        const std::string_view item = Trim(text.substr(0, comma));
        UiSelector selector;
        std::string why;
        if (!ParseCompound(item, selector, &why)) {
            SetError(error, std::format("seletor \"{}\": {}", item, why));
            return false;
        }
        list.push_back(std::move(selector));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    out = std::move(list);
    return true;
}

// --- Folha -----------------------------------------------------------------------------------------------------------

std::string UiStyleSheetToJson(const UiStyleSheet& sheet) {
    UiJson rules = UiJson::array();
    for (const UiStyleRule& rule : sheet.rules) {
        UiJson json{{"selector", rule.selector}};
        WriteUiStyleDeclarations(rule.declarations, json);
        rules.push_back(std::move(json));
    }
    const UiJson json{{"format", kUiStyleFormat}, {"version", kUiStyleVersion}, {"rules", std::move(rules)}};
    return json.dump(2) + "\n";
}

bool UiStyleSheetFromJson(std::string_view text, UiStyleSheet& out, std::string* error,
                          std::vector<std::string>* warnings) {
    const UiJson json = UiJson::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        SetError(error, "JSON inválido");
        return false;
    }
    if (json.value("format", std::string()) != kUiStyleFormat) {
        SetError(error, std::format("não é uma folha de estilo (format \"{}\" esperado)", kUiStyleFormat));
        return false;
    }
    const int version = json.value("version", 1);
    if (version > kUiStyleVersion) {
        SetError(error, std::format("versão {} é mais nova que a desta engine ({})", version, kUiStyleVersion));
        return false;
    }
    auto warn = [&](std::string message) {
        if (warnings) warnings->push_back(std::move(message));
    };
    UiStyleSheet sheet;
    const auto rules = json.find("rules");
    if (rules != json.end() && rules->is_array()) {
        for (size_t i = 0; i < rules->size(); ++i) {
            const UiJson& item = (*rules)[i];
            const std::string where = std::format("regra {}", i + 1);
            if (!item.is_object() || !item.contains("selector") || !item["selector"].is_string()) {
                warn(where + ": sem \"selector\", ignorada");
                continue;
            }
            UiStyleRule rule;
            rule.selector = item["selector"].get<std::string>();
            std::string why;
            if (!ParseUiSelectors(rule.selector, rule.selectors, &why)) {
                warn(std::format("{}: {}; ignorada", where, why));
                continue;
            }
            for (const auto& [key, value] : item.items()) {
                if (key != "selector" && !IsUiStyleKey(key)) warn(std::format("{} ({}): propriedade \"{}\" desconhecida", where, rule.selector, key));
            }
            std::vector<std::string> invalid;
            ReadUiStyleDeclarations(item, rule.declarations, &invalid);
            for (const std::string& key : invalid) {
                warn(std::format("{} ({}): valor de \"{}\" inválido, ignorado", where, rule.selector, key));
            }
            sheet.rules.push_back(std::move(rule));
        }
    }
    out = std::move(sheet);
    return true;
}

bool SaveUiStyleSheet(const std::filesystem::path& file, const UiStyleSheet& sheet, std::string* error) {
    return WriteFileAtomic(file, UiStyleSheetToJson(sheet), error);
}

bool LoadUiStyleSheet(const std::filesystem::path& file, UiStyleSheet& out, std::string* error) {
    const auto text = ReadTextFile(file);
    if (!text) {
        SetError(error, "não abriu " + ToUtf8(file));
        return false;
    }
    std::vector<std::string> warnings;
    if (!UiStyleSheetFromJson(*text, out, error, &warnings)) return false;
    for (const std::string& w : warnings) log::Warn("UI: {}: {}", ToUtf8(file.filename()), w);
    return true;
}

const UiStyleSheet& DefaultUiStyleSheet() {
    static const UiStyleSheet sheet = ParseDefaultSheet();
    return sheet;
}

// --- Cascata ---------------------------------------------------------------------------------------------------------

void ComputeUiStyles(UiDocumentData& document) {
    Cascade cascade{document, {}, std::vector<const UiStyleValue*>(static_cast<size_t>(kPropertyCount)), {}};
    int order = 0;
    auto add = [&](const UiStyleSheet& sheet, int origin) {
        for (const UiStyleRule& rule : sheet.rules) {
            for (const UiSelector& s : rule.selectors) cascade.rules.push_back({&s, &rule.declarations, origin, order});
            ++order;
        }
    };
    add(DefaultUiStyleSheet(), 0);
    for (const auto& sheet : document.sheets) {
        if (sheet) add(*sheet, 1);
    }
    for (UiElement& e : document.elements) cascade.Element(e, nullptr);
    document.styled = true;
    document.styleDirty = false;
    ++document.styleRuns;
}

bool UpdateUiStyles(UiDocumentData& document) {
    if (!document.styled) return false;
    if (!document.styleDirty && !AnyStyleChange(document.elements)) return false;
    ComputeUiStyles(document);
    return true;
}

void AdvanceUiTransitions(UiDocumentData& document, float dt) {
    document.animate = true;
    Advance(document.elements, dt);
}

} // namespace motor
