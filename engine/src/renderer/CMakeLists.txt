# OpenGL: loader próprio (GL.h, privado), shaders, malhas, texturas, câmera, framebuffer e renderer.
motor_add_layer(renderer
    SOURCES
        GL.cpp
        Shader.cpp
        Mesh.cpp
        Texture.cpp
        Camera.cpp
        Renderer.cpp
        Framebuffer.cpp
        Skeleton.cpp
    DEPENDS core
    PRIVATE_LIBS glfw)
