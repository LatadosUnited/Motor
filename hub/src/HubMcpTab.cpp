// Hub: aba MCP (endereço do MCP do hub, token e como ligar o Claude Code nele).

#include "HubApp.h"
#include "HubMcp.h"
#include "HubWidgets.h"

#include <imgui.h>

namespace hub {

using namespace widgets;

void HubApp::DrawMcpTab() {
    ImGui::Spacing();
    if (!m_mcp || !m_mcp->IsRunning()) {
        ImGui::TextColored(kRed, "MCP do Hub desligado.");
        if (!m_mcpError.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kDim, "%s", m_mcpError.c_str());
            ImGui::PopTextWrapPos();
        }
        DrawStatusLine(m_status, m_statusError);
        return;
    }

    ImGui::TextColored(kGreen, "Ligado em %s", m_mcp->Url().c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "Fica ligado enquanto o Hub está aberto, também na bandeja. Agentes como o Claude Code "
                             "veem e abrem projetos por aqui sem simular mouse nem teclado.");
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("Token");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "Todo pedido precisa do cabeçalho \"Authorization: Bearer <token>\". O token fica em "
                             "%s; quem o tem pode usar o Hub.",
                       ToUtf8(m_paths.mcpTokenFile).c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Copiar o token")) {
        ImGui::SetClipboardText(m_mcp->Token().c_str());
        SetStatus("Token copiado.", false);
    }

    ImGui::SeparatorText("Ligar no Claude Code");
    ImGui::TextColored(kDim, "No terminal, uma vez:");
    const std::string command = m_mcp->ClaudeCommand();
    // O comando leva o token: mostra só o começo dele.
    const std::string shown = command.substr(0, command.size() - m_mcp->Token().size() - 1) +
                              m_mcp->Token().substr(0, 8) + "...\"";
    ImGui::TextUnformatted(shown.c_str());
    if (PrimaryButton("Copiar o comando")) {
        ImGui::SetClipboardText(command.c_str());
        SetStatus("Comando copiado: cole no terminal.", false);
    }

    DrawStatusLine(m_status, m_statusError);
}

} // namespace hub
