#include "motor/ui/UiDocument.h"

#include "UiStyleJson.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/common/FileRead.h"
#include "motor/common/FileWrite.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/ui/Font.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace motor {

namespace {
using Json = UiJson;

int Prop(const char* name) { return FindUiStyleProperty(name); }

// Imagem ou folha: {"guid": "...", "path": "Assets/..."} ou só o caminho (como escreve um agente pelo MCP).
Json FileRef(const UiFont& ref) { return Json{{"guid", ref.guid}, {"path", ref.path}}; }

bool ReadFileRef(const Json& json, UiFont& out) {
    out = {};
    if (json.is_string()) {
        out.path = json.get<std::string>();
        return true;
    }
    if (json.is_object()) {
        out.guid = json.value("guid", std::string());
        out.path = json.value("path", std::string());
        return true;
    }
    return false;
}

// As declarações que o arquivo guarda: as do elemento ou, num documento montado em código (não `styled`), os campos
// diferentes do padrão do tipo (como o gravador fazia antes das folhas).
UiStyleDeclarations DeclaredStyle(const UiElement& e, bool styled) {
    if (styled) return e.inlineStyle;
    const UiElement def = UiElement::Make(e.type);
    UiStyleDeclarations out;
    const int anchor = Prop("anchor"), position = Prop("position");
    for (int p = 0; p < UiStylePropertyCount(); ++p) {
        const UiStyleValue value = GetUiStyleValue(e, p);
        const bool absoluteSpot = e.placement == UiPlacement::Absolute && (p == anchor || p == position);
        if (absoluteSpot || !(value == GetUiStyleValue(def, p))) out.Set(p, value);
    }
    return out;
}

Json ElementToJson(const UiElement& e, bool styled) {
    const UiElement def = UiElement::Make(e.type);
    Json json{{"type", UiElementTypeName(e.type)}};
    if (!e.name.empty()) json["name"] = e.name;
    if (!e.classes.empty()) json["classes"] = e.classes;
    if (e.visible != def.visible) json["visible"] = e.visible;
    if (e.interactable != def.interactable) json["interactable"] = e.interactable;
    if (!e.image.IsDefault()) json["image"] = FileRef(e.image);
    if (e.text != def.text) json["text"] = e.text;
    // Controles e follow (versão 4).
    if (e.value != def.value) json["value"] = e.value;
    if (e.minValue != def.minValue) json["min"] = e.minValue;
    if (e.maxValue != def.maxValue) json["max"] = e.maxValue;
    if (e.step != def.step) json["step"] = e.step;
    if (e.checked != def.checked) json["checked"] = e.checked;
    if (e.placeholder != def.placeholder) json["placeholder"] = e.placeholder;
    if (e.maxLength != def.maxLength) json["maxLength"] = e.maxLength;
    if (!e.follow.empty()) json["follow"] = e.follow;
    if (e.followOffset != def.followOffset) json["followOffset"] = {e.followOffset.x, e.followOffset.y, e.followOffset.z};
    WriteUiStyleDeclarations(DeclaredStyle(e, styled), json);
    if (!e.children.empty()) {
        Json children = Json::array();
        for (const UiElement& child : e.children) children.push_back(ElementToJson(child, styled));
        json["children"] = std::move(children);
    }
    return json;
}

void ElementsFromJson(const Json& list, int version, std::vector<UiElement>& out, std::vector<std::string>* warnings,
                      const std::string& where, const UiStyleDeclarations* parentStyle);

UiElement ElementFromJson(const Json& json, int version, std::vector<std::string>* warnings, const std::string& where,
                          const UiStyleDeclarations* parentStyle) {
    UiElementType type = UiElementType::Panel;
    const std::string typeName = json.value("type", std::string("Panel"));
    const bool known = ParseUiElementType(typeName, type);
    if (!known && warnings) warnings->push_back(std::format("{}: tipo \"{}\" desconhecido, lido como Panel", where, typeName));

    UiElement e = UiElement::Make(type);
    e.name = json.value("name", std::string());
    if (const auto it = json.find("classes"); it != json.end() && it->is_array()) {
        for (const Json& c : *it) {
            if (c.is_string()) e.classes.push_back(c.get<std::string>());
        }
    }
    e.visible = json.value("visible", e.visible);
    if (const auto it = json.find("image"); it != json.end()) ReadFileRef(*it, e.image);
    if (const auto it = json.find("text"); it != json.end() && it->is_string()) e.text = it->get<std::string>();
    if (const auto it = json.find("interactable"); it != json.end() && it->is_boolean()) e.interactable = it->get<bool>();
    auto number = [&](const char* key, float& field) {
        if (const auto it = json.find(key); it != json.end() && it->is_number()) field = it->get<float>();
    };
    number("value", e.value);
    number("min", e.minValue);
    number("max", e.maxValue);
    number("step", e.step);
    if (const auto it = json.find("checked"); it != json.end() && it->is_boolean()) e.checked = it->get<bool>();
    if (const auto it = json.find("placeholder"); it != json.end() && it->is_string()) e.placeholder = it->get<std::string>();
    if (const auto it = json.find("maxLength"); it != json.end() && it->is_number_integer()) e.maxLength = it->get<int>();
    if (const auto it = json.find("follow"); it != json.end() && it->is_string()) e.follow = it->get<std::string>();
    if (const auto it = json.find("followOffset");
        it != json.end() && it->is_array() && it->size() == 3 && (*it)[0].is_number() && (*it)[1].is_number() &&
        (*it)[2].is_number()) {
        e.followOffset = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    }

    if (version < 2) {
        // Antes do flex: tudo por âncora, 200x60 sem "size", texto sem quebra nem preenchimento.
        e.inlineStyle.Set(Prop("placement"), static_cast<int>(UiPlacement::Absolute));
        e.inlineStyle.Set(Prop("width"), UiLength::Px(200.0f));
        e.inlineStyle.Set(Prop("height"), UiLength::Px(60.0f));
        e.inlineStyle.Set(Prop("wrap"), false);
        e.inlineStyle.Set(Prop("padding"), UiEdges{});
    }
    ReadUiStyleDeclarations(json, e.inlineStyle);  // valor da forma errada fica com o padrão, como antes
    if (version < 3 && parentStyle) {
        // Antes das folhas não havia herança: o que o pai escreveu de texto não descia para os filhos.
        const UiElement def = UiElement::Make(type);
        for (const UiStyleDeclaration& d : parentStyle->items) {
            if (UiStylePropertyInherited(d.property) && !e.inlineStyle.Find(d.property)) {
                e.inlineStyle.Set(d.property, GetUiStyleValue(def, d.property));
            }
        }
    }
    if (const auto it = json.find("children"); it != json.end()) {
        ElementsFromJson(*it, version, e.children, warnings, where + "/" + (e.name.empty() ? typeName : e.name),
                         &e.inlineStyle);
    }
    return e;
}

void ElementsFromJson(const Json& list, int version, std::vector<UiElement>& out, std::vector<std::string>* warnings,
                      const std::string& where, const UiStyleDeclarations* parentStyle) {
    if (!list.is_array()) return;
    for (const Json& item : list) {
        if (item.is_object()) {
            out.push_back(ElementFromJson(item, version, warnings, where, parentStyle));
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

bool SameContentOnly(const UiElement& a, const UiElement& b) {
    return a.type == b.type && a.name == b.name && a.classes == b.classes && a.visible == b.visible &&
           a.image == b.image && a.text == b.text && a.interactable == b.interactable && a.value == b.value &&
           a.minValue == b.minValue && a.maxValue == b.maxValue && a.step == b.step && a.checked == b.checked &&
           a.placeholder == b.placeholder && a.maxLength == b.maxLength && a.follow == b.follow &&
           a.followOffset == b.followOffset;
}

// Entre um documento lido e um montado em código: as declarações de cada lado, do jeito que iriam para o arquivo.
bool SameDeclaredElements(const std::vector<UiElement>& a, bool aStyled, const std::vector<UiElement>& b, bool bStyled) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!SameContentOnly(a[i], b[i]) || !(DeclaredStyle(a[i], aStyled) == DeclaredStyle(b[i], bStyled)) ||
            !SameDeclaredElements(a[i].children, aStyled, b[i].children, bStyled)) {
            return false;
        }
    }
    return true;
}

void CollectQuery(std::vector<UiElement>& list, const std::vector<UiSelector>& selectors, std::vector<UiElement*>& out,
                  bool first) {
    for (UiElement& e : list) {
        if (first && !out.empty()) return;
        if (UiQueryMatches(selectors, e)) out.push_back(&e);
        CollectQuery(e.children, selectors, out, first);
    }
}

std::vector<UiElement*> RunQuery(std::vector<UiElement>& list, std::string_view selector, bool first) {
    std::vector<UiSelector> selectors;
    std::vector<UiElement*> out;
    if (ParseUiQuery(selector, selectors)) CollectQuery(list, selectors, out, first);
    return out;
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

std::filesystem::path FileOf(const UiFont& ref, AssetLibrary& assets) {
    std::filesystem::path file;
    if (!ref.guid.empty()) file = assets.Database().PathFor(ref.guid);
    if (file.empty() && !ref.path.empty()) file = assets.FileForKey(ref.path);
    return file;
}

std::shared_ptr<const FontData> LoadFontRef(const UiFont& ref, AssetLibrary& assets, std::string_view who) {
    const std::filesystem::path file = FileOf(ref, assets);
    std::string error;
    std::shared_ptr<const FontData> font = file.empty() ? nullptr : LoadUiFont(file, &error);
    if (!font) {
        log::Warn("UI: fonte {} de {}: {}; usando a padrão", ref.path.empty() ? ref.guid : ref.path, who,
                  error.empty() ? std::string("não encontrada") : error);
    }
    return font;
}

struct AssetResolver {
    UiDocumentData& doc;
    AssetLibrary& assets;

    // Fonte de uma declaração (do elemento ou de uma folha): carregada uma vez, achada pelo GUID e pelo caminho.
    void AddFont(const UiFont& ref, std::string_view who) {
        if (ref.IsDefault()) return;
        if ((!ref.guid.empty() && doc.fonts.contains("g:" + ref.guid)) ||
            (ref.guid.empty() && doc.fonts.contains("p:" + ref.path))) {
            return;
        }
        auto font = LoadFontRef(ref, assets, who);
        if (!ref.guid.empty()) doc.fonts["g:" + ref.guid] = font;
        if (!ref.path.empty()) doc.fonts["p:" + ref.path] = font;
    }

    void Elements(std::vector<UiElement>& list) {
        const int fontProp = Prop("font");
        for (UiElement& e : list) {
            ResolveRef(e.image, assets);
            if (doc.styled) {
                for (UiStyleDeclaration& d : e.inlineStyle.items) {
                    if (d.property != fontProp) continue;
                    if (UiFont* ref = std::get_if<UiFont>(&d.value)) {
                        ResolveRef(*ref, assets);
                        AddFont(*ref, "elemento " + e.name);
                    }
                }
            } else {
                // Montado em código: os campos valem (a fonte é a do campo).
                ResolveRef(e.font, assets);
                e.fontData = e.font.IsDefault() ? nullptr : LoadFontRef(e.font, assets, "elemento " + e.name);
            }
            e.texture = nullptr;
            if (!e.image.IsDefault()) {
                std::string error;
                if (!e.image.guid.empty()) e.texture = assets.GetTextureByGuid(e.image.guid, &error);
                if (!e.texture && !e.image.path.empty()) e.texture = assets.GetTexture(e.image.path, &error);
                if (!e.texture) log::Warn("UI: imagem {} do elemento {}: {}", e.image.path, e.name, error);
            }
            Elements(e.children);
        }
    }

    void Sheets() {
        doc.sheets.clear();
        for (UiFont& ref : doc.styles) {
            ResolveRef(ref, assets);
            const std::filesystem::path file = FileOf(ref, assets);
            auto sheet = std::make_shared<UiStyleSheet>();
            std::string error;
            if (file.empty() || !LoadUiStyleSheet(file, *sheet, &error)) {
                log::Warn("UI: folha de estilo {}: {}", ref.path.empty() ? ref.guid : ref.path,
                          error.empty() ? std::string("arquivo não encontrado") : error);
                doc.sheets.push_back(nullptr);
                continue;
            }
            const int fontProp = Prop("font");
            for (const UiStyleRule& rule : sheet->rules) {
                if (const UiStyleValue* v = rule.declarations.Find(fontProp)) {
                    if (const UiFont* font = std::get_if<UiFont>(v)) AddFont(*font, "regra " + rule.selector);
                }
            }
            doc.sheets.push_back(std::move(sheet));
        }
    }
};
} // namespace

const char* UiElementTypeName(UiElementType type) {
    switch (type) {
        case UiElementType::Panel: return "Panel";
        case UiElementType::Text: return "Text";
        case UiElementType::Image: return "Image";
        case UiElementType::Button: return "Button";
        case UiElementType::ProgressBar: return "ProgressBar";
        case UiElementType::Slider: return "Slider";
        case UiElementType::Toggle: return "Toggle";
        case UiElementType::TextField: return "TextField";
    }
    return "Panel";
}

bool ParseUiElementType(std::string_view name, UiElementType& out) {
    for (UiElementType t : kUiElementTypes) {
        if (name == UiElementTypeName(t)) {
            out = t;
            return true;
        }
    }
    return false;
}

bool ParseUiQuery(std::string_view text, std::vector<UiSelector>& out, std::string* error) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    std::string parseError;
    if (ParseUiSelectors(text, out, &parseError)) return true;
    // Só um nome ("jogar"): o elemento com esse nome (como FindElement), mesmo sem o #.
    if (!text.empty() && text.find_first_of(".#:, >+~*") == std::string_view::npos) {
        UiSelector byName;
        byName.name = std::string(text);
        out = {byName};
        return true;
    }
    SetError(error, parseError);
    return false;
}

bool UiQueryMatches(const std::vector<UiSelector>& selectors, const UiElement& element) {
    const uint8_t states = UiElementStates(element);
    for (const UiSelector& s : selectors) {
        if (s.Matches(element, states)) return true;
    }
    return false;
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
        case UiElementType::ProgressBar:
            e.height = UiLength::Px(24.0f);
            e.padding = UiEdges::All(3.0f);
            e.color = {0.08f, 0.09f, 0.12f, 0.9f};
            break;
        case UiElementType::Slider:
            e.height = UiLength::Px(36.0f);
            e.color = {0.08f, 0.09f, 0.12f, 0.9f};
            // O puxador (cor do estado): claro, para aparecer sobre o trilho escuro e o preenchimento.
            e.normalColor = {0.85f, 0.88f, 0.94f, 1.0f};
            e.hoverColor = {1.0f, 1.0f, 1.0f, 1.0f};
            e.pressedColor = {0.68f, 0.73f, 0.84f, 1.0f};
            break;
        case UiElementType::Toggle:
            e.text = "Opção";
            e.color = glm::vec4(0.0f);
            e.align = UiTextAlign::Left;
            break;
        case UiElementType::TextField:
            e.padding = UiEdges::Symmetric(10.0f, 14.0f);
            e.align = UiTextAlign::Left;
            break;
    }
    return e;
}

bool UiElement::HasBackground() const {
    switch (type) {
        case UiElementType::Text:
        case UiElementType::Slider: return false;  // o trilho e o puxador são desenhados à parte (UiRenderer)
        case UiElementType::Button:
        case UiElementType::TextField:
        case UiElementType::ProgressBar: return true;
        case UiElementType::Panel:
        case UiElementType::Image:
        case UiElementType::Toggle:
            return color.a > 0.0f || texture != nullptr || (borderWidth > 0.0f && borderColor.a > 0.0f);
    }
    return false;
}

bool UiElement::BlocksPointer() const {
    return type == UiElementType::Image || type == UiElementType::ProgressBar || IsControl() ||
           (type == UiElementType::Panel && HasBackground());
}

glm::vec4 UiElement::StateColor() const {
    const glm::vec4* state = &normalColor;
    if (interactable && pressed) state = &pressedColor;
    else if (interactable && hovered) state = &hoverColor;
    return *state;
}

glm::vec4 UiElement::BackgroundColor() const {
    if (type != UiElementType::Button && type != UiElementType::TextField) return color;
    return StateColor() * color;
}

std::string_view UiElement::DisplayText() const {
    switch (type) {
        case UiElementType::Text:
        case UiElementType::Button:
        case UiElementType::Toggle: return text;
        case UiElementType::TextField: return text.empty() ? std::string_view(placeholder) : std::string_view(text);
        default: return {};
    }
}

bool UiElement::IsControl() const {
    return type == UiElementType::Button || type == UiElementType::Slider || type == UiElementType::Toggle ||
           type == UiElementType::TextField;
}

float UiElement::Fraction() const {
    const float lo = std::min(minValue, maxValue), hi = std::max(minValue, maxValue);
    if (hi - lo <= 0.0f) return 0.0f;
    return std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
}

bool UiElement::SetValue(float v) {
    const float lo = std::min(minValue, maxValue), hi = std::max(minValue, maxValue);
    if (step > 0.0f) v = lo + std::round((v - lo) / step) * step;
    v = std::clamp(v, lo, hi);
    if (v == value) return false;
    value = v;
    return true;
}

UiElement* UiElement::Q(std::string_view selector) {
    const auto found = RunQuery(children, selector, true);
    return found.empty() ? nullptr : found.front();
}
std::vector<UiElement*> UiElement::Query(std::string_view selector) { return RunQuery(children, selector, false); }

const FontData* UiElement::Font() const { return fontData ? fontData.get() : DefaultUiFont(); }

UiVisual UiElement::Visual() const { return {BackgroundColor(), textColor, borderColor, opacity, scale}; }

bool UiElement::SameContent(const UiElement& o) const {
    return SameContentOnly(*this, o) && inlineStyle == o.inlineStyle && SameElements(children, o.children);
}

bool UiDocumentData::SameContent(const UiDocumentData& other) const {
    return styles == other.styles && SameDeclaredElements(elements, styled, other.elements, other.styled);
}
UiElement* UiDocumentData::Find(std::string_view name) { return name.empty() ? nullptr : FindIn(elements, name); }
const UiElement* UiDocumentData::Find(std::string_view name) const { return name.empty() ? nullptr : FindIn(elements, name); }
UiElement* UiDocumentData::Q(std::string_view selector) {
    const auto found = RunQuery(elements, selector, true);
    return found.empty() ? nullptr : found.front();
}
std::vector<UiElement*> UiDocumentData::Query(std::string_view selector) { return RunQuery(elements, selector, false); }

std::string UiDocumentToJson(const UiDocumentData& document) {
    Json json{{"format", kUiDocumentFormat}, {"version", kUiDocumentVersion}};
    if (!document.styles.empty()) {
        Json styles = Json::array();
        for (const UiFont& ref : document.styles) styles.push_back(FileRef(ref));
        json["styles"] = std::move(styles);
    }
    Json elements = Json::array();
    for (const UiElement& e : document.elements) elements.push_back(ElementToJson(e, document.styled));
    json["elements"] = std::move(elements);
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
    if (const auto it = json.find("styles"); it != json.end() && it->is_array()) {
        for (const Json& item : *it) {
            UiFont ref;
            if (ReadFileRef(item, ref) && !ref.IsDefault()) document.styles.push_back(ref);
            else if (warnings) warnings->push_back("styles: item que não é caminho nem {\"guid\", \"path\"} ignorado");
        }
    }
    if (const auto it = json.find("elements"); it != json.end()) {
        ElementsFromJson(*it, version, document.elements, warnings, "", nullptr);
    }
    ComputeUiStyles(document);  // sem as folhas ainda (ResolveUiDocumentAssets lê e calcula de novo)
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
    AssetResolver resolver{document, assets};
    document.fonts.clear();
    resolver.Sheets();
    resolver.Elements(document.elements);
    if (document.styled) ComputeUiStyles(document);
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
