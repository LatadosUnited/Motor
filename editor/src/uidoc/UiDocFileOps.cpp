#include "uidoc/UiDocFileOps.h"

#include <motor/common/Text.h>

#include <algorithm>
#include <format>

namespace editor {

using namespace motor;
using Json = UiJsonArgs;

namespace {

bool Fail(std::string* error, std::string message) {
    SetError(error, std::move(message));
    return false;
}

Json DeclarationsJson(const UiStyleDeclarations& declarations) {
    Json out = Json::object();
    for (const UiStyleDeclaration& d : declarations.items) {
        out[UiStylePropertyName(d.property)] = Json::parse(UiStyleValueToJson(d.property, d.value), nullptr, false);
    }
    return out;
}

// Declarações de `set` numa lista (regra) ou num elemento (pela função `apply`).
bool SetDeclarations(const Json& set, UiStyleDeclarations& declarations, std::string* error) {
    if (!set.is_object()) return Fail(error, "set/style: objeto {propriedade: valor}");
    for (const auto& [prop, value] : set.items()) {
        if (FindUiStyleProperty(prop) < 0) return Fail(error, "propriedade de estilo desconhecida '" + prop + "'");
        if (value.is_null()) declarations.Remove(prop);
        else if (!declarations.Set(prop, value.dump())) return Fail(error, std::format("valor inválido para '{}': {}", prop, value.dump()));
    }
    return true;
}

UiFont FileArg(const std::string& path) {
    UiFont ref;
    ref.path = path;
    return ref;
}

int RuleIndex(const UiStyleSheet& sheet, const Json& value, std::string* error) {
    if (value.is_number_integer()) {
        const int i = value.get<int>();
        if (i >= 0 && i < static_cast<int>(sheet.rules.size())) return i;
    } else if (value.is_string()) {
        for (size_t i = 0; i < sheet.rules.size(); ++i) {
            if (sheet.rules[i].selector == value.get<std::string>()) return static_cast<int>(i);
        }
    }
    Fail(error, std::format("rule: índice (0 a {}) ou o seletor de uma regra da folha", static_cast<int>(sheet.rules.size()) - 1));
    return -1;
}

} // namespace

bool UiPathFromJson(const std::vector<UiElement>& roots, const Json& value, UiPath& out, std::string* error) {
    out.clear();
    if (value.is_string()) {
        if (FindUiElementPath(roots, value.get<std::string>(), out)) return true;
        return Fail(error, std::format("nenhum elemento com o nome '{}'", value.get<std::string>()));
    }
    if (value.is_array()) {
        for (const Json& v : value) {
            if (!v.is_number_integer()) return Fail(error, "caminho deve ser [i, j...] de inteiros");
            out.push_back(v.get<int>());
        }
        if (UiElementAt(roots, out)) return true;
        return Fail(error, "nenhum elemento no caminho " + value.dump());
    }
    return Fail(error, "use o nome do elemento ou o caminho [i, j...]");
}

bool UiElementFieldsFromJson(UiElement& e, const Json& args, bool* assets, std::string* error) {
    auto str = [&](const char* key, std::string& field) -> bool {
        if (const auto it = args.find(key); it != args.end()) {
            if (!it->is_string()) return Fail(error, std::format("{}: texto", key));
            field = it->get<std::string>();
        }
        return true;
    };
    auto num = [&](const char* key, float& field) -> bool {
        if (const auto it = args.find(key); it != args.end()) {
            if (!it->is_number()) return Fail(error, std::format("{}: número", key));
            field = it->get<float>();
        }
        return true;
    };
    auto flag = [&](const char* key, bool& field) -> bool {
        if (const auto it = args.find(key); it != args.end()) {
            if (!it->is_boolean()) return Fail(error, std::format("{}: true ou false", key));
            field = it->get<bool>();
        }
        return true;
    };
    if (!str("name", e.name) || !str("text", e.text) || !str("placeholder", e.placeholder) || !str("follow", e.follow) ||
        !flag("visible", e.visible) || !flag("interactable", e.interactable) || !flag("checked", e.checked) ||
        !num("min", e.minValue) || !num("max", e.maxValue) || !num("step", e.step) || !num("value", e.value)) {
        return false;
    }
    if (const auto it = args.find("maxLength"); it != args.end()) {
        if (!it->is_number_integer() || it->get<int>() < 0) return Fail(error, "maxLength: inteiro >= 0");
        e.maxLength = it->get<int>();
    }
    if (const auto it = args.find("followOffset"); it != args.end()) {
        if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) {
            return Fail(error, "followOffset: [x, y, z] em metros");
        }
        e.followOffset = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    }
    if (const auto it = args.find("classes"); it != args.end()) {
        if (it->is_string()) {
            e.classes = ParseUiClassList(it->get<std::string>());
        } else if (it->is_array()) {
            std::string joined;
            for (const Json& c : *it) joined += (c.is_string() ? c.get<std::string>() : std::string()) + " ";
            e.classes = ParseUiClassList(joined);
        } else {
            return Fail(error, "classes: lista ou texto");
        }
    }
    if (const auto it = args.find("image"); it != args.end()) {
        if (!it->is_string()) return Fail(error, "image: \"Assets/...\" ou \"\"");
        e.image = it->get<std::string>().empty() ? UiFont{} : FileArg(it->get<std::string>());
        if (assets) *assets = true;
    }
    if (const auto it = args.find("style"); it != args.end()) {
        if (!it->is_object()) return Fail(error, "style: objeto {propriedade: valor}");
        for (const auto& [prop, value] : it->items()) {
            if (FindUiStyleProperty(prop) < 0) return Fail(error, "style: propriedade desconhecida '" + prop + "'");
            if (value.is_null()) {
                e.ClearStyle(prop);
            } else if (!e.SetStyle(prop, value.dump())) {
                return Fail(error, std::format("style: valor inválido para '{}': {}", prop, value.dump()));
            }
            if (assets && prop == "font") *assets = true;
        }
    }
    if (args.contains("value") || args.contains("min") || args.contains("max") || args.contains("step")) {
        const float v = e.value;
        e.value = std::min(e.minValue, e.maxValue) - 1.0f;  // fora do intervalo: o SetValue prende e arredonda sempre
        e.SetValue(v);
    }
    return true;
}

Json UiElementSummaryJson(const UiElement& e, UiPath& path) {
    Json p = Json::array();
    for (int i : path) p.push_back(i);
    Json j{{"path", std::move(p)}, {"type", UiElementTypeName(e.type)}, {"name", e.name}};
    if (!e.classes.empty()) j["classes"] = e.classes;
    if (!e.DisplayText().empty() || e.type == UiElementType::TextField) j["text"] = e.text;
    if (e.type == UiElementType::ProgressBar || e.type == UiElementType::Slider) {
        j["value"] = e.value;
        j["min"] = e.minValue;
        j["max"] = e.maxValue;
        if (e.type == UiElementType::Slider) j["step"] = e.step;
    }
    if (e.type == UiElementType::Toggle) j["checked"] = e.checked;
    if (e.type == UiElementType::TextField) {
        j["placeholder"] = e.placeholder;
        if (e.maxLength > 0) j["maxLength"] = e.maxLength;
    }
    if (!e.follow.empty()) {
        j["follow"] = e.follow;
        j["followOffset"] = {e.followOffset.x, e.followOffset.y, e.followOffset.z};
    }
    if (!e.visible) j["visible"] = false;
    if (!e.interactable) j["interactable"] = false;
    if (!e.image.IsDefault()) j["image"] = e.image.path.empty() ? e.image.guid : e.image.path;
    if (!e.inlineStyle.Empty()) j["style"] = DeclarationsJson(e.inlineStyle);
    if (!e.children.empty()) {
        Json children = Json::array();
        for (size_t i = 0; i < e.children.size(); ++i) {
            path.push_back(static_cast<int>(i));
            children.push_back(UiElementSummaryJson(e.children[i], path));
            path.pop_back();
        }
        j["children"] = std::move(children);
    }
    return j;
}

bool ApplyUiDocumentOps(UiDocumentData& doc, const Json& ops, std::string* error) {
    if (!ops.is_array()) return Fail(error, "ops: lista de operações");
    auto& roots = doc.elements;
    int n = 0;
    for (const Json& op : ops) {
        ++n;
        const std::string where = std::format("ops[{}]", n - 1);
        if (!op.is_object()) return Fail(error, where + ": objeto {\"action\": ...}");
        const std::string action = op.value("action", std::string());
        std::string e;
        auto path = [&](const char* key, bool allowRoot, UiPath& out) -> bool {
            const auto it = op.find(key);
            if (it == op.end() || it->is_null() || (it->is_string() && it->get<std::string>().empty()) ||
                (it->is_array() && it->empty())) {
                out.clear();
                if (allowRoot) return true;
                return Fail(error, std::format("{}: informe '{}' (nome ou caminho)", where, key));
            }
            if (!UiPathFromJson(roots, *it, out, &e)) return Fail(error, std::format("{}: {}: {}", where, key, e));
            return true;
        };
        const int index = op.value("index", -1);
        if (action == "add") {
            UiElementType type;
            if (!ParseUiElementType(op.value("type", std::string()), type)) {
                return Fail(error, where + ": type: Panel, Text, Image, Button, ProgressBar, Slider, Toggle ou TextField");
            }
            UiPath parent;
            if (!path("parent", true, parent)) return false;
            UiElement element = NewPaletteElement(type, roots);
            if (!UiElementFieldsFromJson(element, op, nullptr, &e)) return Fail(error, where + ": " + e);
            // O nome pedido, sem repetir um que já existe (vira "jogar2"; a resposta mostra o nome final).
            if (op.contains("name")) element.name = UniqueUiElementName(roots, element.name);
            if (InsertUiElement(roots, parent, index, std::move(element)).empty()) return Fail(error, where + ": parent inválido");
        } else if (action == "set") {
            UiPath at;
            if (!path("element", false, at)) return false;
            if (!UiElementFieldsFromJson(*UiElementAt(roots, at), op, nullptr, &e)) return Fail(error, where + ": " + e);
        } else if (action == "move") {
            UiPath at, parent;
            if (!path("element", false, at) || !path("parent", true, parent)) return false;
            if (MoveUiElement(roots, at, parent, index).empty()) {
                return Fail(error, where + ": não dá para mover um elemento para dentro dele mesmo");
            }
        } else if (action == "delete") {
            UiPath at;
            if (!path("element", false, at)) return false;
            RemoveUiElement(roots, at);
        } else if (action == "duplicate") {
            UiPath at;
            if (!path("element", false, at)) return false;
            DuplicateUiElement(roots, at);
        } else if (action == "add_sheet" || action == "remove_sheet") {
            const std::string sheet = op.value("path", std::string());
            if (sheet.empty()) return Fail(error, where + ": path da folha (\"Assets/UI/Tema.mstyle\")");
            const auto same = [&](const UiFont& ref) { return ref.path == sheet; };
            if (action == "add_sheet") {
                if (std::none_of(doc.styles.begin(), doc.styles.end(), same)) doc.styles.push_back(FileArg(sheet));
            } else {
                std::erase_if(doc.styles, same);
            }
            doc.styleDirty = true;
        } else {
            return Fail(error, where + ": action: add, set, move, delete, duplicate, add_sheet ou remove_sheet");
        }
    }
    doc.styled = true;
    ComputeUiStyles(doc);
    return true;
}

bool ApplyUiStyleOps(UiStyleSheet& sheet, const Json& ops, std::string* error) {
    if (!ops.is_array()) return Fail(error, "ops: lista de operações");
    int n = 0;
    for (const Json& op : ops) {
        const std::string where = std::format("ops[{}]", n++);
        if (!op.is_object()) return Fail(error, where + ": objeto {\"action\": ...}");
        const std::string action = op.value("action", std::string());
        std::string e;
        if (action == "add_rule") {
            UiStyleRule rule;
            if (!SetUiRuleSelector(rule, op.value("selector", std::string("*")), &e)) return Fail(error, where + ": selector: " + e);
            if (const auto it = op.find("set"); it != op.end() && !SetDeclarations(*it, rule.declarations, &e)) {
                return Fail(error, where + ": " + e);
            }
            sheet.rules.push_back(std::move(rule));
        } else if (action == "set_rule" || action == "delete_rule") {
            const int ri = RuleIndex(sheet, op.value("rule", Json()), &e);
            if (ri < 0) return Fail(error, where + ": " + e);
            if (action == "delete_rule") {
                sheet.rules.erase(sheet.rules.begin() + ri);
                continue;
            }
            UiStyleRule& rule = sheet.rules[static_cast<size_t>(ri)];
            if (op.contains("selector") && !SetUiRuleSelector(rule, op.value("selector", std::string()), &e)) {
                return Fail(error, where + ": selector: " + e);
            }
            if (const auto it = op.find("set"); it != op.end() && !SetDeclarations(*it, rule.declarations, &e)) {
                return Fail(error, where + ": " + e);
            }
        } else {
            return Fail(error, where + ": action: add_rule, set_rule ou delete_rule");
        }
    }
    return true;
}

} // namespace editor
