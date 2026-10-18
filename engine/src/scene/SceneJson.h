#pragma once

// Peças internas do SceneSerializer (SceneSerializer*.cpp, SceneRecovery.cpp): números e vetores em JSON,
// leitores tolerantes e componentes <-> JSON pela descrição dos campos. Não é API pública.

#include "motor/scene/SceneSerializer.h"

#include "motor/scene/Component.h"
#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace motor::SceneSerializer::detail {

// ordered_json mantém a ordem de inserção das chaves ("format" e "version" no topo do arquivo).
using json = nlohmann::ordered_json;

// float -> double passando pela representação decimal mais curta, para o arquivo ter "0.6" e não
// "0.6000000238418579".
double Num(float value);
json Vec(const glm::vec3& v);
json Vec(const glm::vec4& v);

// Leitores tolerantes: campo ausente ou com formato errado mantém o valor padrão.
template <int N>
void ReadFloats(const json& obj, const char* key, float* out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_array() || it->size() != N) return;
    for (int i = 0; i < N; ++i) out[i] = (*it)[i].get<float>();
}

inline void Read(const json& obj, const char* key, glm::vec3& v) { ReadFloats<3>(obj, key, &v.x); }

inline void Read(const json& obj, const char* key, glm::quat& q) {
    float xyzw[4] = {q.x, q.y, q.z, q.w};
    ReadFloats<4>(obj, key, xyzw);
    q = glm::quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
    // Só normaliza se precisar: renormalizar um quaternion que já é unitário muda o último bit e a cena
    // deixaria de ser igual depois de salvar e abrir.
    if (std::abs(glm::length(q) - 1.0f) > 1e-5f) q = glm::normalize(q);
}

template <class T>
void Read(const json& obj, const char* key, T& value) {
    value = obj.value(key, value);
}

// O dump indentado do nlohmann põe cada número numa linha. Junta arrays só de números em uma linha:
// [1.0, 2.0, 3.0]. Ignora o conteúdo de strings (nomes podem ter colchetes).
std::string CompactNumberArrays(const std::string& in);

void SetError(std::string* error, std::string message);

// Componentes pela descrição dos campos (Component.h). Tipo desconhecido grava de volta o JSON lido.
json ComponentToJson(const ComponentPtr& component, const AssetLibrary& assets, const JsonOptions& options,
                     const std::string& owner);
void ComponentFromJson(const ComponentType& type, const json& obj, Entity& e, AssetLibrary& assets);
// Campos ausentes no arquivo ficam com o valor padrão do componente.
void ReadFields(const ComponentType& type, void* data, const json& obj, AssetLibrary& assets, const std::string& owner);
// Depois de ReadFields: o que do JSON guardado não entrou no componente (campo que sumiu ou mudou de tipo).
void CheckStoredFields(const ComponentType& type, void* data, const json& obj, AssetLibrary& assets,
                       const std::string& owner, std::vector<FieldWarning>& warnings);
// Componente "desconhecido" que guarda o JSON como veio (o tipo volta quando o script for carregado).
ComponentPtr MakeMissing(std::string typeName, const json& obj);

} // namespace motor::SceneSerializer::detail
