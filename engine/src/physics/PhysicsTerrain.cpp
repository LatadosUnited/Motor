// Mundo do Jolt: terrenos (Fase 28). Cada bloco do TerrainData vira um HeightFieldShape num corpo estático, com a
// entidade do Terreno como dono: raios, contatos (OnCollision*) e o personagem acertam o terreno sem Collider. Os
// triângulos são os da malha desenhada (mesma diagonal, TerrainData::HeightAt).

#include "PhysicsWorld.h"

#include "motor/assets/TerrainData.h"
#include "motor/scene/Terrain.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

#include <algorithm>

namespace motor {

using namespace physics_jolt;

namespace {
// Erro máximo da altura guardada pela Jolt (ela comprime as amostras em blocos de 2 x 2): 5 mm.
constexpr float kMaxHeightError = 0.005f;

// Bits por amostra para o erro máximo: a Jolt guarda cada amostra como fração da faixa de alturas do seu bloco de
// 2 x 2 células. Calculado aqui porque o CalculateBitsPerSampleForError da Jolt lê além do fim do vetor quando o
// número de amostras é ímpar (33).
uint32_t BitsForError(const std::vector<float>& heights, int resolution, float maxError) {
    float range = 0.0f;
    for (int z = 0; z + 1 < resolution; z += 2) {
        for (int x = 0; x + 1 < resolution; x += 2) {
            float lo = heights[static_cast<size_t>(z * resolution + x)], hi = lo;
            for (int bz = z; bz <= std::min(z + 2, resolution - 1); ++bz) {
                for (int bx = x; bx <= std::min(x + 2, resolution - 1); ++bx) {
                    const float h = heights[static_cast<size_t>(bz * resolution + bx)];
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
            }
            range = std::max(range, hi - lo);
        }
    }
    uint32_t bits = 1;
    while (bits < JPH::HeightFieldShapeConstants::cMaxBitsPerSample &&
           range / static_cast<float>((1u << bits) - 1u) > maxError) {
        ++bits;
    }
    return bits;
}
} // namespace

JPH::ShapeRefC PhysicsSystem::World::BuildTerrainBlock(const TerrainData& data, TerrainBlockKey key,
                                                       const glm::vec3& worldScale) {
    const std::vector<float>* heights = data.BlockHeights(key);
    if (!heights) return nullptr;
    const glm::vec3 s = glm::max(glm::abs(worldScale), glm::vec3(1e-4f));
    const float spacing = data.Spacing();
    const glm::vec3 corner(static_cast<float>(key.x) * data.BlockSize(), 0.0f, static_cast<float>(key.z) * data.BlockSize());
    // Amostra (i, j) do bloco em offset + scale * (i, altura, j): a mesma ordem (linha a linha em z) do TerrainData.
    JPH::HeightFieldShapeSettings settings(heights->data(), ToJolt(corner * s), JPH::Vec3(spacing * s.x, s.y, spacing * s.z),
                                           static_cast<JPH::uint32>(data.Resolution()));
    settings.mBitsPerSample = BitsForError(*heights, data.Resolution(), kMaxHeightError / std::max(s.y, 1e-4f));
    const JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.IsValid()) return result.Get();
    log::Warn("Física: bloco {}_{} do terreno inválido ({})", key.x, key.z, result.GetError().c_str());
    return nullptr;
}

void PhysicsSystem::World::RemoveTerrainBodies(std::vector<JPH::BodyID>& ids) {
    if (ids.empty()) return;
    for (const JPH::BodyID id : ids) {
        WakeAround(id);  // o que estava em cima cai (ou é refeito no lugar novo)
        DropPairsOf(id);
    }
    JPH::BodyInterface& bi = Bodies();
    bi.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
    bi.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
    ids.clear();
}

void PhysicsSystem::World::RemoveTerrain(TerrainRecord& r) {
    std::vector<JPH::BodyID> ids;
    ids.reserve(r.blocks.size());
    for (const auto& [key, body] : r.blocks) ids.push_back(body.id);
    RemoveTerrainBodies(ids);
    r.blocks.clear();
}

void PhysicsSystem::World::SyncTerrains(Scene& scene, std::vector<JPH::BodyID>& addStatic) {
    terrainBlocksBuilt = 0;
    const std::vector<EntityId>& ids = scene.EntitiesWith<Terrain>();
    if (ids.empty() && terrains.empty()) return;
    for (const EntityId id : ids) {
        Entity* entity = scene.Find(id);
        const Terrain* terrain = entity ? entity->Get<Terrain>() : nullptr;
        if (!terrain || !terrain->collision || !scene.IsActiveInHierarchy(*entity)) continue;
        const TerrainData* data = terrain->terrain.Data();
        if (!data) continue;
        const glm::mat4 world = scene.WorldMatrix(*entity);
        TerrainRecord& r = terrains[id];
        r.seen = frame;
        const bool same = r.data == data && r.world == world && r.friction == terrain->friction &&
                          r.layer == terrain->layer;
        // O caminho comum: nada mudou (as alturas só mudam pelos pincéis, no editor, ou por script).
        if (same && r.version == data->Version()) continue;
        if (!same) {
            RemoveTerrain(r);
            r.data = data;
            r.world = world;
            r.friction = terrain->friction;
            r.layer = terrain->layer;
        }
        r.version = data->Version();
        // Saem os blocos tirados e os de alturas mudadas (refeitos abaixo); pintar não muda a versão de bloco nenhum.
        std::vector<JPH::BodyID> gone;
        for (auto it = r.blocks.begin(); it != r.blocks.end();) {
            if (data->BlockVersion(it->first) != it->second.version) {
                gone.push_back(it->second.id);
                it = r.blocks.erase(it);
            } else {
                ++it;
            }
        }
        RemoveTerrainBodies(gone);
        const Pose pose = Decompose(world);
        const JPH::ObjectLayer layer = MakeLayer(false, terrain->layer.index);
        for (const TerrainBlockKey key : data->Blocks()) {
            if (r.blocks.contains(key)) continue;
            const JPH::ShapeRefC shape = BuildTerrainBlock(*data, key, pose.scale);
            if (!shape) continue;
            JPH::BodyCreationSettings settings(shape, ToJolt(pose.position), ToJolt(pose.rotation),
                                               JPH::EMotionType::Static, layer);
            settings.mUserData = id;
            settings.mFriction = std::max(terrain->friction, 0.0f);
            JPH::Body* body = Bodies().CreateBody(settings);
            if (!body) {
                WarnOnce(id, "Física: limite de corpos atingido (terreno)");
                break;
            }
            r.blocks[key] = {body->GetID(), data->BlockVersion(key)};
            addStatic.push_back(body->GetID());
            ++terrainBlocksBuilt;
        }
    }
    // Terreno que saiu, foi desativado ou perdeu a colisão.
    for (auto it = terrains.begin(); it != terrains.end();) {
        if (it->second.seen != frame) {
            RemoveTerrain(it->second);
            it = terrains.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace motor
