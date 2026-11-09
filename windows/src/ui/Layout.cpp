#include "ui/Layout.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>

namespace sw2::ui {

using namespace theme;

void OpenUrl(const std::string& url) {
    const std::wstring w(url.begin(), url.end());
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool ToggleRow(const char* id, const char* title, const char* desc, bool* value) {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float y = ImGui::GetCursorPosY();
    ImGui::PushTextWrapPos(right - Dp(60.f));
    ImGui::BeginGroup();
    ImGui::TextUnformatted(title);
    if (desc && *desc) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
        TextSmall(MUTED, "%s", desc);
    }
    ImGui::EndGroup();
    ImGui::PopTextWrapPos();
    const float h = ImGui::GetItemRectSize().y;
    ImGui::SameLine(right - Dp(42.f));
    ImGui::SetCursorPosY(y + std::max(0.f, (h - Dp(24.f)) * 0.5f));
    const bool r = Toggle(id, value);
    // Move below the taller of text/toggle and submit an item so the parent
    // window grows with it (ImGui requires an item after SetCursorPos).
    ImGui::SetCursorPosY(y + std::max(h, Dp(24.f)));
    ImGui::Dummy({0.f, 0.f});
    return r;
}

void Label(const char* text) {
    ImGui::TextUnformatted(text);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
}

void Divider() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(p, {p.x + ImGui::GetContentRegionAvail().x, p.y}, Rgb(BORDER, 0.7f));
    ImGui::Dummy({0, Dp(1.f)});
}

// Two buttons side by side, each taking half of the row.
float HalfWidth() { return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f; }

std::string FormatDuration(int minutes, bool compact) {
    if (minutes < 0) return {};
    if (minutes >= 60) minutes = (minutes + 5) / 10 * 10;   // false precision helps nobody
    const int h = minutes / 60, m = minutes % 60;
    char b[32];
    if (compact)             std::snprintf(b, sizeof(b), h > 0 ? "%dh" : "%dmin", h > 0 ? h : m);
    else if (h > 0 && m > 0) std::snprintf(b, sizeof(b), "%d h %02d min", h, m);
    else if (h > 0)          std::snprintf(b, sizeof(b), "%d h", h);
    else                     std::snprintf(b, sizeof(b), "%d min", m);
    return b;
}

} // namespace sw2::ui
