#include "motor/assets/TerrainData.h"

#include "motor/common/FileRead.h"
#include "motor/common/FileWrite.h"
#include "motor/common/Text.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/renderer/Mesh.h"

#include <nlohmann/json.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>

namespace motor {

namespace fs = std::filesystem;

namespace {
constexpr char kBlockMagic[4] = {'M', 'T', 'B', 'L'};
constexpr uint32_t kBlockVersion = 1;

// Divisão arredondada para baixo (negativos: -1 / 32 = -1, não 0).
int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

template <class T> void Put(std::string& out, T value) {
    char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.append(bytes, sizeof(T));
}

template <class T> bool Take(std::string_view& in, T& value) {
    if (in.size() < sizeof(T)) return false;
    std::memcpy(&value, in.data(), sizeof(T));
    in.remove_prefix(sizeof(T));
    return true;
}
} // namespace

TerrainData::TerrainData(float blockSize, int resolution)
    : m_blockSize(std::max(blockSize, 0.01f)), m_resolution(std::clamp(resolution, 2, 1025)) {}

bool TerrainData::AddBlock(TerrainBlockKey key, float height) {
    if (m_blocks.contains(key)) return false;
    Block& block = m_blocks[key];
    block.heights.assign(static_cast<size_t>(m_resolution) * m_resolution, height);
    // Os vizinhos refazem a malha: as normais da borda passam a ver este bloco.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (Block* n = Find({key.x + dx, key.z + dz})) ++n->version;
        }
    }
    ++m_version;
    return true;
}

std::vector<TerrainBlockKey> TerrainData::Blocks() const {
    std::vector<TerrainBlockKey> keys;
    keys.reserve(m_blocks.size());
    for (const auto& [key, block] : m_blocks) keys.push_back(key);
    std::sort(keys.begin(), keys.end(), [](TerrainBlockKey a, TerrainBlockKey b) {
        return a.z != b.z ? a.z < b.z : a.x < b.x;
    });
    return keys;
}

TerrainData::Block* TerrainData::Find(TerrainBlockKey key) {
    auto it = m_blocks.find(key);
    return it == m_blocks.end() ? nullptr : &it->second;
}

const TerrainData::Block* TerrainData::Find(TerrainBlockKey key) const {
    auto it = m_blocks.find(key);
    return it == m_blocks.end() ? nullptr : &it->second;
}

const std::vector<float>* TerrainData::BlockHeights(TerrainBlockKey key) const {
    const Block* block = Find(key);
    return block ? &block->heights : nullptr;
}

std::optional<float> TerrainData::Sample(int gx, int gz) const {
    const int n = m_resolution - 1;
    const int bx = FloorDiv(gx, n), bz = FloorDiv(gz, n);
    const int i = gx - bx * n, j = gz - bz * n;
    // Na borda (i ou j = 0) a amostra também é do bloco de antes, na última linha dele.
    for (int ox = 0; ox <= (i == 0 ? 1 : 0); ++ox) {
        for (int oz = 0; oz <= (j == 0 ? 1 : 0); ++oz) {
            if (const Block* block = Find({bx - ox, bz - oz})) {
                return block->heights[static_cast<size_t>((oz ? n : j) * m_resolution + (ox ? n : i))];
            }
        }
    }
    return std::nullopt;
}

void TerrainData::SetSample(int gx, int gz, float height) {
    const int n = m_resolution - 1;
    const int bx = FloorDiv(gx, n), bz = FloorDiv(gz, n);
    const int i = gx - bx * n, j = gz - bz * n;
    bool changed = false;
    for (int ox = 0; ox <= (i == 0 ? 1 : 0); ++ox) {
        for (int oz = 0; oz <= (j == 0 ? 1 : 0); ++oz) {
            Block* block = Find({bx - ox, bz - oz});
            if (!block) continue;
            float& h = block->heights[static_cast<size_t>((oz ? n : j) * m_resolution + (ox ? n : i))];
            if (h == height) continue;
            h = height;
            block->dirty = true;
            changed = true;
        }
    }
    if (changed) Touch(gx, gz);
}

void TerrainData::Touch(int gx, int gz) {
    // A amostra entra na normal das 8 em volta: todo bloco que tem alguma delas refaz a malha.
    const int n = m_resolution - 1;
    const int bx0 = FloorDiv(gx - 1, n), bx1 = FloorDiv(gx + 1, n);
    const int bz0 = FloorDiv(gz - 1, n), bz1 = FloorDiv(gz + 1, n);
    for (int bz = bz0 - 1; bz <= bz1; ++bz) {
        for (int bx = bx0 - 1; bx <= bx1; ++bx) {
            if (Block* block = Find({bx, bz})) ++block->version;
        }
    }
    ++m_version;
}

std::optional<float> TerrainData::HeightAt(float x, float z) const {
    const float spacing = Spacing();
    const float fx = x / spacing, fz = z / spacing;
    const int gx = static_cast<int>(std::floor(fx)), gz = static_cast<int>(std::floor(fz));
    const float tx = fx - static_cast<float>(gx), tz = fz - static_cast<float>(gz);
    const auto h00 = Sample(gx, gz), h10 = Sample(gx + 1, gz), h01 = Sample(gx, gz + 1), h11 = Sample(gx + 1, gz + 1);
    if (!h00 || !h10 || !h01 || !h11) return std::nullopt;
    // Os mesmos dois triângulos da malha (diagonal de (0, 1) a (1, 0)): a altura bate com o que aparece na tela.
    if (tx + tz <= 1.0f) return *h00 + tx * (*h10 - *h00) + tz * (*h01 - *h00);
    return *h11 + (1.0f - tx) * (*h01 - *h11) + (1.0f - tz) * (*h10 - *h11);
}

glm::vec3 TerrainData::NormalAt(int gx, int gz) const {
    const float h = Sample(gx, gz).value_or(0.0f);
    // Na borda do terreno a amostra que falta é a própria: a diferença usa só um lado (metade do declive).
    const float left = Sample(gx - 1, gz).value_or(h), right = Sample(gx + 1, gz).value_or(h);
    const float back = Sample(gx, gz - 1).value_or(h), front = Sample(gx, gz + 1).value_or(h);
    return glm::normalize(glm::vec3(left - right, 2.0f * Spacing(), back - front));
}

std::pair<float, float> TerrainData::BlockHeightRange(TerrainBlockKey key) const {
    const Block* block = Find(key);
    if (!block || block->heights.empty()) return {0.0f, 0.0f};
    const auto [lo, hi] = std::minmax_element(block->heights.begin(), block->heights.end());
    return {*lo, *hi};
}

std::shared_ptr<Mesh> TerrainData::BlockMesh(TerrainBlockKey key) const {
    const Block* block = Find(key);
    if (!block) return nullptr;
    if (block->mesh && block->meshVersion == block->version) return block->mesh;
    const int res = m_resolution, n = res - 1;
    const float spacing = Spacing();
    std::vector<Vertex> vertices(static_cast<size_t>(res) * res);
    for (int j = 0; j < res; ++j) {
        for (int i = 0; i < res; ++i) {
            const float h = block->heights[static_cast<size_t>(j * res + i)];
            const int gx = key.x * n + i, gz = key.z * n + j;
            Vertex& v = vertices[static_cast<size_t>(j * res + i)];
            v.position = {static_cast<float>(i) * spacing, h, static_cast<float>(j) * spacing};
            v.normal = NormalAt(gx, gz);
            v.uv = {static_cast<float>(gx) * spacing, static_cast<float>(gz) * spacing};  // metros: repete por metro
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(n) * n * 6);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const uint32_t v00 = static_cast<uint32_t>(j * res + i), v10 = v00 + 1;
            const uint32_t v01 = v00 + static_cast<uint32_t>(res), v11 = v01 + 1;
            indices.insert(indices.end(), {v00, v01, v10, v10, v01, v11});  // anti-horário visto de cima
        }
    }
    block->mesh = std::make_shared<Mesh>(vertices, indices);
    block->meshVersion = block->version;
    return block->mesh;
}

bool TerrainData::IsDirty() const {
    return std::any_of(m_blocks.begin(), m_blocks.end(), [](const auto& b) { return b.second.dirty; });
}

fs::path TerrainData::BlocksFolder(const fs::path& file) {
    return file.parent_path() / (file.stem().native() + PathFromUtf8(kTerrainBlocksFolderSuffix).native());
}

std::string TerrainData::BlockFileName(TerrainBlockKey key) {
    return std::format("{}_{}{}", key.x, key.z, kTerrainBlockExtension);
}

bool TerrainData::Save(const fs::path& file, std::string* error) {
    const bool sameFile = !m_savedFile.empty() && m_savedFile == file;
    const fs::path folder = BlocksFolder(file);
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        SetError(error, std::format("Não foi possível criar a pasta {}: {}", ToUtf8(folder), ec.message()));
        return false;
    }
    for (auto& [key, block] : m_blocks) {
        const fs::path blockFile = folder / PathFromUtf8(BlockFileName(key));
        if (sameFile && !block.dirty && fs::is_regular_file(blockFile, ec)) continue;
        std::string bytes;
        bytes.reserve(20 + block.heights.size() * sizeof(float));
        bytes.append(kBlockMagic, sizeof(kBlockMagic));
        Put(bytes, kBlockVersion);
        Put(bytes, static_cast<int32_t>(key.x));
        Put(bytes, static_cast<int32_t>(key.z));
        Put(bytes, static_cast<uint32_t>(m_resolution));
        for (float h : block.heights) Put(bytes, h);
        if (!WriteFileAtomic(blockFile, bytes, error)) return false;
    }
    nlohmann::ordered_json blocks = nlohmann::ordered_json::array();
    for (TerrainBlockKey key : Blocks()) blocks.push_back({key.x, key.z});
    const nlohmann::ordered_json header = {
        {"format", "motor-terrain"},
        {"version", kFormatVersion},
        {"blockSize", m_blockSize},
        {"resolution", m_resolution},
        {"blocks", std::move(blocks)},
    };
    if (!WriteFileAtomic(file, header.dump(1) + "\n", error)) return false;
    for (auto& [key, block] : m_blocks) block.dirty = false;
    m_savedFile = file;
    return true;
}

std::shared_ptr<TerrainData> TerrainData::Load(const fs::path& file, std::string* error) {
    const auto text = ReadTextFile(file);
    if (!text) {
        SetError(error, std::format("Não foi possível abrir {}", ToUtf8(file)));
        return nullptr;
    }
    const auto header = nlohmann::json::parse(*text, nullptr, false);
    if (!header.is_object() || header.value("format", std::string()) != "motor-terrain") {
        SetError(error, std::format("{} não é um terreno do Motor", ToUtf8(file)));
        return nullptr;
    }
    if (header.value("version", 0) > kFormatVersion) {
        SetError(error, std::format("{} é de uma versão mais nova do Motor", ToUtf8(file)));
        return nullptr;
    }
    auto data = std::make_shared<TerrainData>(header.value("blockSize", kDefaultBlockSize),
                                              header.value("resolution", kDefaultResolution));
    const int res = data->m_resolution;
    const fs::path folder = BlocksFolder(file);
    std::vector<std::string> problems;
    if (const auto it = header.find("blocks"); it != header.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_array() || entry.size() != 2 || !entry[0].is_number_integer() || !entry[1].is_number_integer()) {
                continue;
            }
            const TerrainBlockKey key{entry[0].get<int>(), entry[1].get<int>()};
            data->AddBlock(key);
            Block& block = data->m_blocks[key];
            block.dirty = false;
            const std::string name = BlockFileName(key);
            const auto bytes = ReadTextFile(folder / PathFromUtf8(name));
            std::string_view in = bytes ? std::string_view(*bytes) : std::string_view();
            char magic[4] = {};
            uint32_t version = 0, resolution = 0;
            int32_t x = 0, z = 0;
            const bool header_ok = in.size() >= 4 && (std::memcpy(magic, in.data(), 4), in.remove_prefix(4), true) &&
                                   std::memcmp(magic, kBlockMagic, 4) == 0 && Take(in, version) && Take(in, x) &&
                                   Take(in, z) && Take(in, resolution);
            if (!bytes) {
                problems.push_back(std::format("bloco {} não encontrado", name));
            } else if (!header_ok || version > kBlockVersion || x != key.x || z != key.z ||
                       resolution != static_cast<uint32_t>(res) || in.size() < block.heights.size() * sizeof(float)) {
                problems.push_back(std::format("bloco {} ilegível", name));
            } else {
                std::memcpy(block.heights.data(), in.data(), block.heights.size() * sizeof(float));
                continue;
            }
            block.dirty = true;  // plano: gravar de novo quando salvar
        }
    }
    data->m_savedFile = file;
    data->m_version = 1;
    if (!problems.empty()) {
        std::string message = std::format("Terreno {}: ", ToUtf8(file.filename()));
        for (size_t i = 0; i < problems.size() && i < 5; ++i) message += (i ? "; " : "") + problems[i];
        if (problems.size() > 5) message += std::format(" (e mais {})", problems.size() - 5);
        message += ". Ficaram planos.";
        log::Warn("{}", message);
        SetError(error, std::move(message));
    }
    return data;
}

std::shared_ptr<TerrainData> TerrainData::Open(const fs::path& file, std::string* error) {
    static std::mutex mutex;
    static std::map<fs::path, std::weak_ptr<TerrainData>> open;
    const fs::path key = file.lexically_normal();
    std::lock_guard lock(mutex);
    if (auto it = open.find(key); it != open.end()) {
        if (auto data = it->second.lock()) return data;
    }
    auto data = Load(file, error);
    if (data) open[key] = data;
    return data;
}

} // namespace motor
