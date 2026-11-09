#include "ui/AppUI.h"

#include "app/Version.h"
#include "core/I18n.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sw2 {

using namespace theme;
using namespace ui;

AppUI::AppUI(DeviceManager& dev, Sounds& sounds) : m_dev(dev), m_sounds(sounds) {
    m_updates.Start();   // once per launch, in the background
}


void AppUI::CheckUpdateNotice() {
    if (m_updateNotified) return;
    const UpdateInfo u = m_updates.Get();
    if (u.state != UpdateInfo::State::Available) return;
    m_updateNotified = true;
    char msg[96];
    std::snprintf(msg, sizeof(msg), tr("Version %s is available", "A versão %s está disponível"), u.latest.c_str());
    m_sounds.Play(Sound::Found);
    PushToast(msg, ACCENT, icon::Info);
}

void AppUI::DrawUpdateBanner() {
    const UpdateInfo u = m_updates.Get();
    if (u.state != UpdateInfo::State::Available || m_updateBannerDismissed) return;
    BeginCard("##updatebanner", 14.f);
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float y = ImGui::GetCursorPosY();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float b = Dp(34.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled({p.x + b * 0.5f, p.y + b * 0.5f}, b * 0.5f, Rgb(ACCENT, 0.16f));
    DrawIconCentered(dl, {p.x + b * 0.5f, p.y + b * 0.5f}, 15.f, Rgb(ACCENT), icon::Info);
    ImGui::Dummy({b, b});
    ImGui::SameLine(0, Dp(12.f));
    ImGui::SetCursorPosY(y - Dp(1.f));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(right - Dp(150.f));
    char title[96];
    std::snprintf(title, sizeof(title), tr("Version %s available", "Versão %s disponível"), u.latest.c_str());
    ImGui::PushFont(g_fonts.semibold, SIZE_BODY);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
    TextSmall(MUTED, tr("You have %s.", "Você está na %s."), APP_VERSION);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SetCursorPos({right - Dp(32.f) - Dp(4.f) - Dp(96.f), y + (b - Dp(BUTTON_H)) * 0.5f});
    if (Button(tr("Download##upd", "Baixar##upd"), ButtonStyle::Primary, {Dp(96.f), 0})) { Click(); OpenUrl(u.url); }
    ImGui::SetCursorPos({right - Dp(32.f), y + (b - Dp(32.f)) * 0.5f});
    if (IconButton("##dismissupd", icon::Close, tr("Hide", "Ocultar"), MUTED, 32.f)) { Click(); m_updateBannerDismissed = true; }
    ImGui::SetCursorPosY(y + b);
    ImGui::Dummy({0, 0});
    EndCard();
}

// ===========================================================================
// Setup
// ===========================================================================

static bool FileExists(const char* p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

void AppUI::SetupFonts(float dpiScale) {
    g_dpiScale = dpiScale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const char* regular  = FileExists("C:\\Windows\\Fonts\\segoeui.ttf") ? "C:\\Windows\\Fonts\\segoeui.ttf" : nullptr;
    const char* semibold = FileExists("C:\\Windows\\Fonts\\seguisb.ttf") ? "C:\\Windows\\Fonts\\seguisb.ttf" : regular;
    const char* icons    = FileExists("C:\\Windows\\Fonts\\SegoeIcons.ttf") ? "C:\\Windows\\Fonts\\SegoeIcons.ttf"
                         : FileExists("C:\\Windows\\Fonts\\segmdl2.ttf")    ? "C:\\Windows\\Fonts\\segmdl2.ttf" : nullptr;

    auto load = [&](const char* path) -> ImFont* {
        ImFont* f = path ? io.Fonts->AddFontFromFileTTF(path, SIZE_BODY) : io.Fonts->AddFontDefault();
        if (f && icons) {
            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.GlyphOffset = {0.f, 1.5f};
            io.Fonts->AddFontFromFileTTF(icons, SIZE_BODY, &cfg);
        }
        return f;
    };
    g_fonts.regular  = load(regular);
    g_fonts.semibold = load(semibold);
    io.FontDefault = g_fonts.regular;
}

void AppUI::SetupStyle(float dpiScale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding     = {12, 9};    // tooltips/popups; the root window pushes its own
    st.FramePadding      = {10, 7};
    st.ItemSpacing       = {10, 10};
    st.ItemInnerSpacing  = {8, 6};
    st.WindowRounding    = 0;
    st.ChildRounding     = 14;
    st.FrameRounding     = 10;
    st.PopupRounding     = 12;
    st.GrabRounding      = 10;
    st.ScrollbarRounding = 10;
    st.ScrollbarSize     = 6;
    st.GrabMinSize       = 14;
    st.WindowBorderSize  = 0;
    st.PopupBorderSize   = 1;
    st.FrameBorderSize   = 0;
    st.ScaleAllSizes(dpiScale);
    st.FontScaleDpi = dpiScale;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = Rgb4(TEXT);
    c[ImGuiCol_TextDisabled]         = Rgb4(FAINT);
    c[ImGuiCol_WindowBg]             = Rgb4(BG);
    c[ImGuiCol_ChildBg]              = Rgb4(0, 0.f);
    c[ImGuiCol_PopupBg]              = Rgb4(SURFACE);
    c[ImGuiCol_Border]               = Rgb4(BORDER);
    c[ImGuiCol_FrameBg]              = Rgb4(SURFACE_2);
    c[ImGuiCol_FrameBgHovered]       = Rgb4(SURFACE_3);
    c[ImGuiCol_FrameBgActive]        = Rgb4(SURFACE_3);
    c[ImGuiCol_TitleBg]              = Rgb4(BG);
    c[ImGuiCol_TitleBgActive]        = Rgb4(BG);
    c[ImGuiCol_ScrollbarBg]          = Rgb4(0, 0.f);
    c[ImGuiCol_ScrollbarGrab]        = Rgb4(SURFACE_3, 0.8f);
    c[ImGuiCol_ScrollbarGrabHovered] = Rgb4(HOVER);
    c[ImGuiCol_ScrollbarGrabActive]  = Rgb4(PRESSED);
    c[ImGuiCol_CheckMark]            = Rgb4(ACCENT);
    c[ImGuiCol_SliderGrab]           = Rgb4(ACCENT);
    c[ImGuiCol_SliderGrabActive]     = Rgb4(ACCENT_HI);
    c[ImGuiCol_Button]               = Rgb4(SURFACE_3);
    c[ImGuiCol_ButtonHovered]        = Rgb4(HOVER);
    c[ImGuiCol_ButtonActive]         = Rgb4(PRESSED);
    c[ImGuiCol_Header]               = Rgb4(SURFACE_3);
    c[ImGuiCol_HeaderHovered]        = Rgb4(HOVER);
    c[ImGuiCol_HeaderActive]         = Rgb4(PRESSED);
    c[ImGuiCol_Separator]            = Rgb4(BORDER);
    c[ImGuiCol_ModalWindowDimBg]     = Rgb4(0x000000, 0.45f);
    c[ImGuiCol_NavCursor]            = Rgb4(ACCENT, 0.6f);
    c[ImGuiCol_TextSelectedBg]       = Rgb4(ACCENT, 0.35f);
}

// ===========================================================================
// Frame
// ===========================================================================

void AppUI::Navigate(Page p, uint64_t address) {
    m_page = p;
    m_selected = address;
    m_renameFor = 0;
    ImGui::SetScrollY(0.f);
}

void AppUI::PushToast(const std::string& text, unsigned color, const char* glyph) {
    m_toasts.push_back({text, color, glyph, ImGui::GetTime()});
    if (m_toasts.size() > 3) m_toasts.erase(m_toasts.begin());
}

void AppUI::HandleEvents() {
    for (const auto& e : m_dev.DrainEvents()) {
        switch (e.type) {
            case EventType::Found:        m_sounds.Play(Sound::Found);        PushToast(e.text, MUTED, icon::Bluetooth); break;
            case EventType::Connected:    m_sounds.Play(Sound::Connected);    PushToast(e.text, SUCCESS, icon::Check); break;
            case EventType::Reconnected:  m_sounds.Play(Sound::Reconnected);  PushToast(e.text, SUCCESS, icon::Sync); break;
            case EventType::Lost:         m_sounds.Play(Sound::Lost);         PushToast(e.text, WARNING, icon::Warning); break;
            case EventType::Disconnected: m_sounds.Play(Sound::Disconnected); PushToast(e.text, MUTED, icon::Close); break;
            case EventType::Failed:       m_sounds.Play(Sound::Error);        PushToast(e.text, DANGER, icon::Warning); break;
            case EventType::MouseMode:    m_sounds.Play(Sound::Toggle);       PushToast(e.text, MUTED, icon::Info); break;
            case EventType::BatteryLow:   m_sounds.Play(Sound::BatteryLow);   PushToast(e.text, WARNING, icon::Warning); break;
            // A cable is a deliberate action: soft cues, never the "lost" alarm.
            case EventType::UsbConnected: m_sounds.Play(Sound::Found);        PushToast(e.text, MUTED, icon::Link); break;
            case EventType::UsbRemoved:   m_sounds.Play(Sound::Toggle);       PushToast(e.text, MUTED, icon::Info); break;
            case EventType::Info:         PushToast(e.text, MUTED, icon::Info); break;
        }
    }
}

void AppUI::Background() {
    m_focused = false;
    m_dev.Tick(false);
    const AppSettings s = m_dev.Settings();
    m_sounds.SetEnabled(s.sounds);
    m_sounds.SetVolume(s.volume);
    HandleEvents();
    CheckUpdateNotice();
}

void AppUI::Frame(bool focused) {
    m_focused = focused;
    m_dev.Tick(focused);
    m_settings = m_dev.Settings();   // one copy per frame, shared by every page
    const AppSettings& s = m_settings;
    m_sounds.SetEnabled(s.sounds);
    m_sounds.SetVolume(s.volume);
    HandleEvents();
    CheckUpdateNotice();
    const auto devices = m_dev.Snapshot();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {Dp(20.f), Dp(18.f)});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    // Centered column: comfortable line length on wide windows.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float colW = std::min(avail, Dp(560.f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - colW) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::BeginChild("##column", {colW, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    DrawHeader();
    switch (m_page) {
        case Page::Home:     DrawHome(devices); break;
        case Page::Device:   DrawDevicePage(devices); break;
        case Page::Settings: DrawSettingsPage(); break;
    }
    ImGui::Dummy({0, Dp(64.f)});   // room for toasts

    DrawCalibration();
    DrawConfirm();
    ImGui::EndChild();
    ImGui::End();
    DrawToasts();
}

// ===========================================================================
// Header
// ===========================================================================

void AppUI::DrawHeader() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = Dp(42.f);
    const float y = ImGui::GetCursorPosY();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float iconBtn = 36.f;

    if (m_page == Page::Home) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        AppLogo(dl, p, h);
        ImGui::Dummy({h, h});
        ImGui::SameLine(0, Dp(12.f));
        ImGui::SetCursorPosY(y + Dp(1.f));
        ImGui::BeginGroup();
        ImGui::PushFont(g_fonts.semibold, SIZE_TITLE);
        ImGui::TextUnformatted("SW2 Adapter");
        ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(9.f));
        TextSmall(MUTED, "%s", tr("Switch 2 controllers on Windows", "Controles do Switch 2 no Windows"));
        ImGui::EndGroup();
        ImGui::SameLine(right - Dp(iconBtn));
        ImGui::SetCursorPosY(y + (h - Dp(iconBtn)) * 0.5f);
        if (IconButton("##settings", icon::Settings, tr("Settings", "Configurações"), MUTED, iconBtn)) { Click(); Navigate(Page::Settings); }
    } else {
        ImGui::SetCursorPosY(y + (h - Dp(iconBtn)) * 0.5f);
        if (IconButton("##back", icon::Back, tr("Back", "Voltar"), MUTED, iconBtn)) { Click(); Navigate(Page::Home); }
        ImGui::SameLine(0, Dp(6.f));
        ImGui::PushFont(g_fonts.semibold, SIZE_TITLE);
        // Optical alignment: centre the cap height (not the line box, which
        // includes descender space) on the back arrow.
        ImGui::SetCursorPosY(y + (h - ImGui::GetTextLineHeight()) * 0.5f - Dp(1.5f));
        ImGui::TextUnformatted(m_page == Page::Settings ? tr("Settings", "Configurações") : tr("Controller", "Controle"));
        ImGui::PopFont();
        ImGui::SetCursorPosY(y + h);
        ImGui::Dummy({0, 0});
    }
    ImGui::Dummy({0, Dp(6.f)});
}


// ===========================================================================
// Toasts
// ===========================================================================

void AppUI::DrawToasts() {
    const double now = ImGui::GetTime();
    constexpr double life = 3.6;
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
                                  [&](const Toast& t) { return now - t.born > life; }), m_toasts.end());
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - Dp(20.f);
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
        const float age = float(now - it->born);
        // Ease-out cubic in, linear fade out.
        const float in = 1.f - std::pow(1.f - std::min(1.f, age / 0.25f), 3.f);
        const float a = std::min(in, float(life - age) / 0.4f);
        ImGui::PushFont(g_fonts.regular, SIZE_SMALL + 0.5f);
        const ImVec2 ts = ImGui::CalcTextSize(it->text.c_str());
        const ImVec2 gs = {Dp(16.f), Dp(16.f)};   // icon box
        const float padX = Dp(16.f), h = Dp(40.f);
        const float w = std::min(vp->WorkSize.x - Dp(32.f), ts.x + gs.x + padX * 2 + Dp(10.f));
        const float slide = (1.f - in) * Dp(12.f);
        const ImVec2 a0 = {vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, y - h + slide};
        dl->AddRectFilled({a0.x, a0.y + Dp(3.f)}, {a0.x + w, a0.y + h + Dp(3.f)}, IM_COL32(0, 0, 0, int(80 * a)), h * 0.5f);
        dl->AddRectFilled(a0, {a0.x + w, a0.y + h}, Rgb(SURFACE_3, a), h * 0.5f);
        dl->AddRect(a0, {a0.x + w, a0.y + h}, Rgb(0xFFFFFF, 0.08f * a), h * 0.5f, 0, 1.f);
        DrawIconCentered(dl, {a0.x + padX + gs.x * 0.5f, a0.y + h * 0.5f}, 14.f, Rgb(it->color, a), it->glyph);
        dl->PushClipRect(a0, {a0.x + w - padX * 0.5f, a0.y + h}, true);
        dl->AddText({a0.x + padX + gs.x + Dp(10.f), a0.y + (h - ts.y) * 0.5f}, Rgb(TEXT, a), it->text.c_str());
        dl->PopClipRect();
        ImGui::PopFont();
        y -= h + Dp(8.f);
    }
}

} // namespace sw2
