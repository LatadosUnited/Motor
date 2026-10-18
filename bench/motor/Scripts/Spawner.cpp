// Comparação com a Unity (bench/README.md), cena "spawn200": a cada quadro destrói os 200 cubos do quadro
// anterior e cria outros 200 em posições sorteadas. Equivalente ao Spawner.cs do projeto gêmeo da Unity.
#include <motor/scene/ScriptModule.h>

#include <cstdint>
#include <vector>

struct Spawner : motor::Script {
    int quantidade = 200;

    void OnUpdate(float) override {
        for (motor::EntityId id : m_vivos) Destroy(id);
        m_vivos.clear();
        const motor::MeshRenderer* meu = GetEntity().Get<motor::MeshRenderer>();
        if (!meu) return;
        const motor::MeshRenderer copia = *meu;  // CreateEntity pode mover as entidades: copia antes
        for (int i = 0; i < quantidade; ++i) {
            motor::Entity& e = CreateEntity("Cubo");
            e.transform.position = {Sorteio() * 80.0f - 40.0f, 0.5f, Sorteio() * 80.0f - 40.0f};
            e.Add<motor::MeshRenderer>() = copia;
            m_vivos.push_back(e.id);
        }
    }

private:
    float Sorteio() {
        m_semente = m_semente * 1664525u + 1013904223u;
        return static_cast<float>(m_semente >> 8) / 16777216.0f;
    }

    std::vector<motor::EntityId> m_vivos;
    uint32_t m_semente = 1;
};

MOTOR_SCRIPT(Spawner) {
    registry.Register<Spawner>("Spawner", "Spawner").Field("quantidade", "Quantidade", &Spawner::quantidade);
}
