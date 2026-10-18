// Hub: aba Projetos (cartões, abrir com outra versão, novo projeto).

#include "HubApp.h"
#include "HubWidgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <Platform.h>

#include <algorithm>
#include <format>

namespace hub {

using namespace widgets;

void HubApp::DrawProjectsTab() {
    if (PrimaryButton("Novo projeto")) {
        m_openNewProject = true;
        m_newName = "Novo Jogo";
        const fs::path parent = m_config.newProjectDir.empty() ? m_paths.defaultProjectsDir : m_config.newProjectDir;
        m_newParent = ToUtf8(parent);
        const InstalledVersion* def = DefaultVersion(m_versions, m_config.defaultVersion);
        m_newVersion = def ? def->version : std::string();
    }
    ImGui::SameLine();
    if (ImGui::Button("Adicionar pasta...")) {
        if (auto folder = platform::PickFolder(m_paths.defaultProjectsDir, L"Pasta de um projeto do Motor")) {
            std::string message;
            const bool ok = AddProjectFolder(*folder, message);
            SetStatus(message, !ok);
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##busca", "Buscar por nome ou pasta", &m_search);
    ImGui::Spacing();

    const float statusHeight = m_status.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing() * 1.6f;
    ImGui::BeginChild("ListaProjetos", ImVec2(0, -statusHeight), ImGuiChildFlags_None);
    // Abertos mais recentemente primeiro; os nunca abertos pelo hub depois, na ordem da lista.
    std::vector<size_t> order(m_projects.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int64_t ta = a < m_config.projects.size() ? m_config.projects[a].lastOpened : 0;
        const int64_t tb = b < m_config.projects.size() ? m_config.projects[b].lastOpened : 0;
        return ta > tb;
    });
    size_t shown = 0;
    for (const size_t i : order) {
        if (i >= m_projects.size()) break;  // um cartão removeu um projeto da lista neste quadro
        const ProjectInfo& p = m_projects[i];
        if (!MatchesSearch(p.name, m_search) && !MatchesSearch(ToUtf8(p.root), m_search)) continue;
        DrawProjectCard(i);
        ++shown;
    }
    if (m_projects.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kDim, "Nenhum projeto ainda. Crie um em \"Novo projeto\" ou adicione a pasta de um existente.");
    } else if (shown == 0) {
        ImGui::TextColored(kDim, "Nenhum projeto com \"%s\".", m_search.c_str());
    }
    if (m_versions.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kOrange, "Nenhuma versão do Motor instalada: adicione uma na aba Versões.");
    }
    ImGui::EndChild();
    DrawStatusLine(m_status, m_statusError);
}

void HubApp::DrawProjectCard(size_t index) {
    const ProjectInfo p = m_projects[index];  // cópia: as ações abaixo podem recarregar a lista
    const HubProject* entry = index < m_config.projects.size() ? &m_config.projects[index] : nullptr;
    const int64_t lastOpened = entry ? entry->lastOpened : 0;

    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginChild("Cartao", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const float buttonsWidth = 190.0f;
    const float startX = ImGui::GetCursorPosX();
    const float innerWidth = ImGui::GetContentRegionAvail().x;
    const float textWidth = innerWidth - buttonsWidth;
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, 20.0f);
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
    ImGui::TextColored(kDim, "%s", ToUtf8(p.root).c_str());
    ImGui::PopTextWrapPos();

    if (!p.found) {
        ImGui::TextColored(kRed, "Não encontrado: %s", p.error.c_str());
    } else if (p.engineVersion.empty()) {
        ImGui::TextColored(kOrange, "Motor: versão desconhecida");
    } else if (FindVersion(m_versions, p.engineVersion)) {
        ImGui::TextColored(kGreen, "Motor %s", p.engineVersion.c_str());
    } else {
        ImGui::TextColored(kOrange, "Motor %s (não instalada)", p.engineVersion.c_str());
    }
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::TextColored(kDim, "Aberto: %s", FormatLastOpened(lastOpened, NowSeconds()).c_str());
    ImGui::EndGroup();

    // Duplo clique no cartão também abre.
    if (p.found && ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        RequestOpen(index, false);
    }

    ImGui::SameLine(startX + innerWidth - buttonsWidth + 16.0f);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(!p.found || m_versions.empty());
    if (PrimaryButton("Abrir", ImVec2(110, 0))) RequestOpen(index, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(44, 0))) ImGui::OpenPopup("MenuProjeto");
    if (ImGui::BeginPopup("MenuProjeto")) {
        if (ImGui::MenuItem("Abrir com outra versão...", nullptr, false, p.found && !m_versions.empty())) {
            RequestOpen(index, true);
        }
        if (ImGui::MenuItem("Mostrar no Explorer")) platform::RevealInExplorer(p.found ? p.file : p.root);
        ImGui::Separator();
        if (ImGui::MenuItem("Remover da lista")) {
            std::string message;
            const bool ok = RemoveProject(p.root, message);
            SetStatus(message, !ok);
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopID();
}

void HubApp::RequestOpen(size_t index, bool chooseVersion) {
    if (index >= m_projects.size()) return;
    const ProjectInfo p = m_projects[index];  // cópia: abrir relê a lista
    const VersionChoice choice = ChooseVersion(p.engineVersion, m_versions, m_config.defaultVersion);
    if (choice.match == VersionMatch::NoVersions) {
        SetStatus("Nenhuma versão do Motor instalada: adicione uma na aba Versões.", true);
        return;
    }
    if (choice.match == VersionMatch::Exact && !chooseVersion) {
        std::string message;
        const bool ok = OpenProject(p.root, *choice.version, message);
        SetStatus(message, !ok);
        return;
    }
    // Versão ausente, desconhecida ou escolha pedida: pergunta (com o aviso de cópia de segurança).
    m_openWithRoot = p.root;
    m_openWithVersion = choice.version ? choice.version->version : std::string();
    m_openOpenWith = true;
}

void HubApp::DrawOpenWithPopup() {
    constexpr const char* kTitle = "Abrir com outra versão";
    if (m_openOpenWith) {
        ImGui::OpenPopup(kTitle);
        m_openOpenWith = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    const ProjectInfo info = ReadProject(m_openWithRoot);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("Projeto: %s", info.name.c_str());
    if (info.engineVersion.empty()) {
        ImGui::TextColored(kOrange, "Este projeto não diz em qual versão do Motor foi feito (projeto antigo).");
    } else if (!FindVersion(m_versions, info.engineVersion)) {
        ImGui::TextColored(kOrange, "Este projeto é do Motor %s, que não está instalado.", info.engineVersion.c_str());
    } else {
        ImGui::Text("Versão do projeto: Motor %s", info.engineVersion.c_str());
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(220.0f);
    VersionCombo("Abrir com", m_versions, m_openWithVersion, m_config.defaultVersion);

    const bool changes = !m_openWithVersion.empty() && m_openWithVersion != info.engineVersion;
    if (changes) {
        ImGui::Spacing();
        ImGui::TextColored(kOrange,
                           "O editor vai passar o projeto para o Motor %s (campo engineVersion do .mproject). "
                           "Depois disso ele pode não abrir mais na versão anterior: faça uma cópia de "
                           "segurança antes (copie a pasta do projeto ou faça um commit).",
                           m_openWithVersion.c_str());
        if (!info.engineVersion.empty() && CompareVersions(m_openWithVersion, info.engineVersion) < 0) {
            ImGui::TextColored(kRed, "Atenção: o Motor %s é mais antigo que o do projeto; cenas e scripts podem "
                                     "não abrir.", m_openWithVersion.c_str());
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    const InstalledVersion* chosen = FindVersion(m_versions, m_openWithVersion);
    ImGui::BeginDisabled(!chosen);
    const std::string label = chosen ? std::format("Abrir no Motor {}", chosen->version) : std::string("Abrir");
    if (PrimaryButton(label.c_str())) {
        std::string message;
        const bool ok = OpenProject(m_openWithRoot, *chosen, message);
        SetStatus(message, !ok);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void HubApp::DrawNewProjectPopup() {
    constexpr const char* kTitle = "Novo projeto";
    if (m_openNewProject) {
        ImGui::OpenPopup(kTitle);
        m_openNewProject = false;
    }
    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    const float labelWidth = 60.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Nome");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##nome", &m_newName);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Pasta");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-100);
    ImGui::InputText("##pasta", &m_newParent);
    ImGui::SameLine();
    if (ImGui::Button("Escolher...", ImVec2(-1, 0))) {
        if (auto folder = platform::PickFolder(PathFromUtf8(m_newParent), L"Pasta onde o projeto será criado")) {
            m_newParent = ToUtf8(*folder);
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Versão");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(220.0f);
    VersionCombo("##versao", m_versions, m_newVersion, m_config.defaultVersion);

    // A pasta padrão (Documentos\Motor) pode ainda não existir: é criada ao confirmar.
    const fs::path parent = PathFromUtf8(m_newParent);
    std::error_code ec;
    const bool parentIsDefault = SamePath(parent, m_paths.defaultProjectsDir) && !fs::exists(parent, ec);
    std::string problem = parentIsDefault ? ValidateProjectName(m_newName) : CheckNewProject(parent, m_newName);
    const InstalledVersion* version = FindVersion(m_versions, m_newVersion);
    if (problem.empty() && !version) problem = "Escolha uma versão do Motor (instale uma na aba Versões).";

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    if (problem.empty()) {
        ImGui::TextColored(kDim, "Será criado em: %s", ToUtf8(parent / PathFromUtf8(m_newName)).c_str());
    } else {
        ImGui::TextColored(kRed, "%s", problem.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    ImGui::BeginDisabled(!problem.empty());
    if (PrimaryButton("Criar e abrir")) {
        if (parentIsDefault) fs::create_directories(parent, ec);
        std::string message;
        const bool ok = CreateProject(parent, m_newName, *version, message);
        SetStatus(message, !ok);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace hub
