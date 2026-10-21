#pragma once

// Cena de referência da alpha 3 (Fase 22): ~5000 entidades para medir o Motor sempre do mesmo jeito.
//
//   - chão e 4000 objetos estáticos (cubos e esferas em grade), 500 deles com collider;
//   - 900 caixas com corpo rígido em colunas (caem um pouco, empilham e dormem);
//   - 50 cubos com o script "Bobber" (sobe e desce; o mesmo do bench/motor/Scripts);
//   - 1 luz direcional com sombra (qualidade Média) e 8 pontuais sem sombra;
//   - câmera principal com AudioListener, 10 fontes de som (paradas, sem arquivo) e uma interface pequena (painel,
//     texto e botão).
//
// Uma fonte só: o test_desempenho monta a cena por aqui (sem janela) e, com --gerar, grava o .mscene no projeto do
// bench para o Player e o editor medirem a mesma cena com GPU.

#include <cstddef>

namespace motor {
class AssetLibrary;
class Scene;
} // namespace motor

namespace bench {

// `assets` precisa ter as malhas embutidas "Cubo" e "Esfera". O script "Bobber" só entra se o tipo estiver
// registrado (no teste, uma cópia; no projeto, o da DLL de scripts).
void MontarCenaReferencia(motor::Scene& scene, motor::AssetLibrary& assets);

inline constexpr size_t kEntidadesReferencia = 5000;  // aproximado: o teste confere a faixa

} // namespace bench
