#include "motor/ui/LoadingScreen.h"

#include "motor/ui/UiComponents.h"

namespace motor {

Scene DefaultLoadingScene(std::string_view text) {
    Scene scene;
    Entity& fundo = scene.CreateEntity("Fundo");
    UiRect& rect = fundo.Add<UiRect>();
    rect.anchor = UiAnchor::Stretch;
    rect.position = {0.0f, 0.0f};
    rect.size = {0.0f, 0.0f};  // Stretch: margens zero, a tela inteira
    fundo.Add<UiImage>().color = {0.04f, 0.045f, 0.05f, 1.0f};

    Entity& texto = scene.CreateEntity("Carregando", fundo.id);
    UiRect& textRect = texto.Add<UiRect>();
    textRect.anchor = UiAnchor::BottomRight;
    textRect.position = {-60.0f, -50.0f};
    textRect.size = {500.0f, 60.0f};
    UiText& t = texto.Add<UiText>();
    t.text = std::string(text);
    t.fontSize = 40.0f;
    t.color = {0.85f, 0.87f, 0.9f, 1.0f};
    t.align = UiTextAlign::Right;
    return scene;
}

} // namespace motor
