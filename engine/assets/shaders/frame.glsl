// Bloco "Frame" (std140), igual ao FrameData do Renderer.cpp: muda um, muda o outro.
// Fase 28: as posições chegam relativas à câmera (objetos, luzes, sombras); uView é só a rotação e uCameraPosition, 0.
layout(std140, binding = 0) uniform Frame {
    mat4 uView;
    mat4 uProjection;
    vec4 uCameraPosition;
    vec4 uAmbient;            // rgb = luz ambiente extra (linear)
    vec4 uFog;                // rgb = cor da neblina (linear), a = densidade
    mat4 uInvViewProjection;  // clip -> mundo (fundo do céu)
    vec4 uSky;                // x = brilho do céu ao fundo, y = brilho dos reflexos, z = rotação (rad), w = mip máximo do reflexo
    vec4 uSh[9];              // irradiância do céu (harmônicos esféricos, já com rotação e brilho)
    vec4 uViewport;           // xy = tamanho em pixels, zw = 1 / tamanho
};
