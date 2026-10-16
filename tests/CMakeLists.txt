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

# Confere nos #include a ordem das camadas da engine (engine/CMakeLists.txt).
motor_add_test(test_layers)
string(JOIN "," motor_layer_list ${MOTOR_LAYERS})
target_compile_definitions(test_layers PRIVATE
    MOTOR_ENGINE_DIR="${PROJECT_SOURCE_DIR}/engine" MOTOR_LAYERS="${motor_layer_list}")

motor_add_test(test_scripts)
target_link_libraries(test_scripts PRIVATE MotorScripts)

# O histórico de desfazer e o modo Play são do editor; compilamos só eles junto com os testes.
motor_add_test(test_undo ${PROJECT_SOURCE_DIR}/editor/src/UndoHistory.cpp)
target_include_directories(test_undo PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
motor_add_test(test_play ${PROJECT_SOURCE_DIR}/editor/src/PlayMode.cpp)
target_include_directories(test_play PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
target_link_libraries(test_play PRIVATE MotorScripts)
