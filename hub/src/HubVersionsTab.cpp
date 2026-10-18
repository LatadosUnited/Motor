// Hub: aba Versões (instaladas, baixar dos servidores, servidores, adicionar e remover).

#include "HubApp.h"
#include "HubWidgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <Platform.h>

#include <algorithm>
#include <format>

namespace hub {

using namespace widgets;

void HubApp::DrawVersionsTab() {
    ImGui::BeginDisabled(m_adding.valid());
    if (PrimaryButton("Adicionar de uma pasta...")) {
        if (auto folder = platform::PickFolder({}, L"Pasta montada de uma versão do Motor (com motor-version.json)")) {
            StartAddVersion(*folder);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Abrir a pasta das versões")) {
        std::error_code ec;
        fs::create_directories(m_paths.versionsDir, ec);
        platform::RevealInExplorer(m_paths.versionsDir);
    }
    ImGui::TextColored(kDim, "Instaladas em %s", ToUtf8(m_paths.versionsDir).c_str());
    ImGui::Spacing();

    const float statusHeight = m_status.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing() * 1.6f;
    ImGui::BeginChild("ListaVersoes", ImVec2(0, -statusHeight));
    ImGui::SeparatorText("Instaladas");
    const InstalledVersion* def = DefaultVersion(m_versions, m_config.defaultVersion);
    if (m_versions.empty()) {
        ImGui::TextColored(kOrange, "Nenhuma versão instalada: baixe uma abaixo ou adicione de uma pasta.");
    } else if (ImGui::BeginTable("Versoes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Versão", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("API de scripts", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Compilação", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Pasta", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 250.0f);
        ImGui::TableHeadersRow();
        for (const InstalledVersion& v : m_versions) {
            ImGui::PushID(v.version.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Motor %s", v.version.c_str());
            if (&v == def) {
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "padrão");
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", v.scriptApiVersion);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.buildType.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", ToUtf8(v.dir).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ToUtf8(v.dir).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(&v == def);
            if (ImGui::Button("Tornar padrão")) {
                std::string message;
                const bool ok = SetDefaultVersion(v.version, message);
                SetStatus(message, !ok);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Mostrar")) platform::RevealInExplorer(v.dir);
            ImGui::SameLine();
            if (ImGui::Button("Remover")) {
                m_removeVersion = v.version;
                m_openRemoveVersion = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    DrawRemoteSection();
    ImGui::Spacing();
    DrawServersSection();

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "Versões vêm de um servidor de versões (MotorServidor, na rede local) ou de uma pasta "
                             "montada no código do Motor (cmake --build build --target motor_package). O editor "
                             "instalado precisa do MSYS2 (UCRT64) e do CMake para compilar os scripts dos projetos.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    DrawStatusLine(m_status, m_statusError);
}

void HubApp::DrawRemoteSection() {
    ImGui::SeparatorText("Disponíveis para baixar");
    ImGui::BeginDisabled(m_fetching.valid());
    if (ImGui::Button(m_fetching.valid() ? "Procurando..." : "Atualizar")) StartFetch();
    ImGui::EndDisabled();

    // Versões de todos os servidores, sem repetir (vale o primeiro servidor da lista que tiver a versão).
    struct Row {
        const distrib::Release* release;
        std::string server;
    };
    std::vector<Row> rows;
    for (const RemoteServer& s : m_remote) {
        if (!s.manifest) continue;
        for (const distrib::Release& r : s.manifest->releases) {
            const bool seen = std::any_of(rows.begin(), rows.end(), [&](const Row& row) {
                return row.release->version == r.version;
            });
            if (!seen) rows.push_back({&r, s.url});
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return CompareVersions(a.release->version, b.release->version) > 0;
    });

    if (!m_fetchedOnce) {
        ImGui::SameLine();
        ImGui::TextColored(kDim, "Procurando versões nos servidores...");
        return;
    }
    if (rows.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kDim, m_config.servers.empty() ? "Nenhum servidor na lista (adicione um abaixo)."
                                                          : "Nenhuma versão encontrada nos servidores.");
        return;
    }
    if (!ImGui::BeginTable("Disponiveis", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        return;
    }
    ImGui::TableSetupColumn("Versão", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("API", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Compilação", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Data e tamanho", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Servidor", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 250.0f);
    ImGui::TableHeadersRow();
    for (const Row& row : rows) {
        const distrib::Release& r = *row.release;
        ImGui::PushID(r.version.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Motor %s", r.version.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.channel.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", r.scriptApiVersion);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.buildType.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%s  %s", distrib::FormatDate(r.date).c_str(), distrib::FormatSize(r.size).c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(kDim, "%s", row.server.c_str());
        ImGui::TableNextColumn();
        if (FindVersion(m_versions, r.version)) {
            ImGui::TextColored(kGreen, "Instalada");
        } else if (m_download && m_download->release.version == r.version) {
            distrib::ReleaseProgress p;
            {
                std::lock_guard lock(m_download->mutex);
                p = m_download->progress;
            }
            const float fraction = p.total ? static_cast<float>(static_cast<double>(p.done) / p.total) : 0.0f;
            const std::string overlay = std::format("{} de {}", distrib::FormatSize(p.done), distrib::FormatSize(p.total));
            ImGui::ProgressBar(fraction, ImVec2(150.0f, 0.0f), overlay.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Arquivo %zu de %zu: %s", p.filesDone + 1, p.filesTotal, p.currentFile.c_str());
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_download->cancel.load());
            if (ImGui::Button("Cancelar")) m_download->cancel = true;
            ImGui::EndDisabled();
        } else {
            const uint64_t staged = distrib::StagedBytes(m_paths.versionsDir, r);
            const std::string label = staged > 0 ? std::format("Retomar ({}%)", staged * 100 / std::max<uint64_t>(r.size, 1))
                                                 : std::string("Baixar");
            ImGui::BeginDisabled(m_download != nullptr);
            if (PrimaryButton(label.c_str())) StartDownload(row.server, r);
            ImGui::EndDisabled();
            if (staged > 0 && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s já baixados; o resto continua de onde parou.", distrib::FormatSize(staged).c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void HubApp::DrawServersSection() {
    if (!ImGui::CollapsingHeader("Servidores", ImGuiTreeNodeFlags_DefaultOpen)) return;
    std::string toRemove;
    for (const std::string& url : m_config.servers) {
        ImGui::PushID(url.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(url.c_str());
        ImGui::SameLine(320.0f);
        const auto it = std::find_if(m_remote.begin(), m_remote.end(), [&](const RemoteServer& s) { return s.url == url; });
        if (m_fetching.valid() || it == m_remote.end()) {
            ImGui::TextColored(kDim, "...");
        } else if (it->manifest) {
            ImGui::TextColored(kGreen, "no ar, %zu versão(ões)", it->manifest->releases.size());
        } else {
            ImGui::TextColored(kRed, "%s", it->error.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 90.0f);
        if (ImGui::Button("Remover", ImVec2(90.0f, 0.0f))) toRemove = url;
        ImGui::PopID();
    }
    if (!toRemove.empty()) {
        std::string message;
        const bool ok = RemoveServer(toRemove, message);
        SetStatus(message, !ok);
        StartFetch();
    }
    ImGui::SetNextItemWidth(320.0f);
    const bool enter = ImGui::InputTextWithHint("##servidor", "http://servidor:47900", &m_newServer,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_newServer.empty());
    if (ImGui::Button("Adicionar servidor") || (enter && !m_newServer.empty())) {
        std::string message;
        const bool ok = AddServer(m_newServer, message);
        SetStatus(message, !ok);
        if (ok) {
            m_newServer.clear();
            StartFetch();
        }
    }
    ImGui::EndDisabled();
}

void HubApp::DrawHubUpdateNotice() {
    // Hub mais novo publicado em algum servidor: aviso com o link do instalador.
    const RemoteServer* newest = nullptr;
    for (const RemoteServer& s : m_remote) {
        if (!s.manifest || !s.manifest->hub) continue;
        if (CompareVersions(s.manifest->hub->version, MOTOR_HUB_VERSION) <= 0) continue;
        if (!newest || CompareVersions(s.manifest->hub->version, newest->manifest->hub->version) > 0) newest = &s;
    }
    if (!newest) return;
    const distrib::HubRelease& h = *newest->manifest->hub;
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kOrange, "Hub %s disponível em %s.", h.version.c_str(), newest->url.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Baixar o instalador")) {
        const char* file = h.Find(distrib::kSetupExe) ? distrib::kSetupExe : distrib::kHubExe;
        distrib::platform::OpenUrl(newest->url + distrib::HubFileUrl(file));
    }
}

void HubApp::DrawRemoveVersionPopup() {
    constexpr const char* kTitle = "Remover versão";
    if (m_openRemoveVersion) {
        ImGui::OpenPopup(kTitle);
        m_openRemoveVersion = false;
    }
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Enviar o Motor %s para a Lixeira?", m_removeVersion.c_str());
    ImGui::TextColored(kDim, "Projetos desta versão vão pedir outra versão para abrir.");
    ImGui::Separator();
    if (PrimaryButton("Enviar para a Lixeira")) {
        std::string message;
        const bool ok = RemoveVersion(m_removeVersion, message);
        SetStatus(message, !ok);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void HubApp::StartAddVersion(const fs::path& source) {
    m_addingSource = source;
    const fs::path versionsDir = m_paths.versionsDir;
    m_adding = std::async(std::launch::async, [source, versionsDir] {
        AddResult result;
        result.version = InstallVersion(source, versionsDir, &result.error);
        return result;
    });
}

void HubApp::DrawAddingVersionPopup() {
    constexpr const char* kTitle = "Adicionando versão";
    if (!m_adding.valid() && !ImGui::IsPopupOpen(kTitle)) return;

    bool done = !m_adding.valid();
    if (!done && m_adding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const AddResult result = m_adding.get();
        RefreshProjects();
        if (result.version) {
            SetStatus(std::format("Motor {} instalado em '{}'.", result.version->version, ToUtf8(result.version->dir)),
                      false);
        } else {
            SetStatus(result.error, true);
        }
        done = true;
    }
    if (!done && !ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (done) ImGui::CloseCurrentPopup();
        ImGui::Text("Copiando %s ...", ToUtf8(m_addingSource).c_str());
        ImGui::TextColored(kDim, "Pode levar alguns segundos (a pasta tem centenas de MB).");
        ImGui::EndPopup();
    }
}

} // namespace hub
