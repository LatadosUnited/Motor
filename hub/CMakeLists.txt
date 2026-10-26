# Hub do Motor (Fase 14): MotorHub.exe, programa separado do editor para projetos e versões instaladas.
#
# Não liga na libMotor.dll nem usa headers da engine: o hub abre projetos de qualquer versão (inclusive futuras)
# e não pode ficar preso a uma. Usa só GLFW, ImGui e nlohmann_json, estáticos (as mesmas bibliotecas do Motor,
# baixadas pelo CMakeLists.txt da raiz). Pode virar um repositório próprio: este CMakeLists só precisa dos alvos
# glfw, imgui (objetos), nlohmann_json::nlohmann_json e motor_common (common/, sem dependências).
set(MOTOR_HUB_VERSION 0.3.0)  # versão do hub, separada da do Motor (0.2: baixa versões de servidores; 0.3: residente)
set(MOTOR_HUB_VERSION ${MOTOR_HUB_VERSION} PARENT_SCOPE)  # o instalador (installer/) leva este hub

# Núcleo sem janela (testado em tests/test_hub.cpp).
add_library(motor_hub_core STATIC src/HubCore.cpp)
target_include_directories(motor_hub_core PUBLIC src)
# motor_common (common/): só a gravação segura de arquivos, sem nada da engine.
target_link_libraries(motor_hub_core PUBLIC nlohmann_json::nlohmann_json motor_common)

# O hub inteiro menos o main.cpp: o MotorHub.exe e o tests/test_hub_mcp.cpp (ferramentas do MCP sem janela).
add_library(motor_hub_app STATIC
    src/HubApp.cpp
    src/HubProjectsTab.cpp
    src/HubVersionsTab.cpp
    src/HubWidgets.cpp
    src/HubPlatform.cpp
    src/HubTray.cpp
    src/HubMcp.cpp
    src/HubMcpTab.cpp
    src/HubEngineProxy.cpp)
# Objetos do ImGui (os mesmos que vão na libMotor.dll, aqui ligados direto no .exe) e o GLFW estático.
# motor_distrib (Fase 15, distrib/): baixar versões dos servidores; vem depois no CMakeLists da raiz, mas o alvo é
# resolvido pelo nome.
# motor_mcp (mcp/): o MCP do hub (Hub residente), sem a engine.
target_link_libraries(motor_hub_app PUBLIC motor_hub_core motor_distrib motor_distrib_platform motor_mcp imgui glfw)
target_compile_definitions(motor_hub_app PUBLIC MOTOR_HUB_VERSION="${MOTOR_HUB_VERSION}" NOMINMAX WIN32_LEAN_AND_MEAN)
if(WIN32)
    # opengl32: glClear/glViewport; shell32: Lixeira, Explorer e a linha de comando; ole32/uuid: seletor de pastas.
    target_link_libraries(motor_hub_app PUBLIC opengl32 shell32 ole32 uuid)
endif()

add_executable(MotorHub src/main.cpp)
target_link_libraries(MotorHub PRIVATE motor_hub_app)
include(${PROJECT_SOURCE_DIR}/cmake/MotorVersionInfo.cmake)
motor_version_resource(MotorHub ${MOTOR_HUB_VERSION} "Hub do Motor" MotorHub.exe)
set_target_properties(MotorHub PROPERTIES WIN32_EXECUTABLE ON)
if(MINGW)
    # Runtime do GCC estático: o hub roda sozinho, sem as DLLs do MinGW ao lado (não é instalado com uma versão).
    target_link_options(MotorHub PRIVATE -static -static-libgcc -static-libstdc++ -municode)
endif()

foreach(target motor_hub_core motor_hub_app MotorHub)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
