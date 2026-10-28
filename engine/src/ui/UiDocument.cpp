#include "motor/ui/UiDocument.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/common/FileRead.h"
#include "motor/common/FileWrite.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/ui/Font.h"

#include <nlohmann/json.hpp>

#include <format>

namespace motor {

namespace {
using Json = nlohmann::ordered_json;

constexpr UiElementType kTypes[] = {UiElementType::Panel, UiElementType::Text, UiElementType::Image,
                                    UiElementType::Button};
constexpr UiAnchor kAnchors[] = {UiAnchor::TopLeft,    UiAnchor::Top,    UiAnchor::TopRight,
                                 UiAnchor::Left,       UiAnchor::Center, UiAnchor::Right,
                                 UiAnchor::BottomLeft, UiAnchor::Bottom, UiAnchor::BottomRight,
                                 UiAnchor::Stretch};
constexpr UiTextAlign kAligns[] = {UiTextAlign::Left, UiTextAlign::Center, UiTextAlign::Right};
constexpr UiVerticalAlign kVerticalAligns[] = {UiVerticalAlign::Top, UiVerticalAlign::Middle, UiVerticalAlign::Bottom};
constexpr UiPlacement kPlacements[] = {UiPlacement::Flow, UiPlacement::Absolute};
constexpr UiDirection kDirections[] = {UiDirection::Column, UiDirection::Row};
constexpr UiJustify kJustifies[] = {UiJustify::Start, UiJustify::Center, UiJustify::End, UiJustify::SpaceBetween,
                                    UiJustify::SpaceAround};
constexpr UiAlign kAlignments[] = {UiAlign::Auto, UiAlign::Start, UiAlign::Center, UiAlign::End, UiAlign::Stretch};
constexpr UiOverflow kOverflows[] = {UiOverflow::Visible, UiOverflow::Hidden, UiOverflow::Scroll};

const char* VerticalAlignName(UiVerticalAlign v) {
    switch (v) {
        case UiVerticalAlign::Top: return "Top";
        case UiVerticalAlign::Middle: return "Middle";
        case UiVerticalAlign::Bottom: return "Bottom";
    }
    return "Middle";
}
const char* PlacementName(UiPlacement p) { return p == UiPlacement::Absolute ? "Absolute" : "Flow"; }
const char* DirectionName(UiDirection d) { return d == UiDirection::Row ? "Row" : "Column"; }
const char* JustifyName(UiJustify j) {
    switch (j) {
        case UiJustify::Start: return "Start";
        case UiJustify::Center: return "Center";
        case UiJustify::End: return "End";
        case UiJustify::SpaceBetween: return "SpaceBetween";
        case UiJustify::SpaceAround: return "SpaceAround";
    }
    return "Start";
}
const char* AlignName(UiAlign a) {
    switch (a) {
        case UiAlign::Auto: return "Auto";
        case UiAlign::Start: return "Start";
        case UiAlign::Center: return "Center";
        case UiAlign::End: return "End";
        case UiAlign::Stretch: return "Stretch";
    }
    return "Auto";
}
const char* OverflowName(UiOverflow o) {
    switch (o) {
        case UiOverflow::Visible: return "Visible";
        case UiOverflow::Hidden: return "Hidden";
        case UiOverflow::Scroll: return "Scroll";
    }
    return "Visible";
}

// Tamanho: número = pixels, "50%" = porcentagem do pai, "auto".
Json Length(const UiLength& l) {
    switch (l.unit) {
        case UiUnit::Auto: return "auto";
        case UiUnit::Px: return l.value;
        case UiUnit::Percent: return std::format("{}%", l.value);
    }
    return "auto";
}

void ReadLength(const Json& json, const char* key, UiLength& out) {
    const auto it = json.find(key);
    if (it == json.end()) return;
    if (it->is_number()) {
        out = UiLength::Px(it->get<float>());
    } else if (it->is_string()) {
        std::string text = it->get<std::string>();
        if (text == "auto") {
            out = {};
            return;
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
            if (used == text.size()) out = {v, unit};
        } catch (...) {
        }
    }
}

// Lados: um número, [vertical, horizontal] ou [cima, direita, baixo, esquerda].
Json Edges(const UiEdges& e) {
    if (e.top == e.right && e.top == e.bottom && e.top == e.left) return e.top;
    return Json::array({e.top, e.right, e.bottom, e.left});
}

void ReadEdges(const Json& json, const char* key, UiEdges& out) {
    const auto it = json.find(key);
    if (it == json.end()) return;
    if (it->is_number()) {
        out = UiEdges::All(it->get<float>());
        return;
    }
    if (!it->is_array()) return;
    for (const Json& v : *it) {
        if (!v.is_number()) return;
    }
    if (it->size() == 2) out = UiEdges::Symmetric((*it)[0].get<float>(), (*it)[1].get<float>());
    if (it->size() == 4) out = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
}

void ReadFloat(const Json& json, const char* key, float& out) {
    if (const auto it = json.find(key); it != json.end() && it->is_number()) out = it->get<float>();
}

Json Vec(const glm::vec2& v) { return Json::array({v.x, v.y}); }
Json Vec(const glm::vec4& v) { return Json::array({v.x, v.y, v.z, v.w}); }

template <class V> void ReadVec(const Json& json, const char* key, V& out) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array() || it->size() != static_cast<size_t>(V::length())) return;
    for (int i = 0; i < V::length(); ++i) {
        if ((*it)[i].is_number()) out[i] = (*it)[i].template get<float>();
    }
}

template <class E, size_t N> void ReadEnum(const Json& json, const char* key, const E (&values)[N],
                                           const char* (*name)(E), E& out) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) return;
    for (E e : values) {
        if (it->get<std::string>() == name(e)) out = e;
    }
}

// Fonte ou imagem: {"guid": "...", "path": "Assets/..."} ou só o caminho (como escreve um agente pelo MCP).
Json FileRef(const UiFont& ref) { return Json{{"guid", ref.guid}, {"path", ref.path}}; }

void ReadFileRef(const Json& json, const char* key, UiFont& out) {
    const auto it = json.find(key);
    if (it == json.end()) return;
    out = {};
    if (it->is_string()) {
        out.path = it->get<std::string>();
    } else if (it->is_object()) {
        out.guid = it->value("guid", std::string());
        out.path = it->value("path", std::string());
    }
}

Json ElementToJson(const UiElement& e) {
    const UiElement def = UiElement::Make(e.type);
    Json json{{"type", UiElementTypeName(e.type)}};
    if (!e.name.empty()) json["name"] = e.name;
    if (!e.classes.empty()) json["classes"] = e.classes;
    if (e.placement != def.placement) json["placement"] = PlacementName(e.placement);
    if (e.placement == UiPlacement::Absolute) {
        json["anchor"] = UiAnchorName(e.anchor);
        json["position"] = Vec(e.position);
    }
    if (e.width != def.width) json["width"] = Length(e.width);
    if (e.height != def.height) json["height"] = Length(e.height);
    if (e.margin != def.margin) json["margin"] = Edges(e.margin);
    if (e.grow != def.grow) json["grow"] = e.grow;
    if (e.shrink != def.shrink) json["shrink"] = e.shrink;
    if (e.alignSelf != def.alignSelf) json["alignSelf"] = AlignName(e.alignSelf);
    if (e.visible != def.visible) json["visible"] = e.visible;
    if (e.direction != def.direction) json["direction"] = DirectionName(e.direction);
    if (e.justify != def.justify) json["justify"] = JustifyName(e.justify);
    if (e.alignItems != def.alignItems) json["alignItems"] = AlignName(e.alignItems);
    if (e.gap != def.gap) json["gap"] = e.gap;
    if (e.padding != def.padding) json["padding"] = Edges(e.padding);
    if (e.overflow != def.overflow) json["overflow"] = OverflowName(e.overflow);
    if (e.color != def.color) json["color"] = Vec(e.color);
    if (!e.image.IsDefault()) json["image"] = FileRef(e.image);
    if (e.text != def.text) json["text"] = e.text;
    if (!e.font.IsDefault()) json["font"] = FileRef(e.font);
    if (e.fontSize != def.fontSize) json["fontSize"] = e.fontSize;
    if (e.textColor != def.textColor) json["textColor"] = Vec(e.textColor);
    if (e.align != def.align) json["align"] = UiTextAlignName(e.align);
    if (e.verticalAlign != def.verticalAlign) json["verticalAlign"] = VerticalAlignName(e.verticalAlign);
    if (e.wrap != def.wrap) json["wrap"] = e.wrap;
    if (e.type == UiElementType::Button) {
        if (e.normalColor != def.normalColor) json["normalColor"] = Vec(e.normalColor);
        if (e.hoverColor != def.hoverColor) json["hoverColor"] = Vec(e.hoverColor);
        if (e.pressedColor != def.pressedColor) json["pressedColor"] = Vec(e.pressedColor);
        if (e.interactable != def.interactable) json["interactable"] = e.interactable;
    }
    if (!e.children.empty()) {
        Json children = Json::array();
        for (const UiElement& child : e.children) children.push_back(ElementToJson(child));
        json["children"] = std::move(children);
    }
    return json;
}

void ElementsFromJson(const Json& list, int version, std::vector<UiElement>& out, std::vector<std::string>* warnings,
                      const std::string& where);

UiElement ElementFromJson(const Json& json, int version, std::vector<std::string>* warnings, const std::string& where) {
    UiElementType type = UiElementType::Panel;
    const std::string typeName = json.value("type", std::string("Panel"));
    bool known = false;
    for (UiElementType t : kTypes) {
        if (typeName == UiElementTypeName(t)) {
            type = t;
            known = true;
        }
    }
    if (!known && warnings) warnings->push_back(std::format("{}: tipo \"{}\" desconhecido, lido como Panel", where, typeName));

    UiElement e = UiElement::Make(type);
    if (version < 2) {
        // Antes do flex: tudo por âncora, 200x60 sem "size", texto sem quebra nem preenchimento.
        e.placement = UiPlacement::Absolute;
        e.width = UiLength::Px(200.0f);
        e.height = UiLength::Px(60.0f);
        e.wrap = false;
        e.padding = {};
    }
    e.name = json.value("name", std::string());
    if (const auto it = json.find("classes"); it != json.end() && it->is_array()) {
        for (const Json& c : *it) {
            if (c.is_string()) e.classes.push_back(c.get<std::string>());
        }
    }
    ReadEnum(json, "placement", kPlacements, PlacementName, e.placement);
    ReadEnum(json, "anchor", kAnchors, UiAnchorName, e.anchor);
    ReadVec(json, "position", e.position);
    glm::vec2 size{0.0f};
    if (json.contains("size")) {  // atalho (e o da versão 1): largura e altura em pixels
        size = {e.width.value, e.height.value};
        ReadVec(json, "size", size);
        e.width = UiLength::Px(size.x);
        e.height = UiLength::Px(size.y);
    }
    ReadLength(json, "width", e.width);
    ReadLength(json, "height", e.height);
    ReadEdges(json, "margin", e.margin);
    ReadFloat(json, "grow", e.grow);
    ReadFloat(json, "shrink", e.shrink);
    ReadEnum(json, "alignSelf", kAlignments, AlignName, e.alignSelf);
    e.visible = json.value("visible", e.visible);
    ReadEnum(json, "direction", kDirections, DirectionName, e.direction);
    ReadEnum(json, "justify", kJustifies, JustifyName, e.justify);
    ReadEnum(json, "alignItems", kAlignments, AlignName, e.alignItems);
    ReadFloat(json, "gap", e.gap);
    ReadEdges(json, "padding", e.padding);
    ReadEnum(json, "overflow", kOverflows, OverflowName, e.overflow);
    ReadVec(json, "color", e.color);
    ReadFileRef(json, "image", e.image);
    if (const auto it = json.find("text"); it != json.end() && it->is_string()) e.text = it->get<std::string>();
    ReadFileRef(json, "font", e.font);
    if (const auto it = json.find("fontSize"); it != json.end() && it->is_number()) e.fontSize = it->get<float>();
    ReadVec(json, "textColor", e.textColor);
    ReadEnum(json, "align", kAligns, UiTextAlignName, e.align);
    ReadEnum(json, "verticalAlign", kVerticalAligns, VerticalAlignName, e.verticalAlign);
    e.wrap = json.value("wrap", e.wrap);
    ReadVec(json, "normalColor", e.normalColor);
    ReadVec(json, "hoverColor", e.hoverColor);
    ReadVec(json, "pressedColor", e.pressedColor);
    e.interactable = json.value("interactable", e.interactable);
    if (const auto it = json.find("children"); it != json.end()) {
        ElementsFromJson(*it, version, e.children, warnings, where + "/" + (e.name.empty() ? typeName : e.name));
    }
    return e;
}

void ElementsFromJson(const Json& list, int version, std::vector<UiElement>& out, std::vector<std::string>* warnings,
                      const std::string& where) {
    if (!list.is_array()) return;
    for (const Json& item : list) {
        if (item.is_object()) {
            out.push_back(ElementFromJson(item, version, warnings, where));
        } else if (warnings) {
            warnings->push_back(where + ": item que não é objeto ignorado");
        }
    }
}

bool SameElements(const std::vector<UiElement>& a, const std::vector<UiElement>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!a[i].SameContent(b[i])) return false;
    }
    return true;
}

template <class E> E* FindIn(std::vector<E>& list, std::string_view name) {
    for (E& e : list) {
        if (e.name == name) return &e;
        if (E* found = FindIn(e.children, name)) return found;
    }
    return nullptr;
}
template <class E> const E* FindIn(const std::vector<E>& list, std::string_view name) {
    for (const E& e : list) {
        if (e.name == name) return &e;
        if (const E* found = FindIn(e.children, name)) return found;
    }
    return nullptr;
}

// Arquivo movido no painel Projeto (o .meta vai junto): o GUID manda e o caminho acompanha.
void ResolveRef(UiFont& ref, AssetLibrary& assets) {
    if (ref.IsDefault()) return;
    if (!ref.guid.empty()) {
        const std::filesystem::path file = assets.Database().PathFor(ref.guid);
        if (const std::string key = file.empty() ? std::string() : assets.KeyForFile(file); !key.empty()) ref.path = key;
    } else if (!ref.path.empty()) {
        const std::filesystem::path file = assets.FileForKey(ref.path);
        std::error_code ec;
        if (!file.empty() && std::filesystem::is_regular_file(file, ec)) ref.guid = assets.Database().GuidFor(file);
    }
}

void ResolveElements(std::vector<UiElement>& list, AssetLibrary& assets) {
    for (UiElement& e : list) {
        ResolveRef(e.font, assets);
        ResolveRef(e.image, assets);
        e.fontData = nullptr;
        if (!e.font.IsDefault()) {
            std::filesystem::path file;
            if (!e.font.guid.empty()) file = assets.Database().PathFor(e.font.guid);
            if (file.empty() && !e.font.path.empty()) file = assets.FileForKey(e.font.path);
            std::string error;
            if (!file.empty()) e.fontData = LoadUiFont(file, &error);
            if (!e.fontData) {
                log::Warn("UI: fonte {} do elemento {}: {}; usando a padrão", e.font.path, e.name,
                          error.empty() ? std::string("não encontrada") : error);
            }
        }
        e.texture = nullptr;
        if (!e.image.IsDefault()) {
            std::string error;
            if (!e.image.guid.empty()) e.texture = assets.GetTextureByGuid(e.image.guid, &error);
            if (!e.texture && !e.image.path.empty()) e.texture = assets.GetTexture(e.image.path, &error);
            if (!e.texture) log::Warn("UI: imagem {} do elemento {}: {}", e.image.path, e.name, error);
        }
        ResolveElements(e.children, assets);
    }
}
} // namespace

const char* UiElementTypeName(UiElementType type) {
    switch (type) {
        case UiElementType::Panel: return "Panel";
        case UiElementType::Text: return "Text";
        case UiElementType::Image: return "Image";
        case UiElementType::Button: return "Button";
    }
    return "Panel";
}

UiElement UiElement::Make(UiElementType type) {
    UiElement e;
    e.type = type;
    switch (type) {
        case UiElementType::Panel: e.color = glm::vec4(0.0f); break;  // só agrupa, até ganhar uma cor
        case UiElementType::Text:
            e.text = "Texto";
            e.wrap = true;
            break;
        case UiElementType::Image:
            e.width = UiLength::Px(100.0f);
            e.height = UiLength::Px(100.0f);
            break;
        case UiElementType::Button:
            e.text = "Botão";
            e.padding = UiEdges::Symmetric(12.0f, 24.0f);
            break;
    }
    return e;
}

bool UiElement::HasBackground() const {
    switch (type) {
        case UiElementType::Text: return false;
        case UiElementType::Button: return true;
        case UiElementType::Panel:
        case UiElementType::Image: return color.a > 0.0f || texture != nullptr;
    }
    return false;
}

bool UiElement::BlocksPointer() const {
    return type == UiElementType::Button || type == UiElementType::Image ||
           (type == UiElementType::Panel && HasBackground());
}

glm::vec4 UiElement::BackgroundColor() const {
    if (type != UiElementType::Button) return color;
    const glm::vec4* state = &normalColor;
    if (interactable && pressed) state = &pressedColor;
    else if (interactable && hovered) state = &hoverColor;
    return *state * color;
}

const FontData* UiElement::Font() const { return fontData ? fontData.get() : DefaultUiFont(); }

bool UiElement::SameContent(const UiElement& o) const {
    return type == o.type && name == o.name && classes == o.classes && placement == o.placement &&
           anchor == o.anchor && position == o.position && width == o.width && height == o.height &&
           margin == o.margin && grow == o.grow && shrink == o.shrink && alignSelf == o.alignSelf &&
           visible == o.visible && direction == o.direction && justify == o.justify && alignItems == o.alignItems &&
           gap == o.gap && padding == o.padding && overflow == o.overflow && color == o.color && image == o.image &&
           text == o.text && font == o.font && fontSize == o.fontSize && textColor == o.textColor &&
           align == o.align && verticalAlign == o.verticalAlign && wrap == o.wrap &&
           normalColor == o.normalColor && hoverColor == o.hoverColor && pressedColor == o.pressedColor &&
           interactable == o.interactable && SameElements(children, o.children);
}

bool UiDocumentData::SameContent(const UiDocumentData& other) const { return SameElements(elements, other.elements); }
UiElement* UiDocumentData::Find(std::string_view name) { return name.empty() ? nullptr : FindIn(elements, name); }
const UiElement* UiDocumentData::Find(std::string_view name) const { return name.empty() ? nullptr : FindIn(elements, name); }

std::string UiDocumentToJson(const UiDocumentData& document) {
    Json elements = Json::array();
    for (const UiElement& e : document.elements) elements.push_back(ElementToJson(e));
    const Json json{{"format", kUiDocumentFormat}, {"version", kUiDocumentVersion}, {"elements", std::move(elements)}};
    return json.dump(2) + "\n";
}

bool UiDocumentFromJson(std::string_view text, UiDocumentData& out, std::string* error,
                        std::vector<std::string>* warnings) {
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        SetError(error, "JSON inválido");
        return false;
    }
    if (json.value("format", std::string()) != kUiDocumentFormat) {
        SetError(error, std::format("não é um documento de UI (format \"{}\" esperado)", kUiDocumentFormat));
        return false;
    }
    const int version = json.value("version", 1);
    if (version > kUiDocumentVersion) {
        SetError(error, std::format("versão {} é mais nova que a desta engine ({})", version, kUiDocumentVersion));
        return false;
    }
    UiDocumentData document;
    if (const auto it = json.find("elements"); it != json.end()) ElementsFromJson(*it, version, document.elements, warnings, "");
    out = std::move(document);
    return true;
}

bool SaveUiDocument(const std::filesystem::path& file, const UiDocumentData& document, std::string* error) {
    return WriteFileAtomic(file, UiDocumentToJson(document), error);
}

bool LoadUiDocument(const std::filesystem::path& file, UiDocumentData& out, std::string* error) {
    const auto text = ReadTextFile(file);
    if (!text) {
        SetError(error, "não abriu " + ToUtf8(file));
        return false;
    }
    std::vector<std::string> warnings;
    if (!UiDocumentFromJson(*text, out, error, &warnings)) return false;
    for (const std::string& w : warnings) log::Warn("UI: {}: {}", ToUtf8(file.filename()), w);
    return true;
}

void ResolveUiDocumentAssets(UiDocumentData& document, AssetLibrary& assets) {
    ResolveElements(document.elements, assets);
}

void ReloadUiDocument(UiDocumentRef& ref, AssetLibrary& assets) {
    ref.data = nullptr;
    if (ref.IsEmpty()) return;
    std::filesystem::path file;
    if (!ref.guid.empty()) file = assets.Database().PathFor(ref.guid);
    if (file.empty() && !ref.path.empty()) file = assets.FileForKey(ref.path);
    if (!file.empty()) {
        if (const std::string key = assets.KeyForFile(file); !key.empty()) ref.path = key;
        std::error_code ec;
        if (ref.guid.empty() && std::filesystem::is_regular_file(file, ec)) ref.guid = assets.Database().GuidFor(file);
    }
    auto data = std::make_shared<UiDocumentData>();
    std::string error;
    if (file.empty() || !LoadUiDocument(file, *data, &error)) {
        log::Warn("UI: documento {}: {}", ref.path.empty() ? ref.guid : ref.path,
                  error.empty() ? std::string("arquivo não encontrado") : error);
        return;
    }
    ResolveUiDocumentAssets(*data, assets);
    ref.data = std::move(data);
}

void SetUiDocumentFile(UiDocumentRef& ref, std::string_view key, AssetLibrary& assets) {
    ref = {};
    if (key.empty()) return;
    ref.path = std::string(key);
    ReloadUiDocument(ref, assets);
}

// Na cena: {"guid": "...", "path": "Assets/UI/Menu.mui"}, só o caminho, ou null. Ler já carrega o documento.
CustomFieldType FieldTraits<UiDocumentRef>::Describe() {
    return {"UiDocumentRef",
            [](const void* a, const void* b) {
                return *static_cast<const UiDocumentRef*>(a) == *static_cast<const UiDocumentRef*>(b);
            },
            [](const void* v, const AssetLibrary&) {
                const auto& ref = *static_cast<const UiDocumentRef*>(v);
                if (ref.IsEmpty()) return std::string("null");
                return Json{{"guid", ref.guid}, {"path", ref.path}}.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& ref = *static_cast<UiDocumentRef*>(v);
                if (json.is_null()) {
                    ref = {};
                    return true;
                }
                if (json.is_string()) {
                    SetUiDocumentFile(ref, json.get<std::string>(), assets);
                    return true;
                }
                if (!json.is_object() || (!json.contains("guid") && !json.contains("path"))) {
                    SetError(error, "esperado caminho \"Assets/....mui\", objeto {\"path\": \"Assets/...\"} ou null");
                    return false;
                }
                ref = {};
                ref.guid = json.value("guid", std::string());
                ref.path = json.value("path", std::string());
                ReloadUiDocument(ref, assets);
                return true;
            }};
}

UiDocumentData* UiDocumentInstance::Get(const UiDocumentRef& ref) {
    if (!ref.data) {
        m_tree.reset();
        m_from.reset();
        return nullptr;
    }
    if (m_from != ref.data || !m_tree) {
        m_tree = std::make_unique<UiDocumentData>(*ref.data);
        m_from = ref.data;
    }
    return m_tree.get();
}

MOTOR_COMPONENT_MODULE(UiDocumento) {
    registry.Register<UiDocument>("UiDocument", "UI: Documento")
        .Field("document", "Documento", &UiDocument::document)
        .Field("order", "Ordem", &UiDocument::order, {.tooltip = "Entre irmãos, maior desenha por cima"})
        .Field("visible", "Visível", &UiDocument::visible);
}

} // namespace motor
