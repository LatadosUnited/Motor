#include "CenaReferencia.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/ui/UiComponents.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

using namespace motor;

namespace bench {

namespace {
constexpr int kEstaticos = 4000;
constexpr int kComCollider = 500;  // dos estáticos, os primeiros
constexpr int kCaixas = 900;
constexpr int kAlturaColuna = 10;
constexpr int kScripts = 50;
constexpr int kPontuais = 8;
constexpr int kSons = 10;

glm::vec4 Cor(int i) {
    static const glm::vec4 paleta[] = {
        {0.95f, 0.35f, 0.30f, 1.0f}, {0.98f, 0.75f, 0.25f, 1.0f}, {0.35f, 0.80f, 0.45f, 1.0f},
        {0.30f, 0.60f, 0.95f, 1.0f}, {0.70f, 0.45f, 0.90f, 1.0f},
    };
    return paleta[i % 5];
}

Entity& Malha(Scene& scene, const std::string& nome, const std::shared_ptr<Mesh>& mesh, const glm::vec3& pos,
              const glm::vec3& escala, const glm::vec4& cor) {
    Entity& e = scene.CreateEntity(nome);
    e.transform.position = pos;
    e.transform.scale = escala;
    MeshRenderer& mr = e.Add<MeshRenderer>();
    mr.mesh = mesh;
    mr.material.color = cor;
    return e;
}
} // namespace

void MontarCenaReferencia(Scene& scene, AssetLibrary& assets) {
    const std::shared_ptr<Mesh> cubo = assets.GetMesh("Cubo");
    const std::shared_ptr<Mesh> esfera = assets.GetMesh("Esfera");

    // Chão.
    Entity& chao = Malha(scene, "Chão", cubo, {0.0f, -0.5f, 0.0f}, {220.0f, 1.0f, 220.0f}, {0.55f, 0.57f, 0.6f, 1.0f});
    chao.Add<ColliderComponent>();

    // Estáticos: grade de 80 x 50 com passo de 2,5 m, longe do centro onde as caixas caem.
    for (int i = 0; i < kEstaticos; ++i) {
        const int x = i % 80, z = i / 80;
        const bool ehEsfera = i % 4 == 3;
        const glm::vec3 pos{(x - 40) * 2.5f, ehEsfera ? 0.6f : 0.5f, (z - 25) * 2.5f - 70.0f};
        Entity& e = Malha(scene, ehEsfera ? "Esfera" : "Cubo", ehEsfera ? esfera : cubo, pos,
                          glm::vec3(ehEsfera ? 1.2f : 1.0f), Cor(i));
        if (i < kComCollider) {
            ColliderComponent& c = e.Add<ColliderComponent>();
            if (ehEsfera) c.shape = ColliderShape::Sphere;
        }
    }

    // Caixas com corpo rígido: colunas de 10 numa grade de 9 x 10, com vão (caem um pouco e empilham).
    for (int i = 0; i < kCaixas; ++i) {
        const int coluna = i / kAlturaColuna, nivel = i % kAlturaColuna;
        const glm::vec3 pos{(coluna % 9 - 4) * 1.6f, 0.6f + nivel * 1.15f, (coluna / 9 - 5) * 1.6f};
        Entity& e = Malha(scene, "Caixa", cubo, pos, glm::vec3(1.0f), Cor(i + 2));
        e.transform.rotation = glm::angleAxis(0.05f * static_cast<float>(i % 7), glm::vec3(0.0f, 1.0f, 0.0f));
        e.Add<ColliderComponent>();
        e.Add<RigidbodyComponent>();
    }

    // Cubos com script, numa fila à frente da câmera.
    const ComponentType* bobber = ComponentRegistry::Instance().Find("Bobber");
    for (int i = 0; i < kScripts; ++i) {
        Entity& e = Malha(scene, "Bobber", cubo, {(i - 25) * 1.5f, 2.0f, 16.0f}, glm::vec3(0.6f), Cor(i));
        if (bobber) e.Add(*bobber);
    }

    // Luzes: o sol com sombra e 8 pontuais em círculo.
    Entity& sol = scene.CreateEntity("Sol");
    sol.transform.rotation = glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                             glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    LightComponent& luzSol = sol.Add<LightComponent>();
    luzSol.type = LightType::Directional;
    luzSol.intensity = 2.5f;
    luzSol.castShadows = true;
    luzSol.shadowQuality = ShadowQuality::Medium;
    for (int i = 0; i < kPontuais; ++i) {
        const float a = static_cast<float>(i) / kPontuais * 6.2831853f;
        Entity& e = scene.CreateEntity("Luz " + std::to_string(i + 1));
        e.transform.position = {std::cos(a) * 18.0f, 3.0f, std::sin(a) * 18.0f};
        LightComponent& l = e.Add<LightComponent>();
        l.color = glm::vec3(Cor(i));
        l.intensity = 4.0f;
        l.range = 14.0f;
        l.castShadows = false;
    }

    // Câmera (com o ouvinte) olhando o centro de cima.
    Entity& camera = scene.CreateEntity("Câmera");
    camera.transform.position = {0.0f, 22.0f, 38.0f};
    camera.transform.rotation = glm::angleAxis(glm::radians(-30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    camera.Add<CameraComponent>().farPlane = 400.0f;
    camera.Add<AudioListener>();

    // Fontes de som sem arquivo e sem tocar ao iniciar (sem aviso no log): o sistema de áudio passa por elas a cada
    // quadro, como num jogo com sons parados.
    for (int i = 0; i < kSons; ++i) {
        Entity& e = scene.CreateEntity("Som " + std::to_string(i + 1));
        e.transform.position = {(i - 5) * 6.0f, 1.0f, -10.0f};
        AudioSource& s = e.Add<AudioSource>();
        s.loop = true;
        s.playOnStart = false;
    }

    // Interface: painel no canto, um título e um botão.
    Entity& painel = scene.CreateEntity("Painel");
    UiRect& rp = painel.Add<UiRect>();
    rp.anchor = UiAnchor::TopLeft;
    rp.position = {20.0f, 20.0f};
    rp.size = {360.0f, 140.0f};
    painel.Add<UiImage>().color = {0.05f, 0.06f, 0.08f, 0.6f};
    const EntityId painelId = painel.id;
    Entity& titulo = scene.CreateEntity("Título", painelId);
    UiRect& rt = titulo.Add<UiRect>();
    rt.anchor = UiAnchor::Top;
    rt.position = {0.0f, 12.0f};
    rt.size = {340.0f, 50.0f};
    titulo.Add<UiText>().text = "Cena de referência";
    Entity& botao = scene.CreateEntity("Botão", painelId);
    UiRect& rb = botao.Add<UiRect>();
    rb.anchor = UiAnchor::Bottom;
    rb.position = {0.0f, -14.0f};
    rb.size = {200.0f, 50.0f};
    botao.Add<UiImage>();
    botao.Add<UiButton>();
    Entity& rotulo = scene.CreateEntity("Rótulo", botao.id);
    rotulo.Add<UiRect>().anchor = UiAnchor::Stretch;
    rotulo.Get<UiRect>()->size = {0.0f, 0.0f};
    UiText& texto = rotulo.Add<UiText>();
    texto.text = "Botão";
    texto.fontSize = 28.0f;
}

} // namespace bench
