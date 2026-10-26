// httplib inclui o winsock2.h: precisa vir antes de qualquer windows.h.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <httplib.h>

#include <motor/mcp/HubLink.h>

#include <algorithm>

namespace motor::mcp::hublink {

HttpReply Post(int port, std::string_view path, std::string_view token, const std::string& body, int timeoutMs) {
    httplib::Client client("127.0.0.1", port);
    const time_t seconds = timeoutMs / 1000;
    const time_t micros = (timeoutMs % 1000) * 1000;
    client.set_connection_timeout(std::min<time_t>(seconds, 5), seconds >= 5 ? 0 : micros);  // loopback: conecta já
    client.set_read_timeout(seconds, micros);
    client.set_write_timeout(seconds, micros);
    httplib::Headers headers{{"Accept", "application/json, text/event-stream"}};
    if (!token.empty()) headers.emplace("Authorization", "Bearer " + std::string(token));
    HttpReply reply;
    const auto res = client.Post(std::string(path), headers, body, "application/json");
    if (!res) {
        reply.error = "sem resposta de 127.0.0.1:" + std::to_string(port) + " (" + httplib::to_string(res.error()) + ")";
        return reply;
    }
    reply.status = res->status;
    reply.body = res->body;
    return reply;
}

bool PostJson(int port, std::string_view path, std::string_view token, const Json& body, std::string* error,
              int timeoutMs) {
    const HttpReply reply = Post(port, path, token, body.dump(), timeoutMs);
    if (reply.status == 0) {
        if (error) *error = reply.error;
        return false;
    }
    if (reply.status < 200 || reply.status >= 300) {
        if (error) *error = "resposta " + std::to_string(reply.status) + ": " + reply.body;
        return false;
    }
    return true;
}

bool PortFree(int port) {
    // Socket cru, sem SO_REUSEADDR: a httplib liga a opção e, no Windows, um segundo bind com ela "consegue" a porta
    // de quem já escuta. Sem a opção, o bind falha se alguém estiver lá.
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bool free = false;
    if (s != INVALID_SOCKET) {
        BOOL exclusive = TRUE;  // nem um SO_REUSEADDR de outro programa pega a porta durante o teste
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        free = bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
        closesocket(s);
    }
    WSACleanup();
    return free;
#else
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool free = bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
    close(s);
    return free;
#endif
}

bool SameSecret(std::string_view a, std::string_view b) {
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ (i < b.size() ? b[i] : 0));
    return diff == 0;
}

} // namespace motor::mcp::hublink
