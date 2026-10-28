#include "motor/ui/UiComponents.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/scene/Scene.h"
#include "motor/ui/UiDocument.h"

#include <nlohmann/json.hpp>

#include <iterator>
#include <string>

namespace motor {

namespace {
constexpr UiAnchor kAnchors[] = {UiAnchor::TopLeft,    UiAnchor::Top,    UiAnchor::TopRight,
                                 UiAnchor::Left,       UiAnchor::Center, UiAnchor::Right,
                                 UiAnchor::BottomLeft, UiAnchor::Bottom, UiAnchor::BottomRight,
                                 UiAnchor::Stretch};
constexpr UiTextAlign kAligns[] = {UiTextAlign::Left, UiTextAlign::Center, UiTextAlign::Right};

// Enum gravado como texto; números (o índice) também são aceitos. Outro valor: fica como está.
template <class E, size_t N, class NameFn>
CustomFieldType DescribeEnum(const char* typeName, const E (&values)[N], NameFn name) {
    return {typeName,
            [](const void* a, const void* b) { return *static_cast<const E*>(a) == *static_cast<const E*>(b); },
            [name](const void* v, const AssetLibrary&) { return nlohmann::json(name(*static_cast<const E*>(v))).dump(); },
            [&values, name](std::string_view text, void* v, AssetLibrary&, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& value = *static_cast<E*>(v);
                if (json.is_string()) {
                    for (E e : values) {
                        if (json.get<std::string>() == name(e)) {
                            value = e;
                            return true;
                        }
                    }
                } else if (json.is_number_integer()) {
                    const int n = json.get<int>();
                    if (n >= 0 && n < static_cast<int>(N)) {
                        value = values[n];
                        return true;
                    }
                }
                if (error) {
                    std::string list;
                    for (E e : values) list += (list.empty() ? "\"" : ", \"") + std::string(name(e)) + "\"";
                    *error = "esperado um de: " + list;
                }
                return false;
            }};
}
} // namespace

const char* UiAnchorName(UiAnchor anchor) {
    switch (anchor) {
        case UiAnchor::TopLeft: return "TopLeft";
        case UiAnchor::Top: return "Top";
        case UiAnchor::TopRight: return "TopRight";
        case UiAnchor::Left: return "Left";
        case UiAnchor::Center: return "Center";
        case UiAnchor::Right: return "Right";
        case UiAnchor::BottomLeft: return "BottomLeft";
        case UiAnchor::Bottom: return "Bottom";
        case UiAnchor::BottomRight: return "BottomRight";
        case UiAnchor::Stretch: return "Stretch";
    }
    return "Center";
}

const char* UiAnchorLabel(UiAnchor anchor) {
    switch (anchor) {
        case UiAnchor::TopLeft: return "Em cima, à esquerda";
        case UiAnchor::Top: return "Em cima, no centro";
        case UiAnchor::TopRight: return "Em cima, à direita";
        case UiAnchor::Left: return "No meio, à esquerda";
        case UiAnchor::Center: return "Centro";
        case UiAnchor::Right: return "No meio, à direita";
        case UiAnchor::BottomLeft: return "Embaixo, à esquerda";
        case UiAnchor::Bottom: return "Embaixo, no centro";
        case UiAnchor::BottomRight: return "Embaixo, à direita";
        case UiAnchor::Stretch: return "Esticar (preenche o pai)";
    }
    return "?";
}

const char* UiTextAlignName(UiTextAlign align) {
    switch (align) {
        case UiTextAlign::Left: return "Left";
        case UiTextAlign::Center: return "Center";
        case UiTextAlign::Right: return "Right";
    }
    return "Center";
}

const char* UiTextAlignLabel(UiTextAlign align) {
    switch (align) {
        case UiTextAlign::Left: return "Esquerda";
        case UiTextAlign::Center: return "Centro";
        case UiTextAlign::Right: return "Direita";
    }
    return "?";
}

CustomFieldType FieldTraits<UiAnchor>::Describe() {
    return DescribeEnum("UiAnchor", kAnchors, UiAnchorName);
}

CustomFieldType FieldTraits<UiTextAlign>::Describe() {
    return DescribeEnum("UiTextAlign", kAligns, UiTextAlignName);
}

// Na cena: {"guid": "...", "path": "Assets/Fontes/x.ttf"}, ou null para a fonte padrão. Ao ler, o GUID
// manda: o caminho é atualizado se o arquivo mudou de lugar (com o .meta).
CustomFieldType FieldTraits<UiFont>::Describe() {
    return {"UiFont",
            [](const void* a, const void* b) { return *static_cast<const UiFont*>(a) == *static_cast<const UiFont*>(b); },
            [](const void* v, const AssetLibrary&) {
                const auto& font = *static_cast<const UiFont*>(v);
                if (font.IsDefault()) return std::string("null");
                return nlohmann::ordered_json{{"guid", font.guid}, {"path", font.path}}.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& font = *static_cast<UiFont*>(v);
                if (json.is_null()) {
                    font = {};
                    return true;
                }
                if (json.is_string()) {
                    // Só o caminho ("Assets/Fontes/x.ttf"), como escreve um agente pelo MCP: o GUID vem do .meta.
                    font = {};
                    font.path = json.get<std::string>();
                    const std::filesystem::path file = assets.FileForKey(font.path);
                    std::error_code ec;
                    if (!file.empty() && std::filesystem::is_regular_file(file, ec)) font.guid = assets.Database().GuidFor(file);
                    return true;
                }
                if (!json.is_object() || (!json.contains("guid") && !json.contains("path"))) {
                    if (error) *error = "esperado caminho \"Assets/...\", objeto {\"path\": \"Assets/...\"} ou null";
                    return false;
                }
                font.guid = json.value("guid", std::string());
                font.path = json.value("path", std::string());
                if (!font.guid.empty()) {
                    const std::filesystem::path file = assets.Database().PathFor(font.guid);
                    if (const std::string key = file.empty() ? std::string() : assets.KeyForFile(file); !key.empty()) {
                        font.path = key;
                    }
                }
                return true;
            }};
}

const glm::vec4& UiButton::CurrentColor() const {
    if (!interactable) return normalColor;
    if (pressed) return pressedColor;
    return hovered ? hoverColor : normalColor;
}

bool IsUiEntity(const Entity& entity) {
    return entity.Has<UiRect>() || entity.Has<UiImage>() || entity.Has<UiText>() || entity.Has<UiButton>() ||
           entity.Has<UiDocument>();
}

// Nomes com o prefixo "Ui" seguidos do rótulo em português: no menu "Adicionar componente" ficam juntos.
MOTOR_COMPONENT_MODULE(Ui) {
    registry.Register<UiRect>("UiRect", "UI: Retângulo")
        .Field("anchor", "Âncora", &UiRect::anchor)
        .Field("position", "Posição", &UiRect::position,
               {.speed = 1.0f, .format = "%.0f", .tooltip = "Pixels a partir da âncora (x para a direita, y para baixo)"})
        .Field("size", "Tamanho", &UiRect::size,
               {.speed = 1.0f, .format = "%.0f", .tooltip = "Pixels de uma tela de 1080 de altura (Esticar: margens direita e de baixo)"})
        .Field("order", "Ordem", &UiRect::order, {.tooltip = "Entre irmãos, maior desenha por cima"})
        .Field("visible", "Visível", &UiRect::visible);
    registry.Register<UiImage>("UiImage", "UI: Imagem")
        .Field("texture", "Textura", &UiImage::texture)
        .Field("color", "Cor", &UiImage::color, {.color = true});
    registry.Register<UiText>("UiText", "UI: Texto")
        .Field("text", "Texto", &UiText::text)
        .Field("font", "Fonte", &UiText::font)
        .Field("fontSize", "Tamanho da fonte", &UiText::fontSize, {.speed = 0.5f, .min = 1.0f, .max = 400.0f, .format = "%.0f px"})
        .Field("color", "Cor", &UiText::color, {.color = true})
        .Field("align", "Alinhamento", &UiText::align);
    registry.Register<UiButton>("UiButton", "UI: Botão")
        .Field("normalColor", "Cor", &UiButton::normalColor, {.color = true})
        .Field("hoverColor", "Cor com o mouse em cima", &UiButton::hoverColor, {.color = true})
        .Field("pressedColor", "Cor pressionado", &UiButton::pressedColor, {.color = true})
        .Field("interactable", "Clicável", &UiButton::interactable);
}

} // namespace motor
