#pragma once

// Ligação entre o Hub residente e os editores que ele abre (Tarefa 7). O Hub escolhe uma porta interna livre
// (kEditorPortFirst a kEditorPortLast), gera um token só para aquele editor e o abre com
//   MotorEditor ... --mcp=<porta interna> --hub <porta do Hub>     e o token na variável kTokenEnv
// O editor liga o MCP dele nessa porta exigindo o token e avisa o Hub (POST em kNotifyPath, com o mesmo token) quando
// fica pronto, quando troca de projeto e quando fecha. Só as versões com "hubLink" >= kVersion na lista de ferramentas
// (mcp-ferramentas.json) entendem o --hub: as antigas abrem como antes.
//
// Aviso: {"event": "pronto" | "projeto" | "fechando", "port": 47801, "project": "C:\\...\\Jogo"}; o Hub sabe de
// qual editor é pelo token.

#include <motor/mcp/McpProtocol.h>

#include <string>
#include <string_view>

namespace motor::mcp::hublink {

inline constexpr int kVersion = 1;
inline constexpr int kEditorPortFirst = 47801;
inline constexpr int kEditorPortLast = 47899;
inline constexpr const char* kNotifyPath = "/mcphub/editor";
inline constexpr const char* kTokenEnv = "MOTOR_MCP_TOKEN";
inline constexpr const char* kArgument = "--hub";

inline constexpr const char* kEventReady = "pronto";
inline constexpr const char* kEventProject = "projeto";
inline constexpr const char* kEventClosing = "fechando";

// POST de `body` em http://127.0.0.1:<port><path> com "Authorization: Bearer <token>". true com resposta 2xx.
bool PostJson(int port, std::string_view path, std::string_view token, const Json& body, std::string* error = nullptr,
              int timeoutMs = 1000);

// A porta abre em 127.0.0.1 agora? (Outro programa pode pegá-la logo depois: o editor avisa se não conseguir.)
bool PortFree(int port);

// Compara sem parar no primeiro caractere diferente (o tempo da resposta não diz quanto do token acertou).
bool SameSecret(std::string_view a, std::string_view b);

} // namespace motor::mcp::hublink
