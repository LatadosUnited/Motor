#include "motor/ui/UiConvert.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/common/Text.h"
#include "motor/scene/Scene.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace motor {

namespace {

bool IsUiComponent(const ComponentPtr& c) {
    const std::string& name = c.Type().name;
    return name == "UiRect" || name == "UiText" || name == "UiImage" || name == "UiButton";
}

struct Converter {
    const Scene& scene;
    const AssetLibrary& assets;
    std::unordered_map<EntityId, std::vector<const Entity*>> children;  // na ordem da cena
    std::vector<EntityId>& converted;
    std::string error;

    // Irmãos na ordem de desenho do layout por entidades: UiRect::order, empate na ordem da cena.
    std::vector<const Entity*> Sorted(EntityId parent) const {
        const auto it = children.find(parent);
        if (it == children.end()) return {};
        std::vector<const Entity*> list = it->second;
        auto orderOf = [](const Entity* e) {
            const UiRect* r = e->Get<UiRect>();
            return r ? r->order : 0;
        };
        std::stable_sort(list.begin(), list.end(), [&](const Entity* a, const Entity* b) { return orderOf(a) < orderOf(b); });
        return list;
    }

    void Declare(UiElement& e, const char* property, const UiStyleValue& value) { e.SetStyleValue(property, value); }

    void Text(UiElement& e, const UiText& text) {
        e.text = text.text;
        Declare(e, "fontSize", text.fontSize);
        Declare(e, "textColor", text.color);
        Declare(e, "align", static_cast<int>(text.align));
        Declare(e, "verticalAlign", static_cast<int>(UiVerticalAlign::Middle));
        Declare(e, "wrap", false);
        if (!text.font.IsDefault()) Declare(e, "font", text.font);
    }

    UiFont ImageRef(const UiImage& image) const {
        UiFont ref;
        if (const auto r = assets.RefOf(image.texture.get())) {
            ref.guid = r->guid;
            ref.path = r->name;
        }
        return ref;
    }

    bool Convert(const Entity& entity, UiElement& out) {
        for (const ComponentPtr& c : entity.components) {
            if (!IsUiComponent(c)) {
                error = std::format("a entidade \"{}\" tem o componente {} além da UI (mova-o para outra entidade antes)",
                                    entity.name, c.Type().label);
                return false;
            }
        }
        const UiRect rect = entity.Get<UiRect>() ? *entity.Get<UiRect>() : UiRect{};
        const UiButton* button = entity.Get<UiButton>();
        const UiImage* image = entity.Get<UiImage>();
        const UiText* text = entity.Get<UiText>();
        UiElementType type = UiElementType::Panel;
        if (button) type = UiElementType::Button;
        else if (image) type = UiElementType::Image;
        else if (text) type = UiElementType::Text;

        UiElement e = UiElement::Make(type);
        e.name = entity.name;
        e.visible = rect.visible && entity.active;
        // Lugar: o mesmo retângulo do UiRect (com Stretch, position e size são as margens, como no documento).
        Declare(e, "placement", static_cast<int>(UiPlacement::Absolute));
        Declare(e, "anchor", static_cast<int>(rect.anchor));
        Declare(e, "position", rect.position);
        Declare(e, "width", UiLength::Px(rect.size.x));
        Declare(e, "height", UiLength::Px(rect.size.y));
        Declare(e, "padding", UiEdges{});  // o texto do UiText ocupa o retângulo inteiro
        switch (type) {
            case UiElementType::Button:
                e.interactable = button->interactable;
                Declare(e, "normalColor", button->normalColor);
                Declare(e, "hoverColor", button->hoverColor);
                Declare(e, "pressedColor", button->pressedColor);
                if (image) {
                    Declare(e, "color", image->color);
                    e.image = ImageRef(*image);
                }
                if (text) Text(e, *text);
                else e.text.clear();
                break;
            case UiElementType::Image:
                Declare(e, "color", image->color);
                e.image = ImageRef(*image);
                if (text) {  // a imagem não desenha texto: um Text filho esticado por cima
                    UiElement label = UiElement::Make(UiElementType::Text);
                    label.name = entity.name + "_texto";
                    Declare(label, "placement", static_cast<int>(UiPlacement::Absolute));
                    Declare(label, "anchor", static_cast<int>(UiAnchor::Stretch));
                    Declare(label, "width", UiLength::Px(0.0f));
                    Declare(label, "height", UiLength::Px(0.0f));
                    Text(label, *text);
                    e.children.push_back(std::move(label));
                }
                break;
            case UiElementType::Text: Text(e, *text); break;
            default: Declare(e, "color", glm::vec4(0.0f)); break;
        }
        converted.push_back(entity.id);
        for (const Entity* child : Sorted(entity.id)) {
            if (!IsUiEntity(*child) || child->Has<UiDocument>()) {
                error = std::format("\"{}\" (filha de \"{}\") não é de UI por entidades: tire-a de dentro antes", child->name,
                                    entity.name);
                return false;
            }
            UiElement childElement;
            if (!Convert(*child, childElement)) return false;
            e.children.push_back(std::move(childElement));
        }
        out = std::move(e);
        return true;
    }
};

} // namespace

bool ConvertUiEntities(const Scene& scene, EntityId root, const AssetLibrary& assets, UiConversion& out, std::string* error) {
    out = {};
    const Entity* r = scene.Find(root);
    if (!r) {
        SetError(error, "a entidade não existe");
        return false;
    }
    if (r->Has<UiDocument>()) {
        SetError(error, std::format("\"{}\" já é um documento de UI", r->name));
        return false;
    }
    Converter c{scene, assets, {}, out.converted, {}};
    for (const auto& e : scene.Entities()) c.children[e->parent].push_back(e.get());
    out.rootIsUi = IsUiEntity(*r);
    if (out.rootIsUi) {
        UiElement element;
        if (!c.Convert(*r, element)) {
            SetError(error, c.error);
            out = {};
            return false;
        }
        out.document.elements.push_back(std::move(element));
    } else {
        // Entidade comum (com scripts, talvez): os filhos de UI viram o documento; os outros filhos ficam.
        for (const Entity* child : c.Sorted(root)) {
            if (!IsUiEntity(*child)) continue;
            if (child->Has<UiDocument>()) {
                SetError(error, std::format("\"{}\" já é um documento de UI", child->name));
                out = {};
                return false;
            }
            UiElement element;
            if (!c.Convert(*child, element)) {
                SetError(error, c.error);
                out = {};
                return false;
            }
            out.document.elements.push_back(std::move(element));
        }
        if (out.document.elements.empty()) {
            SetError(error, std::format("\"{}\" não tem UI por entidades (UiRect, UiText, UiImage, UiButton) para converter",
                                        r->name));
            return false;
        }
    }
    out.document.styled = true;  // as declarações estão nos elementos (SetStyleValue)
    ComputeUiStyles(out.document);
    return true;
}

} // namespace motor
