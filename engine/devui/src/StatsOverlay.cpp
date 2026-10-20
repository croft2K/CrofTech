#include "croftech/devui/StatsOverlay.h"

#include <imgui.h>

#include <algorithm>

namespace ct {
namespace {

constexpr float kMargin = 10.0f;
constexpr float kMinSpeedScale = 10.0f; // m/s, so walking doesn't fill the graph

} // namespace

void StatsOverlay::add(const FrameStats& stats) {
    m_latest = stats;
    m_frameMilliseconds[static_cast<std::size_t>(m_next)] = stats.frameMilliseconds;
    m_speed[static_cast<std::size_t>(m_next)] = stats.speed;
    m_next = (m_next + 1) % kHistory;
    m_count = std::min(m_count + 1, kHistory);
}

void StatsOverlay::draw() const {
    if (m_count == 0) {
        return;
    }
    float total = 0.0f;
    float worst = 0.0f;
    float fastest = 0.0f;
    for (int i = 0; i < m_count; ++i) {
        total += m_frameMilliseconds[static_cast<std::size_t>(i)];
        worst = std::max(worst, m_frameMilliseconds[static_cast<std::size_t>(i)]);
        fastest = std::max(fastest, m_speed[static_cast<std::size_t>(i)]);
    }
    const float average = total / static_cast<float>(m_count);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + kMargin, viewport->WorkPos.y + kMargin});
    ImGui::SetNextWindowBgAlpha(0.45f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("Stats", nullptr, kFlags)) {
        const ImVec2 graphSize{ImGui::GetFontSize() * 16.0f, ImGui::GetFontSize() * 3.0f};
        ImGui::Text("%.0f fps", average > 0.0f ? 1000.0f / average : 0.0f);
        ImGui::Text("frame %.2f ms  (worst %.2f)", average, worst);
        ImGui::Text("tick  %.3f ms", m_latest.tickMilliseconds);
        ImGui::PlotLines("##frame", m_frameMilliseconds.data(), m_count, m_next % m_count, nullptr,
                         0.0f, std::max(worst * 1.25f, 0.1f), graphSize);
        ImGui::Text("speed %.2f m/s", m_latest.speed);
        ImGui::PlotLines("##speed", m_speed.data(), m_count, m_next % m_count, nullptr, 0.0f,
                         std::max(fastest * 1.1f, kMinSpeedScale), graphSize);
    }
    ImGui::End();
}

} // namespace ct
