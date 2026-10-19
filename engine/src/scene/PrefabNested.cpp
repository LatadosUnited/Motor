#include "motor/scene/PrefabNested.h"

namespace motor {

MOTOR_COMPONENT_MODULE(PrefabNested) {
    registry.Register<PrefabNested>("PrefabNested", "Prefab aninhado").Field("prefab", "Prefab", &PrefabNested::link);
}

} // namespace motor
