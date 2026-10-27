# Assets: banco de GUIDs (.meta), biblioteca de malhas/texturas e carregadores de arquivo.
motor_add_layer(assets
    SOURCES
        AssetDatabase.cpp
        AssetLibrary.cpp
        AssetLibraryMaterials.cpp
        AssetLoaders.cpp
        Animation.cpp
        GltfLoader.cpp
        FbxLoader.cpp
        Model.cpp
        BuiltinAssets.cpp
        TerrainData.cpp
    DEPENDS renderer project
    PRIVATE_LIBS nlohmann_json::nlohmann_json)
target_include_directories(motor_assets PRIVATE ${stb_image_SOURCE_DIR} ${cgltf_SOURCE_DIR} ${ufbx_h_SOURCE_DIR}
                           ${ufbx_c_SOURCE_DIR})
# Leitor de FBX (ufbx, C99): compilado como C, sem os avisos do nosso código (não é nosso).
target_sources(motor_assets PRIVATE ${ufbx_c_SOURCE_DIR}/ufbx.c)
set_source_files_properties(${ufbx_c_SOURCE_DIR}/ufbx.c PROPERTIES COMPILE_OPTIONS "-w")
