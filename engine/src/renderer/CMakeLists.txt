# OpenGL: loader próprio (GL.h, privado), shaders, malhas, texturas, câmera, framebuffer e renderer
# (Fase 7: sombras, PBR, céu e pós-processamento; ShadowMath e Pbr são só CPU, testados no ctest).
motor_add_layer(renderer
    SOURCES
        GL.cpp
        Shader.cpp
        Mesh.cpp
        Texture.cpp
        Camera.cpp
        Renderer.cpp
        DrawList.cpp
        DrawOrder.cpp
        Framebuffer.cpp
        Renderer2D.cpp
        Skeleton.cpp
        ShadowMath.cpp
        Pbr.cpp
        PostProcess.cpp
        Shadows.cpp
        Sky.cpp
        GpuTimers.cpp
        GpuMemory.cpp
        SpatialGrid.cpp
        RenderWorld.cpp
        GpuAllocators.cpp
        MeshPool.cpp
        StreamBuffer.cpp
    DEPENDS core
    PRIVATE_LIBS glfw)
