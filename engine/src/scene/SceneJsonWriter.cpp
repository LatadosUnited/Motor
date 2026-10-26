// JsonWriter (SceneJson.h): o texto da cena escrito direto, no formato de CompactNumberArrays(j.dump(2)).

#include "SceneJson.h"

#include <charconv>
#include <cmath>

namespace motor::SceneSerializer::detail {

void JsonWriter::Indent() {
    m_out += '\n';
    m_out.append(m_stack.size() * 2, ' ');
}

void JsonWriter::BeforeValue() {
    if (m_afterKey) {
        m_afterKey = false;
        return;
    }
    if (m_stack.empty()) return;  // o valor de fora
    Level& level = m_stack.back();
    if (!level.empty) m_out += ',';
    level.empty = false;
    Indent();
}

void JsonWriter::BeginObject() {
    BeforeValue();
    m_out += '{';
    m_stack.push_back({true, true});
}

void JsonWriter::EndObject() {
    const bool empty = m_stack.back().empty;
    m_stack.pop_back();
    if (!empty) Indent();
    m_out += '}';
}

void JsonWriter::BeginArray() {
    BeforeValue();
    m_out += '[';
    m_stack.push_back({false, true});
}

void JsonWriter::EndArray() {
    const bool empty = m_stack.back().empty;
    m_stack.pop_back();
    if (!empty) Indent();
    m_out += ']';
}

void JsonWriter::Key(std::string_view key) {
    Level& level = m_stack.back();
    if (!level.empty) m_out += ',';
    level.empty = false;
    Indent();
    AppendString(key);
    m_out += ": ";
    m_afterKey = true;
}

void JsonWriter::Null() {
    BeforeValue();
    m_out += "null";
}

void JsonWriter::Bool(bool value) {
    BeforeValue();
    m_out += value ? "true" : "false";
}

void JsonWriter::Int(int64_t value) {
    BeforeValue();
    char text[24];
    m_out.append(text, std::to_chars(text, text + sizeof(text), value).ptr);
}

void JsonWriter::UInt(uint64_t value) {
    BeforeValue();
    char text[24];
    m_out.append(text, std::to_chars(text, text + sizeof(text), value).ptr);
}

namespace {
// O número como o serializer do nlohmann o escreve (o mesmo to_chars, com ".0" nos inteiros).
void AppendNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char text[64];
    out.append(text, nlohmann::detail::to_chars(text, text + sizeof(text), value));
}
} // namespace

void JsonWriter::Number(double value) {
    BeforeValue();
    AppendNumber(m_out, value);
}

void JsonWriter::String(std::string_view value) {
    BeforeValue();
    AppendString(value);
}

void JsonWriter::AppendString(std::string_view value) {
    // Só ASCII visível sem aspas nem barra: vai como está. O resto (acentos, controle) pelo nlohmann, que confere o
    // UTF-8 e troca bytes inválidos (error_handler_t::replace, como no dump da cena).
    bool plain = true;
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') {
            plain = false;
            break;
        }
    }
    if (plain) {
        m_out += '"';
        m_out += value;
        m_out += '"';
    } else {
        m_out += json(std::string(value)).dump(-1, ' ', false, json::error_handler_t::replace);
    }
}

void JsonWriter::Numbers(const double* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) {  // o null não é número: o CompactNumberArrays deixava a lista aberta
            BeginArray();
            for (size_t j = 0; j < count; ++j) Number(values[j]);
            EndArray();
            return;
        }
    }
    BeforeValue();
    m_out += '[';
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) m_out += ", ";
        AppendNumber(m_out, values[i]);
    }
    m_out += ']';
}

bool JsonWriter::RawSimple(std::string_view text) {
    bool simple = text == "null" || text == "true" || text == "false";
    if (!simple && text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        simple = true;
        for (const char ch : text.substr(1, text.size() - 2)) {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') {
                simple = false;
                break;
            }
        }
    }
    if (!simple && !text.empty() && text.size() <= 18) {
        const std::string_view digits = text.front() == '-' ? text.substr(1) : text;
        simple = !digits.empty() && digits.find_first_not_of("0123456789") == std::string_view::npos &&
                 (digits.size() == 1 || digits.front() != '0');
    }
    if (!simple) return false;
    BeforeValue();
    m_out += text;
    return true;
}

void JsonWriter::Value(const json& value) {
    switch (value.type()) {
        case json::value_t::null: Null(); return;
        case json::value_t::boolean: Bool(value.get<bool>()); return;
        case json::value_t::number_integer: Int(value.get<int64_t>()); return;
        case json::value_t::number_unsigned: UInt(value.get<uint64_t>()); return;
        case json::value_t::number_float: Number(value.get<double>()); return;
        case json::value_t::string: String(value.get_ref<const std::string&>()); return;
        case json::value_t::object:
            BeginObject();
            for (auto it = value.begin(); it != value.end(); ++it) {
                Key(it.key());
                Value(it.value());
            }
            EndObject();
            return;
        case json::value_t::array: {
            // Lista só de números (finitos: o não finito sai null) numa linha, como o CompactNumberArrays.
            bool numbers = !value.empty();
            for (const json& item : value) {
                if (!item.is_number() || (item.is_number_float() && !std::isfinite(item.get<double>()))) {
                    numbers = false;
                    break;
                }
            }
            if (numbers) {
                BeforeValue();
                m_out += '[';
                bool first = true;
                for (const json& item : value) {
                    if (!first) m_out += ", ";
                    first = false;
                    m_out += item.dump();
                }
                m_out += ']';
                return;
            }
            BeginArray();
            for (const json& item : value) Value(item);
            EndArray();
            return;
        }
        default:  // binário, descartado: o que o dump faria
            BeforeValue();
            m_out += value.dump(-1, ' ', false, json::error_handler_t::replace);
            return;
    }
}

} // namespace motor::SceneSerializer::detail
