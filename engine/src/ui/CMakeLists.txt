# Interface do jogo (Fase 9): componentes de UI, layout com âncoras, fontes SDF, desenho por cima da cena
# (UiRenderer, com o Renderer2D) e o sistema do SceneRuntime que detecta os cliques (UiSystem).
motor_add_layer(ui
    SOURCES
        UiComponents.cpp
        UiLayout.cpp
        Font.cpp
        UiRenderer.cpp
        UiSystem.cpp
    DEPENDS scene
    PRIVATE_LIBS nlohmann_json::nlohmann_json)

# stb_truetype: a cópia que vem com o ImGui (imstb_truetype.h), compilada só no Font.cpp (STBTT_STATIC).
target_include_directories(motor_ui SYSTEM PRIVATE ${imgui_SOURCE_DIR})
