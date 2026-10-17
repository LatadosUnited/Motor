# Player: o executável do jogo exportado (Fase 10). Roda um projeto sem o editor, usando a mesma libMotor.dll
# (e a mesma DLL de scripts) que o editor. A exportação (editor/src/GameExport) copia o Player.exe com o nome
# do jogo. Sem console: o log vai para um arquivo.
add_executable(Player src/main.cpp)
target_link_libraries(Player PRIVATE Motor)
set_target_properties(Player PROPERTIES WIN32_EXECUTABLE ON)
if(WIN32)
    target_link_libraries(Player PRIVATE shell32)  # CommandLineToArgvW
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Player PRIVATE -Wall -Wextra -Wpedantic)
endif()
