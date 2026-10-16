// Guia de scripts (docs/scripts.md), "Campos no Inspector": um campo de cada tipo suportado e as opções de
// FieldOptions. No OnStart, aplica cor, malha e textura no Mesh Renderer da própria entidade.

#include <motor/scene/ScriptModule.h>

#include <memory>
#include <string>

struct ExemploCampos : motor::Script {
    bool ligado = true;
    int vidas = 3;
    float velocidade = 2.5f;
    float volume = 0.8f;
    glm::vec2 tamanho{1.0f, 1.0f};
    glm::vec3 direcao{0.0f, 0.0f, -1.0f};
    glm::vec4 cor{1.0f, 0.8f, 0.2f, 1.0f};
    std::string alvo = "Jogador";
    std::shared_ptr<motor::Mesh> malha;         // vazio = nenhuma escolhida
    std::shared_ptr<motor::Texture2D> textura;  // vazio = nenhuma escolhida

    void OnStart() override {
        if (!ligado) return;
        // Componentes da engine são acessados pela entidade: Get<T>() devolve nullptr se não tiver.
        if (auto* renderer = GetEntity().Get<motor::MeshRenderer>()) {
            renderer->material.color = cor;
            if (malha) renderer->mesh = malha;
            if (textura) renderer->material.albedo = textura;
        }
        Log("vidas = {}, alvo = {}", vidas, alvo);
    }
};

MOTOR_SCRIPT(ExemploCampos) {
    registry.Register<ExemploCampos>("ExemploCampos", "Exemplo de campos")
        .Field("ligado", "Ligado", &ExemploCampos::ligado)
        .Field("vidas", "Vidas", &ExemploCampos::vidas, {.slider = true, .min = 0.0f, .max = 9.0f})
        .Field("velocidade", "Velocidade", &ExemploCampos::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 50.0f, .format = "%.1f m/s", .tooltip = "Metros por segundo"})
        .Field("volume", "Volume", &ExemploCampos::volume,
               {.slider = true, .logarithmic = true, .min = 0.01f, .max = 1.0f})
        .Field("tamanho", "Tamanho", &ExemploCampos::tamanho)
        .Field("direcao", "Direção", &ExemploCampos::direcao)
        .Field("cor", "Cor", &ExemploCampos::cor, {.color = true})
        .Field("alvo", "Alvo", &ExemploCampos::alvo, {.tooltip = "Nome de uma entidade"})
        .Field("malha", "Malha", &ExemploCampos::malha)
        .Field("textura", "Textura", &ExemploCampos::textura);
}
