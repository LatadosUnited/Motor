#include "motor/renderer/GpuMemory.h"

#include "renderer/GL.h"

#include <algorithm>
#include <map>
#include <utility>

namespace motor {

namespace {
struct Allocation {
    GpuMemoryKind kind = GpuMemoryKind::Textures;
    uint64_t bytes = 0;
};

// Só a thread do OpenGL mexe aqui. Poucas centenas de objetos: um map basta (criar e apagar são raros).
struct Registry {
    std::map<std::pair<GpuObject, unsigned>, Allocation> objects;
    GpuMemoryUsage usage;
};

Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

uint64_t BytesPerPixel(unsigned format) {
    switch (format) {
        case GL_RGBA16F: return 8;
        case GL_RGB16F: return 8;  // os drivers guardam RGB16F como RGBA16F
        case GL_RGBA32F: return 16;
        case GL_RG16F: return 4;
        case GL_R11F_G11F_B10F: return 4;
        case GL_RGBA8:
        case GL_SRGB8_ALPHA8: return 4;
        case GL_DEPTH_COMPONENT16: return 2;
        case GL_DEPTH_COMPONENT24: return 4;  // alinhado a 32 bits
        case GL_DEPTH_COMPONENT32F: return 4;
        case GL_DEPTH24_STENCIL8: return 4;
        case GL_R8: return 1;
        default: return 4;
    }
}
} // namespace

const char* GpuMemoryKindName(GpuMemoryKind kind) {
    switch (kind) {
        case GpuMemoryKind::Textures: return "texturas";
        case GpuMemoryKind::Meshes: return "malhas";
        case GpuMemoryKind::Framebuffers: return "framebuffers";
        case GpuMemoryKind::PostProcess: return "pos-processamento";
        case GpuMemoryKind::Shadows: return "sombras";
        case GpuMemoryKind::Sky: return "ceu";
        case GpuMemoryKind::Buffers: return "buffers";
    }
    return "?";
}

uint64_t GpuTextureBytes(unsigned internalFormat, int width, int height, int layers, int levels, int samples) {
    uint64_t pixels = 0;
    int w = std::max(width, 1), h = std::max(height, 1);
    for (int level = 0; level < std::max(levels, 1); ++level) {
        pixels += static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
    }
    return pixels * static_cast<uint64_t>(std::max(layers, 1)) * static_cast<uint64_t>(std::max(samples, 1)) *
           BytesPerPixel(internalFormat);
}

void TrackGpuMemory(GpuObject object, unsigned name, GpuMemoryKind kind, uint64_t bytes) {
    if (name == 0) return;
    UntrackGpuMemory(object, name);
    Registry& r = GetRegistry();
    r.objects[{object, name}] = {kind, bytes};
    const auto k = static_cast<size_t>(kind);
    r.usage.bytes[k] += bytes;
    ++r.usage.objects[k];
}

void UntrackGpuMemory(GpuObject object, unsigned name) {
    if (name == 0) return;
    Registry& r = GetRegistry();
    const auto it = r.objects.find({object, name});
    if (it == r.objects.end()) return;
    const auto k = static_cast<size_t>(it->second.kind);
    r.usage.bytes[k] -= it->second.bytes;
    --r.usage.objects[k];
    r.objects.erase(it);
}

uint64_t GpuMemoryUsage::Total() const {
    uint64_t total = 0;
    for (uint64_t b : bytes) total += b;
    return total;
}

GpuMemoryUsage CurrentGpuMemory() {
    return GetRegistry().usage;
}

} // namespace motor
