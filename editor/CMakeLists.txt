add_executable(MotorEditor
    src/main.cpp
    src/EditorApp.cpp
    src/EditorContext.cpp
    src/EditorFile.cpp
    src/EditorScripts.cpp
    src/EditorSettings.cpp
    src/FileDialog.cpp
    src/HierarchyPanel.cpp
    src/InspectorPanel.cpp
    src/PlayMode.cpp
    src/ProjectPanel.cpp
    src/ScenePanel.cpp
    src/ScriptBuilder.cpp
    src/ScriptHost.cpp
    src/ScriptProject.cpp
    src/UndoHistory.cpp
)
target_link_libraries(MotorEditor PRIVATE Motor imguizmo nlohmann_json::nlohmann_json)
if(WIN32)
    # comdlg32: abrir/salvar; ole32/uuid: seletor de pastas (COM); shell32: Lixeira e Explorer.
    target_link_libraries(MotorEditor PRIVATE comdlg32 ole32 uuid shell32)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MotorEditor PRIVATE -Wall -Wextra)
endif()

# Modelos de script (copiados para projetos novos e pelo menu Scripts) ficam ao lado do executável.
add_custom_command(TARGET MotorEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/templates $<TARGET_FILE_DIR:MotorEditor>/templates
    COMMENT "Copiando modelos de script")
