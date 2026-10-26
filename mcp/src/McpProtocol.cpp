#include <motor/mcp/McpProtocol.h>

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace motor::mcp {

namespace {
// Códigos de erro do JSON-RPC 2.0.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

// Versões do protocolo que este servidor fala (a primeira é a preferida).
constexpr std::array<const char*, 3> kVersions = {Protocol::kLatestVersion, "2025-03-26", "2024-11-05"};

struct RpcError {
    int code;
    std::string message;
};

Json ErrorResponse(const Json& id, int code, std::string message) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

Json CallResult(const ToolResult& result) {
    return Json{{"content", result.content}, {"isError", result.isError}};
}

std::string Dump(const Json& message) {
    return message.dump(-1, ' ', false, Json::error_handler_t::replace);
}
} // namespace

std::optional<std::string> Deferred::Poll() const {
    ToolResult result;
    try {
        if (!check(result)) return std::nullopt;
    } catch (const std::exception& e) {
        result = ToolResult::Error(e.what());
    }
    return Finish(result);
}

std::string Deferred::Finish(const ToolResult& result) const {
    return Dump(Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", CallResult(result)}});
}

ToolResult ToolResult::Text(std::string text) {
    ToolResult r;
    r.AddText(std::move(text));
    return r;
}

ToolResult ToolResult::Error(std::string text) {
    ToolResult r = Text(std::move(text));
    r.isError = true;
    return r;
}

ToolResult ToolResult::Data(const Json& value) {
    return Text(value.dump(2, ' ', false, Json::error_handler_t::replace));
}

ToolResult& ToolResult::AddText(std::string text) {
    content.push_back({{"type", "text"}, {"text", std::move(text)}});
    return *this;
}

ToolResult& ToolResult::AddImage(std::string base64, std::string mimeType) {
    content.push_back({{"type", "image"}, {"data", std::move(base64)}, {"mimeType", std::move(mimeType)}});
    return *this;
}

Json Prop(const char* type, const char* description) {
    return Json{{"type", type}, {"description", description}};
}

Json Schema(Json properties, std::vector<std::string> required) {
    Json schema = {{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) schema["required"] = std::move(required);
    return schema;
}

Protocol::Protocol(std::string serverName, std::string serverVersion, std::string instructions)
    : m_name(std::move(serverName)), m_version(std::move(serverVersion)), m_instructions(std::move(instructions)) {}

void Protocol::AddTool(Tool tool) {
    auto it = std::find_if(m_tools.begin(), m_tools.end(), [&](const Tool& t) { return t.name == tool.name; });
    if (it != m_tools.end()) {
        *it = std::move(tool);
    } else {
        m_tools.push_back(std::move(tool));
    }
}

std::optional<Json> Protocol::Handle(const Json& message) {
    m_later = {};  // só vale o da tools/call desta mensagem
    const Json nullId = nullptr;
    if (!message.is_object() || message.value("jsonrpc", "") != "2.0" || !message.contains("method") ||
        !message["method"].is_string()) {
        // Resposta a um pedido nosso (não fazemos pedidos) ou mensagem inválida.
        if (message.is_object() && (message.contains("result") || message.contains("error"))) return std::nullopt;
        return ErrorResponse(message.is_object() ? message.value("id", nullId) : nullId, kInvalidRequest,
                             "mensagem JSON-RPC 2.0 inválida");
    }
    const std::string method = message["method"].get<std::string>();
    const bool notification = !message.contains("id");
    const Json id = notification ? nullId : message["id"];
    const Json params = message.value("params", Json::object());

    try {
        Json result;
        if (method == "initialize") {
            result = Initialize(params);
        } else if (method == "ping") {
            result = Json::object();
        } else if (method == "tools/list") {
            result = ListTools();
        } else if (method == "tools/call") {
            result = CallTool(params);
        } else if (method.starts_with("notifications/")) {
            return std::nullopt;  // initialized, cancelled...: nada a fazer
        } else {
            if (notification) return std::nullopt;
            return ErrorResponse(id, kMethodNotFound, "método não suportado: " + method);
        }
        if (notification) return std::nullopt;
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    } catch (const RpcError& e) {
        if (notification) return std::nullopt;
        return ErrorResponse(id, e.code, e.message);
    } catch (const std::exception& e) {
        if (notification) return std::nullopt;
        return ErrorResponse(id, kInternalError, e.what());
    }
}

std::string Protocol::HandleText(std::string_view body, std::optional<Deferred>* deferred) {
    const Json message = Json::parse(body, nullptr, false);
    if (message.is_discarded()) return ErrorResponse(nullptr, kParseError, "JSON inválido").dump();
    if (message.is_array()) {
        // Lote (versões antigas do protocolo): responde a cada pedido.
        Json responses = Json::array();
        for (const Json& m : message) {
            if (auto r = Handle(m)) responses.push_back(std::move(*r));
        }
        m_later = {};  // em lote não se espera: valem as respostas de agora
        return responses.empty() ? std::string{} : Dump(responses);
    }
    const auto response = Handle(message);
    // Ferramenta que responde depois: em vez da resposta de agora, quem chamou espera (só pedido com id).
    if (auto later = std::exchange(m_later, {}); later && deferred && response && response->contains("result")) {
        *deferred = Deferred{(*response)["id"], std::move(later)};
        return {};
    }
    return response ? Dump(*response) : std::string{};
}

Json Protocol::Initialize(const Json& params) const {
    // Responde com a versão pedida se a conhecemos; senão, com a mais nova (o cliente decide se aceita).
    std::string version = kLatestVersion;
    const std::string asked = params.value("protocolVersion", "");
    for (const char* v : kVersions) {
        if (asked == v) version = v;
    }
    Json result = {
        {"protocolVersion", version},
        {"capabilities", {{"tools", {{"listChanged", false}}}}},
        {"serverInfo", {{"name", m_name}, {"version", m_version}}},
    };
    if (!m_instructions.empty()) result["instructions"] = m_instructions;
    return result;
}

Json Protocol::ListTools() const {
    Json tools = Json::array();
    for (const Tool& t : m_tools) {
        Json tool = {{"name", t.name}, {"description", t.description}, {"inputSchema", t.inputSchema}};
        if (t.readOnly) tool["annotations"] = {{"readOnlyHint", true}};
        tools.push_back(std::move(tool));
    }
    return Json{{"tools", std::move(tools)}};
}

Json Protocol::CallTool(const Json& params) {
    const std::string name = params.value("name", "");
    auto it = std::find_if(m_tools.begin(), m_tools.end(), [&](const Tool& t) { return t.name == name; });
    if (it == m_tools.end()) throw RpcError{kInvalidParams, "ferramenta desconhecida: " + name};
    const Json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"]
                                                                                      : Json::object();
    // Obrigatórios do esquema conferidos aqui: as ferramentas leem args["x"] direto, e ler uma chave que não
    // existe num JSON const derruba o editor (assert do nlohmann no build Debug).
    if (const auto required = it->inputSchema.find("required"); required != it->inputSchema.end()) {
        for (const Json& key : *required) {
            if (key.is_string() && !args.contains(key.get<std::string>())) {
                const ToolResult missing = ToolResult::Error("falta o argumento obrigatório '" + key.get<std::string>() + "'");
                return Json{{"content", missing.content}, {"isError", true}};
            }
        }
    }
    ToolResult result;
    m_later = {};
    try {
        result = it->run(args);
    } catch (const ToolError& e) {
        result = ToolResult::Error(e.what());
    } catch (const Json::exception& e) {
        result = ToolResult::Error(std::string("argumento com tipo errado: ") + e.what());
    } catch (const std::exception& e) {
        result = ToolResult::Error(std::string("erro: ") + e.what());
    }
    if (!result.isError) m_later = std::move(result.later);
    return CallResult(result);
}

} // namespace motor::mcp
