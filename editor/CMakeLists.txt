add_executable(MotorEditor
    src/main.cpp
    src/EditorApp.cpp
    src/EditorContext.cpp
    src/EditorSettings.cpp
    src/drawers/FieldDrawers.cpp
    src/FileDialog.cpp
    src/panels/HierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/play/PlayController.cpp
    src/play/PlayMode.cpp
    src/panels/ProjectPanel.cpp
    src/panels/ProjectPanelDraw.cpp
    src/panels/ProjectPanelFiles.cpp
    src/panels/ProjectPanelUtil.cpp
    src/document/SceneDocument.cpp
    src/document/SceneDocumentProject.cpp
    src/document/SceneDocumentUi.cpp
    src/document/SceneDocumentLayers.cpp
    src/panels/ScenePanel.cpp
    src/viewport/SceneViewport.cpp
    src/viewport/SceneViewportOverlay.cpp
    src/scripts/ScriptBuilder.cpp
    src/scripts/ScriptController.cpp
    src/scripts/ScriptHost.cpp
    src/scripts/ScriptIde.cpp
    src/scripts/ScriptProject.cpp
    src/UndoHistory.cpp
    src/drawers/LightTypeDrawer.cpp
    src/drawers/ColliderShapeDrawer.cpp
    src/drawers/MaterialRefDrawer.cpp
    src/drawers/PrefabLinkDrawer.cpp
    src/drawers/PrefabRefDrawer.cpp
    src/viewport/UiEditor.cpp
    src/drawers/UiFieldDrawers.cpp
    src/drawers/ModelRefDrawer.cpp
    src/drawers/ShadowQualityDrawer.cpp
    src/drawers/AudioClipDrawer.cpp
    src/drawers/AudioGroupDrawer.cpp
    src/drawers/BlendModeDrawer.cpp
    src/drawers/EntityRefDrawer.cpp
    src/drawers/CollisionLayerDrawer.cpp
    src/mcp/ImageEncode.cpp
    src/mcp/McpController.cpp
    src/mcp/McpFileTools.cpp
    src/mcp/McpPlayTools.cpp
    src/mcp/McpProtocol.cpp
    src/mcp/McpSceneTools.cpp
    src/mcp/McpServer.cpp
    src/export/GameExport.cpp
    src/export/ExportController.cpp
    src/viewport/ColliderOverlay.cpp
    src/panels/MultiEdit.cpp
    src/EntityClipboard.cpp
)
target_link_libraries(MotorEditor PRIVATE Motor imguizmo nlohmann_json::nlohmann_json)
# Includes do editor a partir de src/ ("scripts/ScriptHost.h"); as peças comuns ficam na raiz de src/.
target_include_directories(MotorEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
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

# Arquivo > Exportar jogo copia o Player.exe de build/bin: compilar o editor compila o Player junto.
add_dependencies(MotorEditor Player)
