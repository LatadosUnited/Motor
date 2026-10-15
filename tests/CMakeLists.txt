# Testes sem interface (não abrem janela nem criam contexto OpenGL). Rodar com:
#   ctest --test-dir build --output-on-failure

function(motor_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE Motor)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    # Cada teste recebe uma pasta de trabalho própria dentro de build/.
    add_test(NAME ${name} COMMAND ${name} ${CMAKE_CURRENT_BINARY_DIR}/tmp/${name})
endfunction()

motor_add_test(test_project)
motor_add_test(test_assets)
motor_add_test(test_guid)
motor_add_test(test_components)

motor_add_test(test_scripts)

# O histórico de desfazer e o modo Play são do editor; compilamos só eles junto com os testes.
motor_add_test(test_undo ${PROJECT_SOURCE_DIR}/editor/src/UndoHistory.cpp)
target_include_directories(test_undo PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
motor_add_test(test_play ${PROJECT_SOURCE_DIR}/editor/src/PlayMode.cpp)
target_include_directories(test_play PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)

# Componentes desconhecidos e salvar/restaurar o estado na recarga dos scripts (sem DLL).
motor_add_test(test_hotreload ${PROJECT_SOURCE_DIR}/editor/src/UndoHistory.cpp)
target_include_directories(test_hotreload PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)

# Scripts por projeto: CMake gerado, diagnósticos e, de ponta a ponta, compilar uma DLL de scripts com o
# toolchain do Motor, carregar, recarregar e descarregar (sem janela). Demora alguns segundos.
motor_add_test(test_script_project
    ${PROJECT_SOURCE_DIR}/editor/src/ScriptBuilder.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/ScriptHost.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/ScriptProject.cpp)
target_include_directories(test_script_project PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
target_link_libraries(test_script_project PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(test_script_project PRIVATE
    MOTOR_TEST_SDK_JSON="${CMAKE_BINARY_DIR}/bin/sdk/MotorSDK.json"
    MOTOR_TEST_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/editor/templates/Scripts")
set_tests_properties(test_script_project PROPERTIES TIMEOUT 600)
