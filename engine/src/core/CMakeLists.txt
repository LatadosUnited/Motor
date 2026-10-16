# Camada base: log, caminhos UTF-8, janela, entrada e a camada do ImGui. Não depende de outras camadas.
motor_add_layer(core
    SOURCES
        Log.cpp
        Window.cpp
        Input.cpp
        ImGuiLayer.cpp
    PRIVATE_LIBS glfw)

# ImGui: os objetos entram na libMotor.dll pelo alvo Motor; aqui só os headers (públicos: o editor e os scripts
# usam o ImGui pelos headers da engine).
target_link_libraries(motor_core PRIVATE imgui)
target_include_directories(motor_core PUBLIC ${MOTOR_IMGUI_INCLUDE_DIRS})
