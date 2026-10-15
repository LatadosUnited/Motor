# Scripts do jogo, compilados junto com o editor (e com os testes). Hot reload por DLL é o próximo passo
# (ver ROADMAP.md, Fase 3).
add_library(MotorScripts STATIC
    src/Scripts.cpp
    src/Girar.cpp
    src/MoverWasd.cpp
    src/Seguir.cpp
)
target_include_directories(MotorScripts PUBLIC include)
target_link_libraries(MotorScripts PUBLIC Motor)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MotorScripts PRIVATE -Wall -Wextra -Wpedantic)
endif()
