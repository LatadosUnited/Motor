#pragma once

#include <motor/Motor.h>

#include <string>
#include <string_view>
#include <vector>

namespace editor {

// Busca da Hierarquia. Palavras separadas por espaço, todas precisam bater (sem diferença de maiúsculas nem de
// acentos): uma palavra comum procura no nome; "t:" procura no tipo de componente, pelo nome no Inspector ou no
// código ("t:luz", "t:Collider", "t:Girar" para um script). Ex.: "porta t:collider".
struct HierarchyFilter {
    std::vector<std::string> names;  // já dobradas (FoldForSearch)
    std::vector<std::string> types;

    static HierarchyFilter Parse(std::string_view text);
    bool Empty() const { return names.empty() && types.empty(); }
    bool Matches(const motor::Entity& entity) const;
};

// Texto para comparar: minúsculas e sem os acentos do português (UTF-8), "Ação" -> "acao".
std::string FoldForSearch(std::string_view text);

// Entidades que batem, na ordem da Hierarquia (pai antes dos filhos, irmãos na ordem da cena).
std::vector<motor::EntityId> SearchHierarchy(const motor::Scene& scene, const HierarchyFilter& filter);

// "Pai / Filho": o caminho até a entidade, sem ela (vazio na raiz). Mostrado ao lado do resultado.
std::string ParentPath(const motor::Scene& scene, motor::EntityId id);

} // namespace editor
