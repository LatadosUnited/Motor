# Distribuição do Motor (Fase 15): núcleo comum ao servidor de versões (server/), ao instalador (installer/) e ao
# hub (hub/). Como o hub, não liga na libMotor.dll: só nlohmann_json, cpp-httplib (só o header, já baixado para o
# MCP do editor) e APIs do Windows (BCrypt para o SHA-256, Winsock para o HTTP).

# Núcleo testável sem janela (tests/test_distribuicao.cpp): manifesto, SHA-256, download com retomada e o plano
# de instalação/desinstalação.
add_library(motor_distrib STATIC
    src/Sha256.cpp
    src/Manifest.cpp
    src/Download.cpp
    src/InstallPlan.cpp)
target_include_directories(motor_distrib PUBLIC src ${httplib_SOURCE_DIR})
target_link_libraries(motor_distrib PUBLIC motor_hub_core nlohmann_json::nlohmann_json)
if(WIN32)
    # httplib: Winsock e Windows 10+. bcrypt: SHA-256.
    target_compile_definitions(motor_distrib PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(motor_distrib PUBLIC ws2_32 bcrypt)
endif()

# O que pede ao Windows (Lixeira, atalhos, registro, versão de um .exe): fora do núcleo testável.
add_library(motor_distrib_platform STATIC src/Platform.cpp)
target_link_libraries(motor_distrib_platform PUBLIC motor_distrib)
if(WIN32)
    target_link_libraries(motor_distrib_platform PUBLIC shell32 ole32 uuid version advapi32)
endif()

foreach(target motor_distrib motor_distrib_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
