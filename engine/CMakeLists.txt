add_library(Motor STATIC
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
    src/scene/Scene.cpp
    src/scene/SceneSerializer.cpp
    src/project/Project.cpp
)

target_include_directories(Motor
    PUBLIC  include
    PRIVATE src ${stb_image_SOURCE_DIR})

target_link_libraries(Motor
    PUBLIC  glm::glm imgui
    PRIVATE glfw nlohmann_json::nlohmann_json)

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
