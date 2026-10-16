# Cena: entidades, componentes, scripts, loop de jogo (SceneRuntime), serialização (.mscene) e prefabs (.mprefab).
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
        ColliderComponent.cpp
        RigidbodyComponent.cpp
        CharacterControllerComponent.cpp
        PrefabInstance.cpp
        Prefab.cpp
    DEPENDS assets
    PRIVATE_LIBS nlohmann_json::nlohmann_json)
