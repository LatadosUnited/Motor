# Assets: banco de GUIDs (.meta), biblioteca de malhas/texturas e carregadores de arquivo.
motor_add_layer(assets
    SOURCES
        AssetDatabase.cpp
        AssetLibrary.cpp
        AssetLibraryMaterials.cpp
        AssetLoaders.cpp
        Animation.cpp
        GltfLoader.cpp
        Model.cpp
        BuiltinAssets.cpp
    DEPENDS renderer project
    PRIVATE_LIBS nlohmann_json::nlohmann_json)
target_include_directories(motor_assets PRIVATE ${stb_image_SOURCE_DIR} ${cgltf_SOURCE_DIR})
