#include "motor/renderer/Shader.h"

#include "motor/core/Log.h"
#include "renderer/GL.h"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace motor {

namespace {
std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::format("Não foi possível abrir '{}'", path.string()));
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint Compile(GLenum stage, std::string_view source, const std::string& name) {
    const GLuint shader = glCreateShader(stage);
    const GLchar* src = source.data();
    const GLint len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src, &len);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string info(static_cast<size_t>(logLen), '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, info.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::format("Erro ao compilar {} ({}):\n{}",
                                             name, stage == GL_VERTEX_SHADER ? "vertex" : "fragment", info));
    }
    return shader;
}
} // namespace

Shader::Shader(std::string_view vertexSource, std::string_view fragmentSource, std::string debugName)
    : m_name(std::move(debugName)) {
    const GLuint vs = Compile(GL_VERTEX_SHADER, vertexSource, m_name);
    GLuint fs = 0;
    try {
        fs = Compile(GL_FRAGMENT_SHADER, fragmentSource, m_name);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDetachShader(m_program, vs);
    glDetachShader(m_program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLen);
        std::string info(static_cast<size_t>(logLen), '\0');
        glGetProgramInfoLog(m_program, logLen, nullptr, info.data());
        glDeleteProgram(m_program);
        m_program = 0;
        throw std::runtime_error(std::format("Erro ao linkar {}:\n{}", m_name, info));
    }
}

Shader::~Shader() {
    if (m_program) glDeleteProgram(m_program);
}

namespace {
// Troca as linhas `#include "arquivo"` pelo conteúdo do arquivo (relativo ao shader, recursivo) e põe as
// definições logo depois do `#version` (que tem de ser a primeira linha).
std::string Preprocess(const std::filesystem::path& path, std::string_view defines, int depth = 0) {
    if (depth > 8) throw std::runtime_error(std::format("Shader '{}': #include aninhado demais", path.string()));
    const std::string text = ReadTextFile(path);
    std::string out;
    size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string_view line(text.data() + pos, end - pos);
        if (line.starts_with("#include \"")) {
            const size_t close = line.find('"', 10);
            if (close == std::string_view::npos) throw std::runtime_error(std::format("Shader '{}': #include sem aspas", path.string()));
            out += Preprocess(path.parent_path() / std::string(line.substr(10, close - 10)), {}, depth + 1);
        } else {
            out.append(line);
            out += '\n';
        }
        if (first && depth == 0) out.append(defines);
        first = false;
        pos = end + 1;
    }
    return out;
}
} // namespace

std::unique_ptr<Shader> Shader::FromFiles(const std::filesystem::path& vertexPath,
                                          const std::filesystem::path& fragmentPath, std::string_view defines) {
    std::string name = vertexPath.stem().string() + "+" + fragmentPath.stem().string();
    return std::make_unique<Shader>(Preprocess(vertexPath, defines), Preprocess(fragmentPath, defines), std::move(name));
}

void Shader::Bind() const {
    glUseProgram(m_program);
}

int Shader::Location(std::string_view name) {
    auto it = m_locations.find(std::string(name));
    if (it != m_locations.end()) return it->second;

    const std::string key(name);
    const int loc = glGetUniformLocation(m_program, key.c_str());
    if (loc < 0) log::Warn("Shader '{}': uniform '{}' não encontrado (ou otimizado)", m_name, key);
    m_locations.emplace(key, loc);
    return loc;
}

void Shader::SetInt(std::string_view name, int value) {
    glProgramUniform1i(m_program, Location(name), value);
}

void Shader::SetFloat(std::string_view name, float value) {
    glProgramUniform1f(m_program, Location(name), value);
}

void Shader::SetVec2(std::string_view name, const glm::vec2& value) {
    glProgramUniform2fv(m_program, Location(name), 1, glm::value_ptr(value));
}

void Shader::SetVec3(std::string_view name, const glm::vec3& value) {
    glProgramUniform3fv(m_program, Location(name), 1, glm::value_ptr(value));
}

void Shader::SetVec4(std::string_view name, const glm::vec4& value) {
    glProgramUniform4fv(m_program, Location(name), 1, glm::value_ptr(value));
}

void Shader::SetMat3(std::string_view name, const glm::mat3& value) {
    glProgramUniformMatrix3fv(m_program, Location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::SetMat4(std::string_view name, const glm::mat4& value) {
    glProgramUniformMatrix4fv(m_program, Location(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace motor
