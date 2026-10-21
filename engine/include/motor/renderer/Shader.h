#pragma once

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace motor {

class Shader {
public:
    // Lança std::runtime_error se a compilação/linkagem falhar.
    Shader(std::string_view vertexSource, std::string_view fragmentSource, std::string debugName = "shader");
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Aceita `#include "arquivo.glsl"` (relativo ao shader). `defines` ("#define X\n...") entra logo depois
    // do `#version`: o mesmo par de arquivos vira variantes (ex.: com e sem pele).
    static std::unique_ptr<Shader> FromFiles(const std::filesystem::path& vertexPath,
                                             const std::filesystem::path& fragmentPath, std::string_view defines = {});

    void Bind() const;

    void SetInt(std::string_view name, int value);
    void SetFloat(std::string_view name, float value);
    void SetVec2(std::string_view name, const glm::vec2& value);
    void SetVec3(std::string_view name, const glm::vec3& value);
    void SetVec4(std::string_view name, const glm::vec4& value);
    void SetMat3(std::string_view name, const glm::mat3& value);
    void SetMat4(std::string_view name, const glm::mat4& value);

    // Posição de um uniform: quem desenha muitas vezes por quadro guarda a posição uma vez e usa os Set* abaixo,
    // sem buscar pelo nome a cada desenho. -1 se o uniform não existir (os Set* com -1 não fazem nada).
    int Uniform(std::string_view name);

    void SetInt(int location, int value);
    void SetFloat(int location, float value);
    void SetVec2(int location, const glm::vec2& value);
    void SetVec3(int location, const glm::vec3& value);
    void SetVec4(int location, const glm::vec4& value);
    void SetMat3(int location, const glm::mat3& value);
    void SetMat4(int location, const glm::mat4& value);

    unsigned int Id() const { return m_program; }

private:

    unsigned int m_program = 0;
    std::string m_name;
    std::unordered_map<std::string, int> m_locations;
};

} // namespace motor
