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

    static std::unique_ptr<Shader> FromFiles(const std::filesystem::path& vertexPath,
                                             const std::filesystem::path& fragmentPath);

    void Bind() const;

    void SetInt(std::string_view name, int value);
    void SetFloat(std::string_view name, float value);
    void SetVec2(std::string_view name, const glm::vec2& value);
    void SetVec3(std::string_view name, const glm::vec3& value);
    void SetVec4(std::string_view name, const glm::vec4& value);
    void SetMat3(std::string_view name, const glm::mat3& value);
    void SetMat4(std::string_view name, const glm::mat4& value);

    unsigned int Id() const { return m_program; }

private:
    int Location(std::string_view name);

    unsigned int m_program = 0;
    std::string m_name;
    std::unordered_map<std::string, int> m_locations;
};

} // namespace motor
