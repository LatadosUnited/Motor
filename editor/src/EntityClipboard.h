#pragma once

#include <motor/Motor.h>

#include <cstdint>
#include <vector>

namespace editor {

// Área de transferência de entidades do editor (Copiar, Recortar, Colar). Guarda cópias das sub-árvores numa
// cena própria, com as raízes já na posição do mundo: colar em outra cena, ou sob outro pai, mantém o lugar
// onde estavam. Fica na memória do editor (vale entre cenas do mesmo projeto, não entre editores abertos).
class EntityClipboard {
public:
    // Copia `roots` (e os filhos) de `scene`. `sceneTag` identifica a cena de origem: referências a entidades
    // de fora da cópia só continuam valendo ao colar na mesma cena.
    void Copy(const motor::Scene& scene, const std::vector<motor::EntityId>& roots, uint64_t sceneTag);
    // Cola em `scene` (sob `parent`, ou na raiz), mantendo a posição no mundo. Devolve as raízes coladas.
    std::vector<motor::EntityId> Paste(motor::Scene& scene, motor::EntityId parent, uint64_t sceneTag) const;

    bool Empty() const { return m_roots.empty(); }
    size_t Count() const { return m_roots.size(); }
    void Clear();

private:
    motor::Scene m_scene;
    std::vector<motor::EntityId> m_roots;
    // Ids das entidades copiadas na cena de origem, para saber se uma referência aponta para dentro da cópia.
    std::vector<std::pair<motor::EntityId, motor::EntityId>> m_originals;  // (origem, na m_scene)
    uint64_t m_sceneTag = 0;
};

// Copia a sub-árvore `id` de `from` para `to`, sob `parent` (transform local igual). `copies` recebe os pares
// (id em `from`, id em `to`). Não ajusta as referências entre entidades: quem chama decide (ver RemapEntityRefs).
motor::EntityId CopySubtree(const motor::Scene& from, motor::EntityId id, motor::Scene& to, motor::EntityId parent,
                            std::vector<std::pair<motor::EntityId, motor::EntityId>>& copies);

} // namespace editor
