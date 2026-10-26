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

    ImGui::SeparatorText("MCP dos editores");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "O MCP do editor de cada projeto, pelo Hub: funciona com o editor fechado (o Hub o abre "
                             "na primeira ferramenta) e não pede para reconectar quando ele fecha e abre de novo. %s "
                             "vai para o editor aberto por último.",
                       m_mcp->EngineUrl().c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Copiar o comando (editor aberto por último)")) {
        ImGui::SetClipboardText(m_mcp->ClaudeEngineCommand().c_str());
        SetStatus("Comando do servidor \"motor\" copiado: cole no terminal.", false);
    }
    for (size_t i = 0; i < m_projects.size(); ++i) {
        const ProjectInfo& p = m_projects[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Copiar URL")) {
            ImGui::SetClipboardText(m_mcp->EngineUrl(p.name).c_str());
            SetStatus("URL de '" + p.name + "' copiada.", false);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(m_mcp->EngineUrl(p.name).c_str());
        ImGui::PopID();
    }

    DrawStatusLine(m_status, m_statusError);
}

} // namespace hub
