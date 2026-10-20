#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Protocolo MCP (Model Context Protocol) do servidor do editor: mensagens JSON-RPC 2.0 e a lista de
// ferramentas. Não sabe nada de rede nem de thread (McpServer cuida disso), então dá para testar sozinho.
// Métodos atendidos: initialize, ping, tools/list e tools/call; notificações não têm resposta.
namespace editor::mcp {

using Json = nlohmann::ordered_json;

// Erro nos argumentos de uma ferramenta: vira um resultado com isError (o modelo lê e corrige a chamada).
struct ToolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Resultado de tools/call: lista de conteúdos (texto ou imagem).
struct ToolResult {
    Json content = Json::array();
    bool isError = false;
    // Resposta que só fica pronta em quadros seguintes (play e scripts_build com wait = true). O servidor HTTP
    // chama `later` uma vez por quadro, na thread principal, até ela devolver true com o resultado final em `out`
    // (ToolError vira erro). Sem quem espere (testes, lote de pedidos), vale o conteúdo de agora.
    std::function<bool(ToolResult& out)> later;

    static ToolResult Text(std::string text);
    static ToolResult Error(std::string text);
    static ToolResult Data(const Json& value);  // JSON indentado como texto
    ToolResult& AddText(std::string text);
    ToolResult& AddImage(std::string base64, std::string mimeType);
};

struct Tool {
    std::string name;         // snake_case, único
    std::string description;  // o que faz e quando usar (o modelo escolhe a ferramenta por ela)
    Json inputSchema;         // JSON Schema do objeto de argumentos
    bool readOnly = false;    // só lê o estado (anotação readOnlyHint)
    std::function<ToolResult(const Json& args)> run;
};

// Ajudantes para montar o inputSchema: Schema({{"id", Prop("integer", "...")}}, {"id"}).
Json Prop(const char* type, const char* description);
Json Schema(Json properties = Json::object(), std::vector<std::string> required = {});

// Pedido cuja resposta ficou para depois (ToolResult::later): o id do JSON-RPC e a verificação.
struct Deferred {
    Json id;
    std::function<bool(ToolResult& out)> check;

    // Chama a verificação; quando pronta, o texto da resposta JSON-RPC (senão, vazio).
    std::optional<std::string> Poll() const;
    std::string Finish(const ToolResult& result) const;
};

class Protocol {
public:
    static constexpr const char* kLatestVersion = "2025-06-18";

    Protocol(std::string serverName, std::string serverVersion, std::string instructions = {});

    void AddTool(Tool tool);  // nome repetido substitui a anterior
    const std::vector<Tool>& Tools() const { return m_tools; }

    // Uma mensagem JSON-RPC -> resposta; nullopt para notificações (sem "id").
    std::optional<Json> Handle(const Json& message);
    // Corpo de um POST: texto -> texto da resposta ("" = nada a responder, HTTP 202). Com `deferred`, uma
    // ferramenta que responde depois (ToolResult::later) preenche *deferred e o texto volta vazio: quem chamou
    // espera com Deferred::Poll.
    std::string HandleText(std::string_view body, std::optional<Deferred>* deferred = nullptr);

private:
    Json Initialize(const Json& params) const;
    Json ListTools() const;
    Json CallTool(const Json& params);

    std::string m_name;
    std::string m_version;
    std::string m_instructions;
    std::vector<Tool> m_tools;
    std::function<bool(ToolResult&)> m_later;  // `later` da última tools/call (Handle decide se espera)
};

} // namespace editor::mcp
