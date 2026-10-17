#pragma once

namespace motor {

class AssetLibrary;

// Malhas e texturas embutidas que as cenas citam pelo nome ("Cubo", "Esfera", "Plano", "Xadrez", "Xadrez
// grosso"). O editor e o Player (jogo exportado) registram as mesmas: uma cena feita no editor abre igual no
// jogo. Exige o contexto OpenGL (as texturas vão para a GPU).
void AddBuiltinAssets(AssetLibrary& assets);

} // namespace motor
