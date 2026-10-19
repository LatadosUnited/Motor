# Camada base: log, caminhos UTF-8, janela, entrada e a camada do ImGui. Não depende de outras camadas.
motor_add_layer(core
    SOURCES
        Log.cpp
        EngineAssets.cpp
        Window.cpp
        Input.cpp
        ImGuiLayer.cpp
        FileWrite.cpp
    PRIVATE_LIBS glfw)

# ImGui: os objetos entram na libMotor.dll pelo alvo Motor; aqui só os headers (públicos: o editor e os scripts
# usam o ImGui pelos headers da engine).
target_link_libraries(motor_core PRIVATE imgui)
target_include_directories(motor_core PUBLIC ${MOTOR_IMGUI_INCLUDE_DIRS})

# Fase 14: número de versão (project(Motor VERSION ...) na raiz) num header gerado, motor/core/Version.h.
set(MOTOR_GENERATED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/generated/include)
configure_file(${MOTOR_ENGINE_DIR}/include/motor/core/Version.h.in
               ${MOTOR_GENERATED_INCLUDE_DIR}/motor/core/Version.h @ONLY)
target_include_directories(motor_core PUBLIC ${MOTOR_GENERATED_INCLUDE_DIR})
set(MOTOR_GENERATED_INCLUDE_DIR ${MOTOR_GENERATED_INCLUDE_DIR} PARENT_SCOPE)
