#include "viewport/FrameStatsPanel.h"

#include <algorithm>
#include <format>

namespace editor {

namespace {
constexpr double kAlpha = 0.1;  // peso do quadro novo na média: ~10 quadros para assentar

double ValueOf(const std::vector<motor::FrameStats::Entry>& list, std::string_view name) {
    for (const auto& e : list) {
        if (e.name == name) return e.value;
    }
    return 0.0;
}

// Uma linha do painel: rótulo à esquerda e valor alinhado à direita (a fonte do ImGui não é monoespaçada).
struct Line {
    std::string label;
    std::string value;
    ImU32 color;
    float indent = 0.0f;
};
} // namespace

void FrameStatsPanel::Blend(std::vector<Smoothed>& into, const std::vector<motor::FrameStats::Entry>& from,
                            double alpha) {
    for (const auto& e : from) {
        auto it = std::find_if(into.begin(), into.end(), [&](const Smoothed& s) { return s.name == e.name; });
        if (it == into.end()) {
            into.push_back({e.name, e.value});  // nova: começa no valor, sem subir do zero
        } else {
            it->value += (e.value - it->value) * alpha;
        }
    }
}

void FrameStatsPanel::Update(const motor::FrameStats::Frame& frame) {
    if (frame.index == 0 || frame.index == m_lastFrame) return;
    const double alpha = m_lastFrame == 0 ? 1.0 : kAlpha;
    m_lastFrame = frame.index;
    m_frameMs += (frame.frameMs - m_frameMs) * alpha;
    Blend(m_cpu, frame.cpu, alpha);
    Blend(m_gpu, frame.gpu, alpha);
    // Números e memória: o valor do quadro (já são estáveis; a média deixaria "1000,4 draws").
    m_values.clear();
    for (const auto& e : frame.values) m_values.push_back({e.name, e.value});
}

void FrameStatsPanel::Draw(ImDrawList* dl, const ImVec2& topRight, float maxHeight) {
    const motor::FrameStats::Frame& frame = motor::FrameStats::Instance().Last();
    Update(frame);

    const ImU32 head = IM_COL32(255, 220, 120, 255), text = IM_COL32(235, 235, 235, 255),
                dim = IM_COL32(175, 175, 175, 255);
    const float step = ImGui::GetFontSize();
    std::vector<Line> lines;
    auto ms = [](double v) { return std::format("{:.2f}", v); };

    lines.push_back({"Quadro", std::format("{:.2f} ms  {:.0f} FPS", m_frameMs, m_frameMs > 0.0 ? 1000.0 / m_frameMs : 0.0),
                     head});

    // CPU: as partes do loop na ordem em que rodam; debaixo delas, sistemas e scripts do mais caro ao mais barato.
    static constexpr const char* kLoop[] = {"Eventos", "FixedUpdate", "Update", "Render", "ImGui", "SwapBuffers"};
    lines.push_back({"CPU (ms)", "", head});
    for (const char* name : kLoop) {
        const auto it = std::find_if(m_cpu.begin(), m_cpu.end(), [&](const Smoothed& s) { return s.name == name; });
        if (it != m_cpu.end()) lines.push_back({it->name, ms(it->value), text, step});
    }
    std::vector<const Smoothed*> parts;
    for (const Smoothed& s : m_cpu) {
        if (std::find(std::begin(kLoop), std::end(kLoop), s.name) == std::end(kLoop)) parts.push_back(&s);
    }
    std::sort(parts.begin(), parts.end(), [](const Smoothed* a, const Smoothed* b) { return a->value > b->value; });
    for (const Smoothed* s : parts) {
        if (s->value >= 0.05) lines.push_back({s->name, ms(s->value), dim, step * 2.0f});  // o resto só ocupa espaço
    }

    if (!m_gpu.empty()) {
        lines.push_back({"GPU (ms)", "", head});
        for (const Smoothed& s : m_gpu) lines.push_back({s.name, ms(s.value), text, step});
    }

    lines.push_back({"Draws / triângulos",
                     std::format("{:.0f} ({:.0f} sombra) / {:.0f}", ValueOf(frame.values, "draws"),
                                 ValueOf(frame.values, "draws sombra"), ValueOf(frame.values, "triangulos")),
                     head});

    lines.push_back({"Memória de vídeo", std::format("{:.1f} MB", ValueOf(frame.values, "vram total MB")), head});
    for (const Smoothed& s : m_values) {
        if (!s.name.starts_with("vram ") || s.name == "vram total MB" || s.value < 0.05) continue;
        const std::string kind = s.name.substr(5, s.name.size() - 5 - 3);  // "vram <tipo> MB"
        lines.push_back({kind, std::format("{:.1f}", s.value), text, step});
    }

    // Fundo do tamanho do texto, encostado no canto; o que não couber até `maxHeight` vira "...".
    const ImVec2 pad{8.0f, 6.0f};
    const float gap = step * 1.5f;
    const float lineHeight = ImGui::GetTextLineHeight();
    const auto fit = static_cast<size_t>(std::max(1.0f, (maxHeight - pad.y * 2) / lineHeight));
    if (lines.size() > fit) {
        lines.resize(fit - 1);
        lines.push_back({"...", "", dim});  // "…" pode não estar na fonte do editor
    }
    float labelWidth = 0.0f, valueWidth = 0.0f;
    for (const Line& l : lines) {
        labelWidth = std::max(labelWidth, l.indent + ImGui::CalcTextSize(l.label.c_str()).x);
        valueWidth = std::max(valueWidth, ImGui::CalcTextSize(l.value.c_str()).x);
    }
    const float width = labelWidth + gap + valueWidth;
    const ImVec2 p0{topRight.x - width - pad.x * 2, topRight.y};
    const ImVec2 p1{topRight.x, topRight.y + lineHeight * static_cast<float>(lines.size()) + pad.y * 2};
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 150), 4.0f);
    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& l = lines[i];
        const float y = p0.y + pad.y + lineHeight * static_cast<float>(i);
        dl->AddText(ImVec2(p0.x + pad.x + l.indent, y), l.color, l.label.c_str());
        const float vw = ImGui::CalcTextSize(l.value.c_str()).x;
        dl->AddText(ImVec2(p1.x - pad.x - vw, y), l.color, l.value.c_str());
    }
}

} // namespace editor
