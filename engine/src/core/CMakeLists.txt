# Camada base: log, caminhos UTF-8, janela, entrada e a camada do ImGui. Não depende de outras camadas.
motor_add_layer(core
    SOURCES
        Log.cpp
        Window.cpp
        Input.cpp
        ImGuiLayer.cpp
    PRIVATE_LIBS glfw)

# O editor e os jogos usam o ImGui pelos headers da engine.
target_link_libraries(motor_core PUBLIC imgui)
