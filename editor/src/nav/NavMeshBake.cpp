#include "nav/NavMeshBake.h"

#include "EditorContext.h"

#include <motor/assets/AssetLoaders.h>
#include <motor/core/Log.h>
#include <motor/scene/NavMeshSurface.h>

#include <format>

namespace editor {

NavBakeResult BakeNavMesh(EditorContext& ctx, motor::NavMeshSurface& surface) {
    NavBakeResult result;
    if (ctx.playing) {
        result.error = "pare o Play para gerar o navmesh";
        return result;
    }
    std::filesystem::path file = surface.navMesh.file;
    if (file.empty()) file = ctx.NewAssetPath("Navegacao", "Navmesh", motor::kNavMeshFileExtension);
    if (file.empty()) {
        result.error = "abra ou crie um projeto para gerar o navmesh";
        return result;
    }

    const auto nav = motor::BuildSceneNavMesh(ctx.scene, surface, &result.stats);
    if (!nav) {
        result.error = std::format("o navmesh não foi gerado: {}", result.stats.error);
        return result;
    }
    std::string error;
    if (!nav->Save(file, &error)) {
        result.error = std::format("não foi possível gravar o navmesh: {}", error);
        return result;
    }
    // Arquivo novo: o GUID sai do .meta criado agora.
    surface.navMesh.file = file;
    surface.navMesh.guid = ctx.assets.Database().GuidFor(file);
    surface.navMesh.path = ctx.assets.KeyForFile(file);
    result.ok = true;
    result.file = file;
    motor::log::Info("Navegação: navmesh {} gerado com {} polígonos ({} triângulos) em {:.0f} ms", surface.navMesh.path,
                     result.stats.polygons, result.stats.triangles, result.stats.buildMs);
    return result;
}

motor::Entity* FindNavSurface(EditorContext& ctx) {
    for (motor::EntityId id : ctx.scene.EntitiesWith<motor::NavMeshSurface>()) {
        motor::Entity* e = ctx.scene.Find(id);
        if (e && ctx.scene.IsActiveInHierarchy(id)) return e;
    }
    return nullptr;
}

} // namespace editor
