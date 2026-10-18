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
motor_add_test(test_play ${PROJECT_SOURCE_DIR}/editor/src/play/PlayMode.cpp)
target_include_directories(test_play PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)

# Componentes desconhecidos e salvar/restaurar o estado na recarga dos scripts (sem DLL).
motor_add_test(test_hotreload ${PROJECT_SOURCE_DIR}/editor/src/UndoHistory.cpp)
target_include_directories(test_hotreload PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)

# Scripts por projeto: CMake gerado, diagnósticos e, de ponta a ponta, compilar uma DLL de scripts com o
# toolchain do Motor, carregar, recarregar e descarregar (sem janela). Demora alguns segundos.
motor_add_test(test_script_project
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptBuilder.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptHost.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptIde.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptProject.cpp)
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

# Documentação dos scripts: compila os exemplos do guia (docs/examples) e os modelos do editor numa DLL de
# scripts, roda os exemplos e confere que o guia e a referência batem com os arquivos e os headers.
motor_add_test(test_script_docs
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptBuilder.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptHost.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/scripts/ScriptProject.cpp)
target_include_directories(test_script_docs PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
target_link_libraries(test_script_docs PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(test_script_docs PRIVATE
    MOTOR_TEST_SDK_JSON="${CMAKE_BINARY_DIR}/bin/sdk/MotorSDK.json"
    MOTOR_TEST_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/editor/templates/Scripts"
    MOTOR_TEST_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs"
    MOTOR_TEST_ENGINE_INCLUDE="${PROJECT_SOURCE_DIR}/engine/include")
set_tests_properties(test_script_docs PROPERTIES TIMEOUT 600)

# Interface do jogo (camada ui/): âncoras, fontes SDF, cache da lista de desenho, cliques pelo UiSystem, API dos
# scripts, serialização e a troca de cena pelo Play do editor (PlayMode).
motor_add_test(test_ui ${PROJECT_SOURCE_DIR}/editor/src/play/PlayMode.cpp)
target_include_directories(test_ui PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)

# Modelos glTF e animação esquelética (Fase 11): leitura, asset, importação como prefab, dados da malha na
# CPU (raycast e collider exatos), amostragem dos clipes, skinning e Animator. Gera os próprios arquivos glTF.
motor_add_test(test_gltf)
target_link_libraries(test_gltf PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(test_gltf PRIVATE MOTOR_ENGINE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/engine/assets")

# Servidor MCP do editor (Fase 13): protocolo, ferramentas de cena, PNG/base64 e o transporte HTTP (cpp-httplib).
motor_add_test(test_mcp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorContext.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/viewport/UiEditor.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/mcp/ImageEncode.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/mcp/McpProtocol.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/mcp/McpSceneTools.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/mcp/McpServer.cpp)
target_include_directories(test_mcp PRIVATE ${PROJECT_SOURCE_DIR}/editor/src ${httplib_SOURCE_DIR})
target_link_libraries(test_mcp PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(test_mcp PRIVATE ws2_32)
    target_compile_definitions(test_mcp PRIVATE _WIN32_WINNT=0x0A00)
endif()

# Renderização avançada (Fase 7) sem GPU: cascatas, recorte, BRDF, harmônicos do céu, HDR e formatos novos.
motor_add_test(test_render)

# Exportar o jogo (Fase 10): assets referenciados (cenas, prefabs, materiais, glTF, fontes, Recursos, scripts) e a
# pasta exportada (Player, DLLs, engine/, Dados/), com arquivos falsos no lugar do Player e das DLLs.
motor_add_test(test_export ${PROJECT_SOURCE_DIR}/editor/src/export/GameExport.cpp)
target_include_directories(test_export PRIVATE ${PROJECT_SOURCE_DIR}/editor/src)
target_link_libraries(test_export PRIVATE nlohmann_json::nlohmann_json)
# Áudio (Fase 8) sem dispositivo de som: clipes como assets com GUID, som 3D (distância e lado), som 2D tocado
# por script, pausa, fontes desativadas/excluídas e arquivos inválidos. Gera os próprios .wav.
motor_add_test(test_audio)

# Versão da engine (Fase 14): Version.h e engineVersion no .mproject (formato 2; o 1 abre como desconhecida).
motor_add_test(test_project_version)
target_link_libraries(test_project_version PRIVATE nlohmann_json::nlohmann_json)

# Hub do Motor (Fase 14): núcleo sem janela (versões, projetos, escolha da versão, hub.json, linha de comando do
# editor). Sem a libMotor.dll, como o próprio hub.
add_executable(test_hub test_hub.cpp)
target_link_libraries(test_hub PRIVATE motor_hub_core)
target_include_directories(test_hub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_hub PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(test_hub PRIVATE shell32)  # CommandLineToArgvW
endif()
add_test(NAME test_hub COMMAND test_hub ${CMAKE_CURRENT_BINARY_DIR}/tmp/test_hub)

# Distribuição (Fase 15): SHA-256, manifesto, pasta de publicações, plano de instalação/desinstalação, hub.json
# formato 2 e o servidor de versões com o cliente em loopback (porta livre em 127.0.0.1). Sem a libMotor.dll.
add_executable(test_distribuicao test_distribuicao.cpp)
target_link_libraries(test_distribuicao PRIVATE motor_server_core motor_distrib)
target_include_directories(test_distribuicao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_distribuicao PRIVATE -Wall -Wextra)
endif()
add_test(NAME test_distribuicao COMMAND test_distribuicao ${CMAKE_CURRENT_BINARY_DIR}/tmp/test_distribuicao)

# API dos scripts da Fase 17 (sem DLL): temporizadores, mensagens, aleatórios, GetComponent, EntityRef (cópias e
# prefabs), MOTOR_FIELDS e campos renomeados ou perdidos na recarga.
motor_add_test(test_script_api)
