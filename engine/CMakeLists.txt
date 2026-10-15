# Biblioteca compartilhada (libMotor.dll): o editor e a DLL de scripts de cada projeto ligam nela, e assim
# compartilham o registro de componentes, o Input, o log e o ImGui.
add_library(Motor SHARED
    src/assets/AssetDatabase.cpp
    src/assets/AssetLibrary.cpp
    src/assets/AssetLoaders.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/core/Input.cpp
    src/core/Application.cpp
    src/core/ImGuiLayer.cpp
    src/renderer/GL.cpp
    src/renderer/Shader.cpp
    src/renderer/Mesh.cpp
    src/renderer/Texture.cpp
    src/renderer/Camera.cpp
    src/renderer/Renderer.cpp
    src/renderer/Framebuffer.cpp
    src/scene/Transform.cpp
    src/scene/FlyCameraController.cpp
    src/scene/Component.cpp
    src/scene/Scene.cpp
    src/scene/SceneRuntime.cpp
    src/scene/SceneSerializer.cpp
    src/project/Project.cpp
)

target_include_directories(Motor
    PUBLIC  include ${MOTOR_IMGUI_INCLUDE_DIRS}
    PRIVATE src ${stb_image_SOURCE_DIR})

# imgui é uma biblioteca de objetos: os objetos entram na DLL (os headers são publicados acima).
target_link_libraries(Motor
    PUBLIC  glm::glm
    PRIVATE imgui glfw nlohmann_json::nlohmann_json)

# Sem __declspec(dllexport) no código: exporta todos os símbolos (engine, ImGui e GLFW).
if(MINGW)
    target_link_options(Motor PRIVATE -Wl,--export-all-symbols)
endif()

target_compile_definitions(Motor
    PUBLIC  GLM_FORCE_RADIANS
    PRIVATE MOTOR_ENGINE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets"
            GLFW_INCLUDE_NONE
            NOMINMAX
            WIN32_LEAN_AND_MEAN)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Motor PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(Motor PRIVATE /W4)
endif()

# ---------------------------------------------------------------------------
# SDK para os scripts dos projetos: o editor gera um CMakeLists por projeto que inclui MotorSDK.cmake (alvo
# importado Motor::Motor) e lê MotorSDK.json para saber qual toolchain usar (o mesmo deste build).
# ---------------------------------------------------------------------------
set(MOTOR_SDK_DIR ${CMAKE_BINARY_DIR}/bin/sdk)
set(MOTOR_SDK_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include ${glm_SOURCE_DIR} ${MOTOR_IMGUI_INCLUDE_DIRS})
file(GENERATE OUTPUT ${MOTOR_SDK_DIR}/MotorSDK.cmake CONTENT
"# Gerado pelo build do Motor. Não edite.
if(NOT TARGET Motor::Motor)
    add_library(Motor::Motor SHARED IMPORTED)
    set_target_properties(Motor::Motor PROPERTIES
        IMPORTED_LOCATION \"$<TARGET_FILE:Motor>\"
        IMPORTED_IMPLIB \"$<TARGET_LINKER_FILE:Motor>\"
        INTERFACE_INCLUDE_DIRECTORIES \"${MOTOR_SDK_INCLUDE_DIRS}\"
        INTERFACE_COMPILE_DEFINITIONS \"GLM_FORCE_RADIANS\"
        INTERFACE_COMPILE_FEATURES \"cxx_std_20\")
endif()
")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/MotorSDK.json.in ${MOTOR_SDK_DIR}/MotorSDK.json @ONLY)
