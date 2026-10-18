#include "HubWidgets.h"

#include <filesystem>
#include <format>

namespace hub::widgets {

void ApplyStyle() {
    // As mesmas cores do editor (engine/src/core/ImGuiLayer.cpp), sem depender da engine.
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.ChildRounding = 4.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.FramePadding = ImVec2(8.0f, 5.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.11f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.33f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.33f, 0.52f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.40f, 0.62f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.19f, 0.27f, 0.39f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
}

void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
#ifdef _WIN32
    for (const char* path : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"}) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path, 17.0f)) return;
    }
#endif
    io.Fonts->AddFontDefault();
}

bool PrimaryButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.70f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.50f, 0.80f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return pressed;
}

void VersionCombo(const char* label, const std::vector<InstalledVersion>& versions, std::string& selected,
                  std::string_view defaultVersion) {
    const std::string preview = selected.empty() ? "(nenhuma)" : "Motor " + selected;
    if (!ImGui::BeginCombo(label, preview.c_str())) return;
    for (const InstalledVersion& v : versions) {
        const std::string item = std::format("Motor {}{}", v.version, v.version == defaultVersion ? " (padrão)" : "");
        if (ImGui::Selectable(item.c_str(), v.version == selected)) selected = v.version;
    }
    ImGui::EndCombo();
}

void DrawStatusLine(const std::string& status, bool error) {
    if (status.empty()) return;
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(error ? kRed : kDim, "%s", status.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace hub::widgets
