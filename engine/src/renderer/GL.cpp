#include "renderer/GL.h"

#include "motor/core/Log.h"

namespace motor::glfn {
#define MOTOR_GL_DEFINE(type, name) type name = nullptr;
MOTOR_GL_FUNCTIONS(MOTOR_GL_DEFINE)
#undef MOTOR_GL_DEFINE
} // namespace motor::glfn

namespace motor::gl {

bool Load(ProcLoader loader) {
    int missing = 0;

#define MOTOR_GL_LOAD(type, name)                                   \
    glfn::name = reinterpret_cast<type>(loader(#name));             \
    if (!glfn::name) {                                              \
        log::Error("OpenGL: função ausente: {}", #name);            \
        ++missing;                                                  \
    }
    MOTOR_GL_FUNCTIONS(MOTOR_GL_LOAD)
#undef MOTOR_GL_LOAD

    return missing == 0;
}

} // namespace motor::gl
