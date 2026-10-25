#pragma once

// Componentes que vêm com a engine. Cada um tem o seu header e registra os seus campos num .cpp próprio
// (MOTOR_COMPONENT_MODULE, ver Component.h). Componente novo da engine: header + .cpp novos e uma linha aqui.

#include "motor/scene/CameraComponent.h"
#include "motor/scene/MeshRenderer.h"
#include "motor/scene/LightComponent.h"
#include "motor/scene/ColliderComponent.h"
#include "motor/scene/RigidbodyComponent.h"
#include "motor/scene/CharacterControllerComponent.h"
#include "motor/scene/PrefabInstance.h"
#include "motor/scene/Animator.h"
#include "motor/scene/AudioSource.h"
#include "motor/scene/AudioListener.h"
#include "motor/scene/EntityRef.h"
#include "motor/scene/PrefabNested.h"
#include "motor/scene/NavMeshSurface.h"
#include "motor/scene/NavMeshAgent.h"
#include "motor/scene/NavMeshLink.h"
