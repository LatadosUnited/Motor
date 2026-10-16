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
motor_add_test(test_component_extension)

# Confere nos #include a ordem das camadas da engine (engine/CMakeLists.txt).
motor_add_test(test_layers)
string(JOIN "," motor_layer_list ${MOTOR_LAYERS})
target_compile_definitions(test_layers PRIVATE
    MOTOR_ENGINE_DIR="${PROJECT_SOURCE_DIR}/engine" MOTOR_LAYERS="${motor_layer_list}")

motor_add_test(test_scripts)
motor_add_test(test_runtime_systems)

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

# Luzes: componente, tipo da luz, coleta para o renderer e conversão de cenas antigas.
motor_add_test(test_lights)

# Física (Jolt): componentes, passo dentro do SceneRuntime, eventos, raios, personagem e contorno do editor.
motor_add_test(test_physics)

# Desempenho da física: caixas caindo e empilhando (pequeno no ctest; números do relatório em Release).
motor_add_test(test_physics_bench)

# Materiais (.mmat) e migração de cenas antigas; prefabs (.mprefab) com overrides e o histórico de desfazer.
motor_add_test(test_materials)
motor_add_test(test_prefabs ${PROJECT_SOURCE_DIR}/editor/src/UndoHistory.cpp)
target_include_directories(test_prefabs PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
