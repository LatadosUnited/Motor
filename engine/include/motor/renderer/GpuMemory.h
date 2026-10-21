#pragma once

// Memória de vídeo por tipo de recurso (Fase 22). O renderer registra cada objeto OpenGL que guarda dados (textura,
// buffer, renderbuffer) ao criar e tira do registro ao apagar; os contadores por quadro (FrameStats) mostram o total de
// cada tipo. É uma estimativa: o driver alinha e acrescenta margens, então vale para comparar e achar o que pesa, não
// para o byte exato.

#include <cstdint>

namespace motor {

enum class GpuMemoryKind {
    Textures,      // texturas dos materiais e da UI
    Meshes,        // vértices e índices das malhas
    Framebuffers,  // alvos do editor (viewport, vista do jogo)
    PostProcess,   // cena em HDR (com MSAA), bloom e imagem final
    Shadows,       // mapas de sombra (cascatas e cubos)
    Sky,           // cubos do céu e dos reflexos, tabela da BRDF, imagem HDR do céu
    Buffers,       // blocos de dados do renderer e o buffer da UI
};
inline constexpr int kGpuMemoryKinds = 7;
const char* GpuMemoryKindName(GpuMemoryKind kind);  // "texturas", "malhas"... (sem acento: vira coluna de CSV)

enum class GpuObject { Texture, Buffer, Renderbuffer };  // os nomes do OpenGL são separados por tipo de objeto

// Tamanho de uma textura ou renderbuffer: `internalFormat` é o GLenum (GL_RGBA8...), `layers` = 6 num cubo, `levels`
// = níveis de mip (cada um com metade do lado), `samples` = MSAA.
uint64_t GpuTextureBytes(unsigned internalFormat, int width, int height, int layers = 1, int levels = 1, int samples = 1);

// Registra (ou troca, se o nome já estava registrado) e tira do registro. Nome 0 é ignorado.
void TrackGpuMemory(GpuObject object, unsigned name, GpuMemoryKind kind, uint64_t bytes);
void UntrackGpuMemory(GpuObject object, unsigned name);

struct GpuMemoryUsage {
    uint64_t bytes[kGpuMemoryKinds] = {};
    uint32_t objects[kGpuMemoryKinds] = {};
    uint64_t Total() const;
};
GpuMemoryUsage CurrentGpuMemory();

} // namespace motor
