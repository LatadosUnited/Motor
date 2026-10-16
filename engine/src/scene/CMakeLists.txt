# Cena: entidades, componentes, scripts, loop de jogo (SceneRuntime) e serialização (.mscene).
motor_add_layer(scene
    SOURCES
        Transform.cpp
        FlyCameraController.cpp
        Component.cpp
        CameraComponent.cpp
        LightComponent.cpp
        MeshRenderer.cpp
        Scene.cpp
        SceneRuntime.cpp
        SceneSerializer.cpp
    DEPENDS assets
    PRIVATE_LIBS nlohmann_json::nlohmann_json)
