# Navegação e IA (Fase 34): navmesh gerado pela Recast a partir dos Colliders estáticos, caminhos pelo Detour e o
# sistema do SceneRuntime que move os NavMeshAgent (pela física, com CharacterController). Recast e Detour não
# aparecem nos headers públicos.
motor_add_layer(ai
    SOURCES
        NavCrowd.cpp
        NavMesh.cpp
        NavMeshFile.cpp
        NavSceneGeometry.cpp
        NavSystem.cpp
        PerceptionSystem.cpp
    DEPENDS scene physics
    PRIVATE_LIBS Recast)

# A libMotor.dll exporta todos os símbolos (--export-all-symbols); os da Recast ficam de fora.
if(MINGW)
    target_link_options(motor_ai INTERFACE -Wl,--exclude-libs,libRecast.a)
endif()
