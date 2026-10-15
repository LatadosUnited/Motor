cmake_minimum_required(VERSION 3.24)
project(Motor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Algumas dependências declaram versões antigas do CMake; o CMake 4 exige >= 3.5.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ---------------------------------------------------------------------------
# Dependências externas (mínimas): GLFW para janela/input, GLM para matemática.
# O carregador de OpenGL é nosso (engine/src/renderer/GL.cpp).
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# Dear ImGui (branch docking) para o editor. Não tem CMakeLists: montamos a lib abaixo.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8-docking
    GIT_SHALLOW    TRUE)

# ImGuizmo (gizmos de transform do editor). As tags são antigas; fixamos um commit do master.
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        105e4fde45e4e74aae8973d98750820ef7aca7e2)

# nlohmann/json para serialização de cenas (tarball oficial do release, só o header).
FetchContent_Declare(json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)

# stb_image: decodificador de PNG/JPG/BMP/TGA (um único header, domínio público). Fixado num commit.
FetchContent_Declare(stb_image
    URL      https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
    URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
    DOWNLOAD_NO_EXTRACT TRUE)

FetchContent_MakeAvailable(glfw glm imgui imguizmo json stb_image)

# Objetos do ImGui (e o GLFW) entram na libMotor.dll: o editor e a DLL de scripts usam a mesma cópia (um
# único contexto do ImGui e uma única janela do GLFW no processo).
add_library(imgui OBJECT
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
set(MOTOR_IMGUI_INCLUDE_DIRS ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_include_directories(imgui PUBLIC ${MOTOR_IMGUI_INCLUDE_DIRS})
target_link_libraries(imgui PUBLIC glfw)

# O CMakeLists do ImGuizmo cria o alvo "imguizmo" mas só liga ao ImGui no modo standalone. Ligamos ao Motor,
# que traz os headers e os símbolos do ImGui (ligar ao alvo imgui duplicaria os objetos).
target_link_libraries(imguizmo PUBLIC Motor)

# Executáveis e DLLs na mesma pasta (build/bin): no Windows uma DLL é procurada ao lado do .exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# O runtime do C++ é dinâmico (libstdc++-6.dll...): o editor, a libMotor.dll e a DLL de scripts do jogo
# precisam compartilhar alocação, exceções e RTTI. Copiamos as DLLs do toolchain para build/bin para os
# executáveis rodarem fora do shell do MSYS2.
if(MINGW)
    get_filename_component(MOTOR_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${MOTOR_TOOLCHAIN_BIN}/${dll}")
            file(COPY "${MOTOR_TOOLCHAIN_BIN}/${dll}" DESTINATION "${CMAKE_BINARY_DIR}/bin")
        endif()
    endforeach()
endif()

add_subdirectory(engine)
add_subdirectory(sandbox)
add_subdirectory(editor)

option(MOTOR_BUILD_TESTS "Compila os testes (ctest)" ON)
if(MOTOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
