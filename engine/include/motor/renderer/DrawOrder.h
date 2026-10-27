#pragma once

// Ordem de desenho (Fase 23): o renderer dá a cada item do quadro um número de material (valores iguais = o mesmo
// número, mesmo vindo de objetos diferentes) e ordena por (shader, material, malha), para que os itens que podem ir
// no mesmo draw instanciado fiquem seguidos. Sem OpenGL: testado no ctest (test_render).

#include "motor/renderer/Material.h"
#include "motor/renderer/Pbr.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace motor {

// O que o lit.frag recebe de um material, mais o que muda o estado do desenho (duas faces).
struct MaterialKey {
    MaterialParams params;
    bool doubleSided = false;
    bool operator==(const MaterialKey&) const = default;
};

// O que a sombra de um item usa do material: recorte (com a textura) e alfa.
struct ShadowParams {
    float cutoff = -1.0f;               // < 0: sem recorte; > 1: recortado inteiro (recorte sem textura e alfa baixo)
    float alpha = 1.0f;
    const Texture2D* albedo = nullptr;  // só no recorte com textura
    // Terreno (Fase 28): nas cascatas, só as faces de costas para o sol. Num mapa de altura são elas que fazem a
    // sombra dos morros (a encosta virada para o sol não se sombreia: N.L já a ilumina), e o mapa quase não é
    // preenchido: o chão inteiro em 4 mapas de 2048 custava ~2,8 ms na HD 530.
    bool backFacesOnly = false;
    bool operator==(const ShadowParams&) const = default;
};
ShadowParams ShadowParamsOf(const Material& material);

struct MaterialKeyHash {
    size_t operator()(const MaterialKey& key) const;
};
struct ShadowParamsHash {
    size_t operator()(const ShadowParams& params) const;
};

// Números pequenos e seguidos (0, 1, 2...) para chaves iguais, na ordem em que aparecem. Clear() a cada quadro.
// `hash` (o de Hash) pode vir pronto: o SubmitMany o calcula em paralelo.
template <class Key, class Hash>
class KeyIds {
public:
    uint32_t Id(const Key& key) { return Id(key, Hash{}(key)); }
    uint32_t Id(const Key& key, size_t hash) {
        const auto [it, inserted] = m_ids.try_emplace(Hashed{key, hash}, static_cast<uint32_t>(m_keys.size()));
        if (inserted) m_keys.push_back(key);
        return it->second;
    }
    const Key& operator[](uint32_t id) const { return m_keys[id]; }
    size_t Size() const { return m_keys.size(); }
    void Clear() {
        m_ids.clear();
        m_keys.clear();
    }

private:
    struct Hashed {
        Key key;
        size_t hash;
        bool operator==(const Hashed& other) const { return hash == other.hash && key == other.key; }
    };
    struct ByHash {
        size_t operator()(const Hashed& h) const { return h.hash; }
    };
    std::unordered_map<Hashed, uint32_t, ByHash> m_ids;
    std::vector<Key> m_keys;
};

using MaterialIds = KeyIds<MaterialKey, MaterialKeyHash>;
using ShadowParamsIds = KeyIds<ShadowParams, ShadowParamsHash>;

// Chave de ordenação de um item: shader (com pele ou não), material (um número de KeyIds) e malha.
struct DrawKey {
    uint32_t shader = 0;
    uint32_t material = 0;
    const void* mesh = nullptr;

    bool operator==(const DrawKey&) const = default;
};

// `order` recebe 0..keys.size()-1 ordenados por (shader, material, malha); chaves iguais ficam na ordem original.
void SortByKey(std::span<const DrawKey> keys, std::vector<uint32_t>& order);

} // namespace motor
