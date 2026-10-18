// Chuva de objetos: a cada intervalo cria uma "gota" num lugar sorteado acima da entidade, e cada gota some
// sozinha depois de alguns segundos. De vez em quando (sorteio) cai uma gota grande.
// Mostra: Every, After com o id da entidade, Random, RandomInt e Chance.
// Guia: docs/scripts.md, seção 10 (Temporizadores, mensagens e sorteio).

#include <motor/scene/ScriptModule.h>

struct Chuva : motor::Script {
    float intervalo = 0.25f;   // segundos entre uma gota e outra
    float raio = 4.0f;         // espalha as gotas neste raio
    float vida = 1.5f;         // segundos até a gota sumir
    float chanceGrande = 0.1f; // 0 a 1

    int criadas = 0;  // estado do jogo

    void OnStart() override {
        Every(intervalo, [this] { CriarGota(); });
    }

    void CriarGota() {
        const glm::vec3 centro = GetTransform().position;
        motor::Entity& gota = CreateEntity("Gota");
        gota.transform.position = centro + glm::vec3(Random(-raio, raio), 0.0f, Random(-raio, raio));
        const float tamanho = Chance(chanceGrande) ? 0.6f : 0.1f * static_cast<float>(RandomInt(1, 3));
        gota.transform.scale = glm::vec3(tamanho);
        ++criadas;

        // Guarde o id (não o ponteiro): o temporizador roda depois, e a gota pode já ter sido destruída.
        const motor::EntityId id = gota.id;
        After(vida, [this, id] { Destroy(id); });
    }
};

MOTOR_FIELDS(Chuva, intervalo, raio, vida, chanceGrande)
