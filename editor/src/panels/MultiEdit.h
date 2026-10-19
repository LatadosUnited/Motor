#pragma once

#include <motor/Motor.h>

namespace editor {

// Inspector com vários selecionados: ele desenha a entidade principal e, depois, o que mudou nela (comparada
// com a cópia `before`, tirada antes do desenho) é repetido em `target`:
// - ativo; posição, rotação (em graus) e escala só nos eixos que mudaram, como na Unity;
// - campos de componente que mudaram, nos componentes do mesmo tipo (quem não tem o componente fica igual);
// - componente adicionado ou removido na principal: adicionado (cópia) ou removido nos outros.
// O nome não é repetido. O vínculo com prefab e os componentes de script desconhecidos ficam de fora.
void PropagateEdits(const motor::Entity& before, const motor::Entity& after, motor::Entity& target,
                    motor::AssetLibrary& assets);

} // namespace editor
