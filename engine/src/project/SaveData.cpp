#include "motor/project/SaveData.h"

#include "motor/common/FileWrite.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <type_traits>

namespace motor {

using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {
constexpr const char* kFormatName = "motor-save";
constexpr int kFormatVersion = 1;
constexpr const char* kExtension = ".json";

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}
} // namespace

// --- SaveData ----------------------------------------------------------------------------------------

const SaveData::Value* SaveData::Find(std::string_view key) const {
    const auto it = m_values.find(key);
    return it == m_values.end() ? nullptr : &it->second;
}

bool SaveData::Has(std::string_view key) const {
    return Find(key) != nullptr;
}

int SaveData::GetInt(std::string_view key, int fallback) const {
    const Value* v = Find(key);
    if (!v) return fallback;
    if (const auto* i = std::get_if<int64_t>(v)) return static_cast<int>(*i);
    if (const auto* d = std::get_if<double>(v)) return static_cast<int>(*d);
    return fallback;
}

float SaveData::GetFloat(std::string_view key, float fallback) const {
    const Value* v = Find(key);
    if (!v) return fallback;
    if (const auto* d = std::get_if<double>(v)) return static_cast<float>(*d);
    if (const auto* i = std::get_if<int64_t>(v)) return static_cast<float>(*i);
    return fallback;
}

bool SaveData::GetBool(std::string_view key, bool fallback) const {
    const Value* v = Find(key);
    const auto* b = v ? std::get_if<bool>(v) : nullptr;
    return b ? *b : fallback;
}

std::string SaveData::GetString(std::string_view key, std::string_view fallback) const {
    const Value* v = Find(key);
    const auto* s = v ? std::get_if<std::string>(v) : nullptr;
    return s ? *s : std::string(fallback);
}

glm::vec3 SaveData::GetVec3(std::string_view key, const glm::vec3& fallback) const {
    const Value* v = Find(key);
    const auto* p = v ? std::get_if<glm::vec3>(v) : nullptr;
    return p ? *p : fallback;
}

void SaveData::Set(std::string_view key, Value value) {
    if (key.empty()) return;
    const auto it = m_values.find(key);
    if (it != m_values.end()) {
        if (it->second == value) return;
        it->second = std::move(value);
    } else {
        m_values.emplace(std::string(key), std::move(value));
    }
    m_dirty = true;
}

void SaveData::Remove(std::string_view key) {
    const auto it = m_values.find(key);
    if (it == m_values.end()) return;
    m_values.erase(it);
    m_dirty = true;
}

void SaveData::Clear() {
    if (m_values.empty()) return;
    m_values.clear();
    m_dirty = true;
}

std::vector<std::string> SaveData::Keys() const {
    std::vector<std::string> keys;
    for (const auto& [key, value] : m_values) keys.push_back(key);
    return keys;
}

std::string SaveData::ToJson() const {
    json values = json::object();
    for (const auto& [key, value] : m_values) {
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, glm::vec3>) {
                    values[key] = json::array({v.x, v.y, v.z});
                } else {
                    values[key] = v;
                }
            },
            value);
    }
    json root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;
    root["values"] = std::move(values);
    return root.dump(2, ' ', false, json::error_handler_t::replace) + '\n';
}

bool SaveData::FromJson(std::string_view text, std::string* error) {
    std::map<std::string, Value, std::less<>> values;
    try {
        const json root = json::parse(text);
        if (!root.is_object() || root.value("format", std::string()) != kFormatName) {
            SetError(error, "não é um save do Motor");
            return false;
        }
        if (root.value("version", 0) > kFormatVersion) {
            SetError(error, "save de uma versão mais nova do jogo");
            return false;
        }
        const auto list = root.find("values");
        if (list != root.end() && list->is_object()) {
            for (const auto& [key, v] : list->items()) {
                if (v.is_boolean()) values[key] = v.get<bool>();
                else if (v.is_number_integer()) values[key] = v.get<int64_t>();
                else if (v.is_number()) values[key] = v.get<double>();
                else if (v.is_string()) values[key] = v.get<std::string>();
                else if (v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number()) {
                    values[key] = glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
                }
                // Outros tipos (de uma versão futura): ignorados.
            }
        }
    } catch (const json::exception& e) {
        SetError(error, std::format("save inválido: {}", e.what()));
        return false;
    }
    m_values = std::move(values);
    m_dirty = false;
    return true;
}

bool SaveData::Save(std::string* error) {
    if (m_file.empty()) {
        SetError(error, "o save não tem arquivo (jogo sem pasta de dados)");
        return false;
    }
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);
    if (!WriteFileAtomic(m_file, ToJson(), error)) return false;
    m_dirty = false;
    return true;
}

bool SaveData::Load(std::string* error) {
    m_values.clear();
    m_dirty = false;
    std::error_code ec;
    if (m_file.empty() || !fs::exists(m_file, ec)) return true;
    std::ifstream in(m_file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string problem;
    if (!in || !FromJson(ss.str(), &problem)) {
        // Guarda o arquivo estragado ao lado antes que um Save o substitua.
        fs::path copy = m_file;
        copy += ".corrompido";
        fs::copy_file(m_file, copy, fs::copy_options::overwrite_existing, ec);
        SetError(error, std::format("'{}': {} (cópia em {})", ToUtf8(m_file.filename()), problem.empty() ? "não deu para ler" : problem,
                                    ToUtf8(copy.filename())));
        return false;
    }
    return true;
}

// --- SaveStore ---------------------------------------------------------------------------------------

std::string SaveStore::SlotFileName(std::string_view name) {
    std::string file;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        file += (u < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) ? '_' : c;
    }
    while (!file.empty() && (file.back() == ' ' || file.back() == '.')) file.pop_back();
    if (file.empty()) file = kDefaultSlot;
    return file + kExtension;
}

SaveData& SaveStore::Slot(std::string_view name) {
    const std::string file = SlotFileName(name);
    if (const auto it = m_slots.find(file); it != m_slots.end()) return *it->second;
    auto slot = std::make_unique<SaveData>(m_folder.empty() ? fs::path() : m_folder / PathFromUtf8(file));
    std::string error;
    if (!slot->Load(&error)) log::Error("Save: {}", error);
    return *m_slots.emplace(file, std::move(slot)).first->second;
}

bool SaveStore::Exists(std::string_view name) const {
    const std::string file = SlotFileName(name);
    if (const auto it = m_slots.find(file); it != m_slots.end() && !it->second->Empty()) return true;
    std::error_code ec;
    return !m_folder.empty() && fs::is_regular_file(m_folder / PathFromUtf8(file), ec);
}

bool SaveStore::Delete(std::string_view name) {
    const std::string file = SlotFileName(name);
    std::error_code ec;
    if (!m_folder.empty()) fs::remove(m_folder / PathFromUtf8(file), ec);
    // O slot fica no mapa, vazio (Load sem arquivo): um script pode guardar a referência dele.
    if (const auto it = m_slots.find(file); it != m_slots.end()) it->second->Load();
    return !ec;
}

std::vector<std::string> SaveStore::List() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (auto it = fs::directory_iterator(m_folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == kExtension) names.push_back(ToUtf8(it->path().stem()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool SaveStore::SaveAll(std::string* error) {
    bool ok = true;
    for (auto& [file, slot] : m_slots) {
        if (!slot->IsDirty()) continue;
        std::string problem;
        if (!slot->Save(&problem)) {
            ok = false;
            SetError(error, problem);
            log::Error("Save: não foi possível gravar '{}': {}", file, problem);
        }
    }
    return ok;
}

} // namespace motor
