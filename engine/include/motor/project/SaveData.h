#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace motor {

// Progresso salvo do jogo (Fase 21): um "slot" é um arquivo <nome>.json de chave/valor na pasta de saves (no jogo
// exportado %APPDATA%\<Jogo>\saves\; no Play do editor <projeto>/.motor/saves/). Os scripts usam pelo
// Script::SaveSlot:
//
//   motor::SaveData& save = SaveSlot();         // "progresso"
//   save.SetInt("fase", 3);
//   save.SetVec3("posicao", GetTransform().position);
//   save.Save();                                // grava agora (também é gravado ao fechar o jogo / parar o Play)
//   int fase = SaveSlot().GetInt("fase", 1);    // 1 se ainda não tem
class SaveData {
public:
    using Value = std::variant<int64_t, double, bool, std::string, glm::vec3>;

    SaveData() = default;
    explicit SaveData(std::filesystem::path file) : m_file(std::move(file)) {}

    bool Has(std::string_view key) const;
    // Valor da chave, ou `fallback` se não existe ou é de outro tipo (int e float se convertem entre si).
    int GetInt(std::string_view key, int fallback = 0) const;
    float GetFloat(std::string_view key, float fallback = 0.0f) const;
    bool GetBool(std::string_view key, bool fallback = false) const;
    std::string GetString(std::string_view key, std::string_view fallback = {}) const;
    glm::vec3 GetVec3(std::string_view key, const glm::vec3& fallback = glm::vec3(0.0f)) const;

    void SetInt(std::string_view key, int value) { Set(key, static_cast<int64_t>(value)); }
    void SetFloat(std::string_view key, float value) { Set(key, static_cast<double>(value)); }
    void SetBool(std::string_view key, bool value) { Set(key, value); }
    void SetString(std::string_view key, std::string_view value) { Set(key, std::string(value)); }
    void SetVec3(std::string_view key, const glm::vec3& value) { Set(key, value); }
    void Remove(std::string_view key);
    void Clear();
    std::vector<std::string> Keys() const;  // em ordem alfabética
    bool Empty() const { return m_values.empty(); }

    // Grava no arquivo (cria a pasta; troca o arquivo inteiro de uma vez). false com `error` se não deu.
    bool Save(std::string* error = nullptr);
    // Lê do arquivo; sem arquivo = vazio e true. Arquivo estragado: vazio, false e uma cópia ".corrompido".
    bool Load(std::string* error = nullptr);
    bool IsDirty() const { return m_dirty; }  // mudou desde o último Save/Load
    const std::filesystem::path& File() const { return m_file; }

    std::string ToJson() const;
    bool FromJson(std::string_view text, std::string* error = nullptr);

private:
    void Set(std::string_view key, Value value);
    const Value* Find(std::string_view key) const;

    std::filesystem::path m_file;
    std::map<std::string, Value, std::less<>> m_values;
    bool m_dirty = false;
};

// Os slots de um jogo numa pasta: carregados na primeira vez que são pedidos e guardados até o jogo fechar.
class SaveStore {
public:
    static constexpr const char* kDefaultSlot = "progresso";

    SaveStore() = default;  // sem pasta: slots só na memória (Save falha)
    explicit SaveStore(std::filesystem::path folder) : m_folder(std::move(folder)) {}

    const std::filesystem::path& Folder() const { return m_folder; }
    // Slot pelo nome (caracteres que não servem em arquivo viram '_'; vazio = "progresso").
    SaveData& Slot(std::string_view name = kDefaultSlot);
    bool Exists(std::string_view name = kDefaultSlot) const;  // tem arquivo, ou valores ainda não gravados
    bool Delete(std::string_view name = kDefaultSlot);        // apaga o arquivo e esvazia o slot
    std::vector<std::string> List() const;                    // slots com arquivo, em ordem alfabética
    // Grava os slots que mudaram (ao fechar o jogo / parar o Play). false se algum falhou.
    bool SaveAll(std::string* error = nullptr);
    void Forget() { m_slots.clear(); }  // esquece o que foi carregado (relê do disco no próximo Slot)

    static std::string SlotFileName(std::string_view name);  // "progresso" -> "progresso.json"

private:
    std::filesystem::path m_folder;
    std::map<std::string, std::unique_ptr<SaveData>, std::less<>> m_slots;
};

} // namespace motor
