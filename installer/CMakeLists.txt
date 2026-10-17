# Instalador do Hub do Motor (Fase 15): MotorSetup.exe instala o Hub por usuário (sem administrador), com atalhos e
# a desinstalação em Aplicativos instalados, e opcionalmente uma versão do Motor (de um servidor ou de uma pasta).
# O MotorHub.exe vai embutido como recurso: o instalador funciona sem servidor. Como o hub, só GLFW, ImGui e
# nlohmann_json estáticos, sem a libMotor.dll.
add_executable(MotorSetup
    src/main.cpp
    src/Setup.cpp
    src/SetupApp.cpp
    ${PROJECT_SOURCE_DIR}/hub/src/HubPlatform.cpp)  # seletor de pastas e abrir o hub no fim
target_link_libraries(MotorSetup PRIVATE motor_distrib_platform motor_hub_core imgui glfw)
# A versão do instalador é a do Hub que ele leva (MOTOR_HUB_VERSION, de hub/CMakeLists.txt).
target_compile_definitions(MotorSetup PRIVATE MOTOR_HUB_VERSION="${MOTOR_HUB_VERSION}")
set_target_properties(MotorSetup PROPERTIES WIN32_EXECUTABLE ON)
if(WIN32)
    target_link_libraries(MotorSetup PRIVATE opengl32 shell32 ole32 uuid)
endif()
if(MINGW)
    # Um arquivo só, sem as DLLs do MinGW ao lado (vai ser baixado e rodado em outra máquina). O manifesto padrão
    # do MinGW (asInvoker) evita que o Windows peça administrador por causa do "Setup" no nome.
    target_link_options(MotorSetup PRIVATE -static -static-libgcc -static-libstdc++ -municode)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MotorSetup PRIVATE -Wall -Wextra -Wpedantic)
endif()

# O MotorHub.exe compilado entra como recurso RCDATA "MOTORHUB" (Setup.cpp, EmbeddedHub).
set(motor_embedded_hub "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/MotorHub.exe")
add_dependencies(MotorSetup MotorHub)
include(${PROJECT_SOURCE_DIR}/cmake/MotorVersionInfo.cmake)
motor_version_resource(MotorSetup ${MOTOR_HUB_VERSION} "Instalador do Hub do Motor" MotorSetup.exe
    "MOTORHUB RCDATA \"${motor_embedded_hub}\"")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/MotorSetup_version.rc" PROPERTIES
    OBJECT_DEPENDS "${motor_embedded_hub}")
