# Servidor de versões do Motor (Fase 15): MotorServidor.exe publica versões montadas (alvo motor_package) e o Hub
# numa pasta e serve essa pasta por HTTP (cpp-httplib) para o Hub e o instalador baixarem.
set(MOTOR_SERVER_VERSION 0.1.0)

# Rotas HTTP sem a linha de comando (testadas em loopback por tests/test_distribuicao.cpp).
add_library(motor_server_core STATIC src/ServerCore.cpp)
target_include_directories(motor_server_core PUBLIC src)
target_link_libraries(motor_server_core PUBLIC motor_distrib)

add_executable(MotorServidor src/main.cpp)
target_link_libraries(MotorServidor PRIVATE motor_server_core motor_distrib_platform)
target_compile_definitions(MotorServidor PRIVATE MOTOR_SERVER_VERSION="${MOTOR_SERVER_VERSION}")
include(${PROJECT_SOURCE_DIR}/cmake/MotorVersionInfo.cmake)
motor_version_resource(MotorServidor ${MOTOR_SERVER_VERSION} "Servidor de versoes do Motor" MotorServidor.exe)
if(MINGW)
    # Roda sozinho, sem as DLLs do MinGW ao lado (pode ir para outra máquina da rede).
    target_link_options(MotorServidor PRIVATE -static -static-libgcc -static-libstdc++)
endif()

foreach(target motor_server_core MotorServidor)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
