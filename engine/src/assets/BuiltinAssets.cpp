#include "motor/assets/BuiltinAssets.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Texture.h"

namespace motor {

void AddBuiltinAssets(AssetLibrary& assets) {
    assets.AddMesh("Cubo", Mesh::CreateCube());
    assets.AddMesh("Esfera", Mesh::CreateSphere());
    assets.AddMesh("Plano", Mesh::CreatePlane());
    assets.AddTexture("Xadrez", Texture2D::CreateChecker(512, 16, {215, 215, 220, 255}, {150, 155, 165, 255}));
    assets.AddTexture("Xadrez grosso", Texture2D::CreateChecker(256, 4, {240, 240, 240, 255}, {60, 60, 70, 255}));
}

} // namespace motor
