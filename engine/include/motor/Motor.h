#pragma once

// Header "guarda-chuva" da engine Motor.

#include "motor/assets/AssetDatabase.h"
#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"

#include "motor/app/Application.h"
#include "motor/core/ImGuiLayer.h"
#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/core/Window.h"

#include "motor/renderer/Camera.h"
#include "motor/renderer/Framebuffer.h"
#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Renderer.h"
#include "motor/renderer/Shader.h"
#include "motor/renderer/Texture.h"

#include "motor/project/Project.h"

#include "motor/scene/Component.h"
#include "motor/scene/Components.h"
#include "motor/scene/FlyCameraController.h"
#include "motor/scene/Prefab.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/scene/SceneSerializer.h"
#include "motor/scene/Script.h"
#include "motor/scene/Transform.h"

#include "motor/physics/ColliderGizmo.h"
#include "motor/physics/PhysicsSystem.h"

#include "motor/ui/Ui.h"

// Fase 10 (exportar o jogo): assets embutidos e pasta dos assets da engine.
#include "motor/assets/BuiltinAssets.h"
#include "motor/core/EngineAssets.h"
#include "motor/audio/AudioSystem.h"
#include "motor/scene/GameHost.h"

// Fase 34: navegação (navmesh, agentes).
#include "motor/ai/NavMesh.h"
#include "motor/ai/NavSystem.h"

// Fase 37: percepção (visão, audição, memória dos agentes).
#include "motor/ai/PerceptionSystem.h"
