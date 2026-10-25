// Arquivo .mnav: o navmesh gerado no editor, lido no começo do Play (e no jogo exportado).
//
//   "MNAV"                      4 bytes
//   versão                      uint32 (kVersion)
//   n, n parâmetros             uint32 e n float (NavMeshBuildSettings, na ordem de kSettingFields)
//   impressão digital           uint64 (NavMesh::SourceHash da entrada)
//   blocos                      uint32
//   para cada bloco: tamanho    int32 e os bytes do bloco do Detour (que tem a sua própria marca e versão)
//
// Tudo em little-endian. Ler é tolerante nos parâmetros: os que faltam ficam no padrão, os a mais são pulados.

#include "motor/ai/NavMesh.h"

#include "ai/NavMeshDetour.h"
#include "motor/common/FileWrite.h"
#include "motor/core/Paths.h"

#include <DetourAlloc.h>

#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <span>

namespace motor {

namespace {

constexpr char kMagic[4] = {'M', 'N', 'A', 'V'};
constexpr uint32_t kVersion = 1;

constexpr float NavMeshBuildSettings::*kSettingFields[] = {
    &NavMeshBuildSettings::agentRadius,   &NavMeshBuildSettings::agentHeight,
    &NavMeshBuildSettings::agentMaxClimb, &NavMeshBuildSettings::agentMaxSlope,
    &NavMeshBuildSettings::cellSize,      &NavMeshBuildSettings::cellHeight,
    &NavMeshBuildSettings::minRegionArea, &NavMeshBuildSettings::maxEdgeLength,
    &NavMeshBuildSettings::maxEdgeError,  &NavMeshBuildSettings::detailSampleDistance,
    &NavMeshBuildSettings::detailMaxError,
};

template <class T> void Put(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

// Leitura com fim conferido: qualquer leitura além do fim deixa `ok` falso.
struct Reader {
    std::span<const char> data;
    size_t pos = 0;
    bool ok = true;

    template <class T> T Get() {
        T value{};
        if (pos + sizeof(T) > data.size()) {
            ok = false;
            return value;
        }
        std::memcpy(&value, data.data() + pos, sizeof(T));
        pos += sizeof(T);
        return value;
    }
    const char* Take(size_t size) {
        if (pos + size > data.size()) {
            ok = false;
            return nullptr;
        }
        const char* p = data.data() + pos;
        pos += size;
        return p;
    }
};

// FNV-1a de 64 bits.
void Mix(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

bool Fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

uint64_t NavMesh::SourceHash(const NavGeometry& geometry, const NavMeshBuildSettings& settings) {
    uint64_t hash = 14695981039346656037ull;
    Mix(hash, geometry.vertices.data(), geometry.vertices.size() * sizeof(glm::vec3));
    Mix(hash, geometry.indices.data(), geometry.indices.size() * sizeof(uint32_t));
    for (auto field : kSettingFields) Mix(hash, &(settings.*field), sizeof(float));
    // Só com ligações: navmesh sem elas guarda a mesma impressão digital de antes da Fase 35.
    for (const NavLink& link : geometry.links) {
        const float values[7] = {link.start.x, link.start.y, link.start.z, link.end.x, link.end.y, link.end.z, link.radius};
        Mix(hash, values, sizeof(values));
        const unsigned char both = link.bidirectional ? 1 : 0;
        Mix(hash, &both, 1);
    }
    return hash;
}

bool NavMesh::Save(const std::filesystem::path& file, std::string* error) const {
    std::string out(kMagic, sizeof(kMagic));
    Put(out, kVersion);
    Put(out, static_cast<uint32_t>(std::size(kSettingFields)));
    for (auto field : kSettingFields) Put(out, m_settings.*field);
    Put(out, m_source);

    const dtNavMesh& mesh = *m_detour->mesh;
    std::vector<const dtMeshTile*> tiles;
    for (int i = 0; i < mesh.getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh.getTile(i);
        if (tile && tile->header && tile->data && tile->dataSize > 0) tiles.push_back(tile);
    }
    Put(out, static_cast<uint32_t>(tiles.size()));
    for (const dtMeshTile* tile : tiles) {
        Put(out, static_cast<int32_t>(tile->dataSize));
        out.append(reinterpret_cast<const char*>(tile->data), static_cast<size_t>(tile->dataSize));
    }
    return WriteFileAtomic(file, out, error);
}

std::unique_ptr<NavMesh> NavMesh::Load(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        Fail(error, "não foi possível abrir " + ToUtf8(file));
        return nullptr;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Reader r{std::span<const char>(bytes.data(), bytes.size())};

    const char* magic = r.Take(sizeof(kMagic));
    if (!magic || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        Fail(error, ToUtf8(file) + " não é um navmesh do Motor");
        return nullptr;
    }
    const uint32_t version = r.Get<uint32_t>();
    if (!r.ok || version == 0 || version > kVersion) {
        Fail(error, std::format("{}: versão {} do navmesh, mais nova que esta engine ({})", ToUtf8(file), version, kVersion));
        return nullptr;
    }

    std::unique_ptr<NavMesh> nav(new NavMesh());
    const uint32_t settingCount = r.Get<uint32_t>();
    for (uint32_t i = 0; i < settingCount && r.ok; ++i) {
        const float value = r.Get<float>();
        if (i < std::size(kSettingFields)) nav->m_settings.*kSettingFields[i] = value;
    }
    nav->m_source = r.Get<uint64_t>();
    const uint32_t tileCount = r.Get<uint32_t>();
    if (!r.ok || tileCount == 0) {
        Fail(error, ToUtf8(file) + ": arquivo cortado ou sem navmesh");
        return nullptr;
    }

    Detour& d = *nav->m_detour;
    d.mesh = dtAllocNavMesh();
    if (!d.mesh) {
        Fail(error, "sem memória para o navmesh");
        return nullptr;
    }
    for (uint32_t t = 0; t < tileCount; ++t) {
        const int32_t size = r.Get<int32_t>();
        const char* source = size > 0 ? r.Take(static_cast<size_t>(size)) : nullptr;
        if (!r.ok || !source) {
            Fail(error, ToUtf8(file) + ": arquivo cortado");
            return nullptr;
        }
        // O Detour fica dono da cópia (DT_TILE_FREE_DATA) e a libera com o navmesh.
        auto* data = static_cast<unsigned char*>(dtAlloc(static_cast<size_t>(size), DT_ALLOC_PERM));
        if (!data) {
            Fail(error, "sem memória para o navmesh");
            return nullptr;
        }
        std::memcpy(data, source, static_cast<size_t>(size));
        // Por enquanto um bloco só (o navmesh "solo" da Fase 34); blocos vários entram com o TileCache (Fase 36).
        if (t > 0 || dtStatusFailed(d.mesh->init(data, size, DT_TILE_FREE_DATA))) {
            dtFree(data);
            Fail(error, ToUtf8(file) + ": navmesh inválido (gerado por outra versão do Detour?)");
            return nullptr;
        }
    }
    if (!d.InitQuery()) {
        Fail(error, "falha ao iniciar as consultas do navmesh");
        return nullptr;
    }
    return nav;
}

} // namespace motor
