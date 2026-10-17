add_executable(MotorEditor
    src/main.cpp
    src/EditorApp.cpp
    src/EditorContext.cpp
    src/EditorSettings.cpp
    src/FieldDrawers.cpp
    src/FileDialog.cpp
    src/HierarchyPanel.cpp
    src/InspectorPanel.cpp
    src/PlayController.cpp
    src/PlayMode.cpp
    src/ProjectPanel.cpp
    src/SceneDocument.cpp
    src/ScenePanel.cpp
    src/SceneViewport.cpp
    src/ScriptBuilder.cpp
    src/ScriptController.cpp
    src/ScriptHost.cpp
    src/ScriptProject.cpp
    src/UndoHistory.cpp
    src/LightTypeDrawer.cpp
    src/ColliderShapeDrawer.cpp
    src/MaterialRefDrawer.cpp
    src/PrefabLinkDrawer.cpp
    src/UiEditor.cpp
    src/UiFieldDrawers.cpp
    src/ModelRefDrawer.cpp
    src/ShadowQualityDrawer.cpp
    src/AudioClipDrawer.cpp
    src/mcp/ImageEncode.cpp
    src/mcp/McpController.cpp
    src/mcp/McpProtocol.cpp
    src/mcp/McpSceneTools.cpp
    src/mcp/McpServer.cpp
)
target_link_libraries(MotorEditor PRIVATE Motor imguizmo nlohmann_json::nlohmann_json)
if(WIN32)
    # comdlg32: abrir/salvar; ole32/uuid: seletor de pastas (COM); shell32: Lixeira e Explorer.
    target_link_libraries(MotorEditor PRIVATE comdlg32 ole32 uuid shell32)
endif()

# Servidor MCP (src/mcp): cpp-httplib, só no editor. No Windows precisa do Winsock e de Windows 10+.
target_include_directories(MotorEditor PRIVATE ${httplib_SOURCE_DIR})
if(WIN32)
    target_link_libraries(MotorEditor PRIVATE ws2_32)
    target_compile_definitions(MotorEditor PRIVATE _WIN32_WINNT=0x0A00)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MotorEditor PRIVATE -Wall -Wextra)
endif()

# Modelos de script (copiados para projetos novos e pelo menu Scripts) ficam ao lado do executável.
add_custom_command(TARGET MotorEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/templates $<TARGET_FILE_DIR:MotorEditor>/templates
    COMMENT "Copiando modelos de script")
