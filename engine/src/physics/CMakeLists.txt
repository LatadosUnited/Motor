# Física (Jolt Physics): sistema do SceneRuntime que move os corpos dos componentes Collider/Rigidbody e os
# personagens (CharacterController). O Jolt não aparece nos headers públicos: os scripts não precisam dele.
motor_add_layer(physics
    SOURCES
        PhysicsSystem.cpp
        ColliderGizmo.cpp
    DEPENDS scene
    PRIVATE_LIBS Jolt)

# Headers do Jolt como "de sistema": os avisos de -Wpedantic de lá não são do nosso código.
target_include_directories(motor_physics SYSTEM PRIVATE ${jolt_SOURCE_DIR})

# A libMotor.dll exporta todos os símbolos (--export-all-symbols); os do Jolt ficam de fora.
if(MINGW)
    target_link_options(motor_physics INTERFACE -Wl,--exclude-libs,libJolt.a)
endif()
