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

# Jolt Physics (MIT) para a física (Fase 5): multithread (job system próprio) e SIMD. Tarball da tag v5.6.0
# (commit e77f175595e64cb44218cc9d9d56fc365ad0e36a). O CMakeLists dele fica em Build/.
#
# SIMD: base SSE4.2 (+POPCNT), sem AVX/AVX2/FMA/F16C/LZCNT/TZCNT. O jogo exportado roda em qualquer x86-64
# dos últimos ~15 anos (SSE4.2 existe desde Nehalem/2008 e Bulldozer/2011); com AVX2 ligado (padrão do Jolt) o
# executável fecha com "instrução ilegal" em CPUs sem AVX2, sem aviso, e não há troca em tempo de execução.
# O ganho de AVX2 no passo da física é pequeno perto disso.
set(USE_SSE4_1 ON  CACHE BOOL "" FORCE)
set(USE_SSE4_2 ON  CACHE BOOL "" FORCE)
set(USE_AVX    OFF CACHE BOOL "" FORCE)
set(USE_AVX2   OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT  OFF CACHE BOOL "" FORCE)
set(USE_TZCNT  OFF CACHE BOOL "" FORCE)
set(USE_F16C   OFF CACHE BOOL "" FORCE)
set(USE_FMADD  OFF CACHE BOOL "" FORCE)
# RTTI ligado: herdamos de classes do Jolt (ContactListener, filtros de camada) num código com RTTI, e as
# vtables precisam do typeinfo das classes base.
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
# Só a biblioteca: sem asserts, -Werror, LTO, instalação, profiler, debug renderer (o editor desenha os
# colliders), ObjectStream, nem a parte de GPU (DX12/Vulkan/Metal/CPU compute, usada só pelo cabelo).
set(USE_ASSERTS                         OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS                 OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION        OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS              OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED   OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL                      OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE       OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM                OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12                        OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK                          OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL                         OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE                 OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_SHARED_LIBS               OFF CACHE BOOL "" FORCE)  # estática, entra na libMotor.dll

FetchContent_Declare(jolt
    URL         https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH    SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(jolt)

# Os objetos do Jolt vão para uma DLL. No Debug ele é otimizado mesmo assim: sem otimização o passo da física
# fica dezenas de vezes mais lento e o Play no editor deixa de ser utilizável (não depuramos o Jolt por dentro).
set_target_properties(Jolt PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Jolt PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()

# cgltf (MIT): leitor de glTF 2.0 (.gltf/.glb) num único header C99, sem dependências (Fase 11). Fixado na tag
# v1.15; a implementação é compilada só no GltfLoader.cpp da camada de assets.
FetchContent_Declare(cgltf
    URL      https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/cgltf.h
    URL_HASH SHA256=e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(cgltf)

# cpp-httplib (MIT): servidor HTTP num único header, para o servidor MCP do editor (Fase 13; só o editor usa,
# escutando em 127.0.0.1). Fixado na tag v0.58.0, sem SSL.
FetchContent_Declare(httplib
    URL      https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.58.0/httplib.h
    URL_HASH SHA256=aa14e7e7bd2703694e0a6b6855af3b8c406102ab1fc56ac905fe33619b31faa5
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(httplib)

# miniaudio (domínio público/MIT-0): áudio num único header (WAV/MP3/FLAC embutidos; OGG com o stb_vorbis que
# vem em extras/), dispositivo, mixagem e som 3D (Fase 8). Fixado na tag 0.11.22; a implementação é compilada
# só no AudioEngine.cpp da camada de áudio. SOURCE_SUBDIR inexistente: só baixa, sem o CMakeLists de lá.
FetchContent_Declare(miniaudio
    URL           https://github.com/mackron/miniaudio/archive/refs/tags/0.11.22.tar.gz
    URL_HASH      SHA256=bcb07bfb27e6fa94d34da73ba2d5642d4940b208ec2a660dbf4e52e6b7cd492f
    SOURCE_SUBDIR nenhum)
FetchContent_MakeAvailable(miniaudio)

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
add_subdirectory(player)  # Fase 10: executável do jogo exportado
add_subdirectory(editor)

option(MOTOR_BUILD_TESTS "Compila os testes (ctest)" ON)
if(MOTOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
