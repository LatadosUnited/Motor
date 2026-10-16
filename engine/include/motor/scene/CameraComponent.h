#pragma once

namespace motor {

// Ponto de vista do jogo: no Play, o viewport mostra a cena pela câmera principal (Scene::FindMainCamera).
// Olha para o -Z local da entidade (a rotação em torno do próprio eixo, "roll", ainda é ignorada).
struct CameraComponent {
    float fov = 60.0f;  // vertical, em graus
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
    bool main = true;   // havendo várias, vale a primeira ativa marcada como principal
};

} // namespace motor
