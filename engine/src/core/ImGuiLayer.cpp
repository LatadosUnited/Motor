#include "motor/core/ImGuiLayer.h"

#include "motor/core/Log.h"
#include "motor/core/Window.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <filesystem>

namespace motor {

namespace {
void ApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.FramePadding = ImVec2(6.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 5.0f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.09f, 0.10f, 0.11f, 0.98f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.26f, 0.29f, 0.33f, 1.00f);
    c[ImGuiCol_Header]            = ImVec4(0.20f, 0.33f, 0.52f, 0.80f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.25f, 0.40f, 0.62f, 0.80f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_Tab]               = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.19f, 0.27f, 0.39f, 1.00f);
    c[ImGuiCol_TabDimmed]         = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.19f, 0.25f, 1.00f);
    c[ImGuiCol_Button]            = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
}

void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
#ifdef _WIN32
    const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"};
    for (const char* path : candidates) {
        if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path, 16.0f)) return;
    }
#endif
    io.Fonts->AddFontDefault();
}
} // namespace

ImGuiLayer::ImGuiLayer(Window& window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ApplyStyle();
    LoadFont();

    // install_callbacks=true encadeia com os callbacks do nosso Input (já instalados pela Window).
    ImGui_ImplGlfw_InitForOpenGL(window.Native(), true);
    ImGui_ImplOpenGL3_Init("#version 450 core");

    log::Info("Dear ImGui {} inicializado", ImGui::GetVersion());
}

ImGuiLayer::~ImGuiLayer() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::WantsMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantsKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace motor
