# Testes sem interface (não abrem janela nem criam contexto OpenGL). Rodar com:
#   ctest --test-dir build --output-on-failure

function(motor_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE Motor)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    if(MINGW)
        target_link_options(${name} PRIVATE -static)
    endif()
    # Cada teste recebe uma pasta de trabalho própria dentro de build/.
    add_test(NAME ${name} COMMAND ${name} ${CMAKE_CURRENT_BINARY_DIR}/tmp/${name})
endfunction()

motor_add_test(test_project)
motor_add_test(test_assets)
motor_add_test(test_guid)
motor_add_test(test_components)

# O histórico de desfazer é do editor; compilamos só ele junto com o teste.
motor_add_test(test_undo ${PROJECT_SOURCE_DIR}/editor/src/UndoHistory.cpp)
target_include_directories(test_undo PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
