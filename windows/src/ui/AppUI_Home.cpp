// Home page: search card, controller rows, empty state.

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

// ===========================================================================
// Home
// ===========================================================================

void AppUI::DrawSearchCard(const AppSettings& s) {
    BeginCard("##search", 16.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float y = ImGui::GetCursorPosY();
    const bool searching = m_dev.Searching();
    const bool failed = m_dev.ScannerFailed();

    // Icon badge with a soft "radar" pulse while listening.
    const float b = Dp(42.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c = {p.x + b * 0.5f, p.y + b * 0.5f};
    const unsigned badge = failed ? DANGER : searching ? ACCENT : FAINT;
    if (searching && !failed) {
        for (int i = 0; i < 2; ++i) {
            const float ph = std::fmod(float(ImGui::GetTime()) * 0.6f + i * 0.5f, 1.f);
            dl->AddCircle(c, b * 0.5f + ph * Dp(8.f), Rgb(ACCENT, 0.45f * (1.f - ph)), 0, Dp(1.5f));
        }
    }
    dl->AddCircleFilled(c, b * 0.5f, Rgb(badge, 0.16f));
    DrawIconCentered(dl, c, 18.f, Rgb(badge), icon::Bluetooth);
    ImGui::Dummy({b, b});

    ImGui::SameLine(0, Dp(14.f));
    ImGui::SetCursorPosY(y + Dp(2.f));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(right - Dp(56.f));
    ImGui::PushFont(g_fonts.semibold, SIZE_BODY);
    ImGui::TextUnformatted(tr("Auto search", "Buscar automaticamente"));
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
    if (failed)             TextSmall(DANGER, "%s", tr("Bluetooth unavailable. Make sure it is turned on.", "Bluetooth indisponível. Verifique se está ligado."));
    else if (searching)     TextSmall(MUTED, "%s", tr("Listening for controllers in sync mode…", "Escutando controles em modo sync…"));
    else if (!s.autoSearch) TextSmall(MUTED, "%s", tr("Off. Use Search when you need it.", "Desligada. Use Procurar quando precisar."));
    else                    TextSmall(MUTED, "%s", tr("Paused while in the background.", "Pausada em segundo plano."));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::SameLine(right - Dp(42.f));
    ImGui::SetCursorPosY(y + (b - Dp(24.f)) * 0.5f);
    bool v = s.autoSearch;
    if (Toggle("##autosearch", &v)) {
        Click();
        m_dev.UpdateSettings([v](AppSettings& a) { a.autoSearch = v; });
    }
    ImGui::SetCursorPosY(y + b);
    ImGui::Dummy({0, 0});

    if (!s.autoSearch) {
        if (Button(searching ? tr("Searching…##s", "Procurando…##s") : tr("Search now##s", "Procurar agora##s"),
                   ButtonStyle::Secondary, {-1.f, 0}, icon::Search)) {
            Click();
            m_dev.SearchNow(30);
        }
    }
    EndCard();
}

void AppUI::DrawDeviceRow(const DeviceView& v) {
    ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(v.address)));
    const bool openable = v.known || v.state == DeviceState::Connected;
    BeginCard("##row", 12.f, openable);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float rowH = Dp(42.f);
    const float rowY = ImGui::GetCursorPosY();

    unsigned glyphColor = FAINT;
    bool filled = false;
    switch (v.state) {
        case DeviceState::Connected:   glyphColor = ACCENT; filled = true; break;
        case DeviceState::Connecting:  glyphColor = MUTED; break;
        case DeviceState::WaitingSync: glyphColor = WARNING; break;
        case DeviceState::Available:   glyphColor = TEXT; break;
        default: break;
    }
    if (v.onUsb) { glyphColor = ACCENT; filled = true; }   // recognised on the cable: shown as healthy
    const ImVec2 p = ImGui::GetCursorScreenPos();
    // Glyph sits on a soft rounded tile.
    dl->AddRectFilled(p, {p.x + rowH, p.y + rowH}, Rgb(SURFACE_2), Dp(12.f));
    ControllerGlyph(dl, {p.x + rowH * 0.5f, p.y + rowH * 0.5f}, rowH * 0.74f, v.kind, glyphColor, filled);
    ImGui::Dummy({rowH, rowH});

    // ---- text ----
    char status[200];
    unsigned statusColor = MUTED;
    switch (v.state) {
        case DeviceState::Connected:
            // The player number is shown as a badge; the text keeps the state.
            std::snprintf(status, sizeof(status), "%s%s%s%s", tr("Connected", "Conectado"),
                          v.combined ? tr(" · L+R pair", " · par L+R") : "",
                          v.mouseMode ? tr(" · mouse mode", " · modo mouse") : "",
                          v.charging ? tr(" · charging", " · carregando") : "");
            statusColor = SUCCESS;
            break;
        case DeviceState::Connecting:
            std::snprintf(status, sizeof(status), "%s", tr("Connecting…", "Conectando…"));
            break;
        case DeviceState::WaitingSync:
            if (v.onUsb) {   // the controller switched to USB itself; not an error
                std::snprintf(status, sizeof(status), "%s", tr("Plugged in by cable", "Conectado por cabo"));
                statusColor = SUCCESS;
            } else {
                std::snprintf(status, sizeof(status), "%s", tr("Disconnected · press sync to bring it back", "Desconectado · aperte sync para voltar"));
                statusColor = WARNING;
            }
            break;
        case DeviceState::Available:
            std::snprintf(status, sizeof(status), "%s", v.known ? tr("In sync mode", "Em modo sync")
                                                                : tr("In sync mode · new controller", "Em modo sync · novo controle"));
            break;
        default:
            if (v.onUsb)
                std::snprintf(status, sizeof(status), "%s", tr("Plugged in by cable", "Conectado por cabo"));
            else
                std::snprintf(status, sizeof(status), "%s", tr("Press sync on the controller to connect", "Aperte sync no controle para conectar"));
            statusColor = v.onUsb ? SUCCESS : FAINT;
            break;
    }
    if (!v.error.empty() && v.state != DeviceState::Connected) {
        std::snprintf(status, sizeof(status), "%s", v.error.c_str());
        statusColor = DANGER;
    }

    float actionsW = 0.f;
    const float iconW = Dp(32.f), gap = Dp(2.f);
    const std::string timeLeft = FormatDuration(v.batteryMinutesLeft, /*compact*/ true);
    const float batteryW = v.onUsb || v.charging ? BatteryInlineWidth(-1, nullptr, true)
                         : v.state == DeviceState::Connected ? BatteryInlineWidth(v.batteryPct, timeLeft.c_str()) : 0.f;
    const float batteryGap = batteryW > 0.f ? Dp(10.f) : 0.f;
    switch (v.state) {
        case DeviceState::Connected:
        case DeviceState::WaitingSync: actionsW = batteryW + batteryGap + iconW * 2 + gap; break;
        case DeviceState::Connecting:  actionsW = Dp(20.f); break;
        case DeviceState::Available:   actionsW = v.known ? Dp(20.f) : Dp(104.f) + Dp(36.f); break;
        default:                       actionsW = batteryW + batteryGap + iconW * 2 + gap; break;
    }

    ImGui::SameLine(0, Dp(12.f));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(right - actionsW - Dp(8.f));
    ImGui::SetCursorPosY(rowY + Dp(2.f));
    ImGui::PushFont(g_fonts.semibold, SIZE_BODY);
    ImGui::TextUnformatted(v.name.c_str());
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
    if (v.state == DeviceState::Connected && v.player > 0) {
        // Player badge: the most useful fact at a glance.
        char badge[8];
        std::snprintf(badge, sizeof(badge), "P%d", v.player);
        ImGui::PushFont(g_fonts.semibold, SIZE_SMALL - 1.f);
        const ImVec2 ts = ImGui::CalcTextSize(badge);
        const float bh = ImGui::GetTextLineHeight() + Dp(2.f), bw = ts.x + Dp(12.f);
        const ImVec2 bp = ImGui::GetCursorScreenPos();
        const float by = bp.y + Dp(1.f);
        dl->AddRectFilled({bp.x, by}, {bp.x + bw, by + bh}, Rgb(SUCCESS, 0.16f), bh * 0.5f);
        dl->AddText({bp.x + (bw - ts.x) * 0.5f, by + (bh - ts.y) * 0.5f}, Rgb(SUCCESS), badge);
        ImGui::PopFont();
        ImGui::Dummy({bw, bh});
        ImGui::SameLine(0, Dp(7.f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Dp(1.f));
    }
    TextSmall(statusColor, "%s", status);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // ---- right-aligned actions ----
    // Every action gets an explicit position, vertically centred on the row.
    // (SameLine() would reuse the line's first item Y, which differs between
    // the battery text and the 32 px icon buttons.)
    float ax = right - actionsW;
    auto at = [&](float width, float height) {
        ImGui::SetCursorPos({ax, rowY + (rowH - height) * 0.5f});
        ax += width;
    };
    auto details = [&] {
        at(iconW, iconW);
        if (IconButton("##details", icon::Chevron, tr("Details and calibration", "Detalhes e calibração"), MUTED, 32.f)) { Click(); Navigate(Page::Device, v.address); }
        ax += gap;
    };
    auto battery = [&] {
        if (batteryW <= 0.f) return;
        ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
        const float lineH = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        at(batteryW, lineH);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(1.f));   // optical: glyph sits 1 px low
        if (v.onUsb || v.charging) {
            BatteryInline(-1, nullptr, /*charging*/ true, /*iconRight*/ true);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", v.charging
                    ? tr("Charging over USB while playing over Bluetooth. The level shows again once unplugged.",
                         "Carregando pelo USB enquanto joga pelo Bluetooth. O nível volta a aparecer ao desplugar.")
                    : tr("Charging over USB. Unplug and press sync to play over Bluetooth.",
                         "Carregando pelo USB. Desplugue e aperte sync para jogar pelo Bluetooth."));
        } else {
            BatteryInline(v.batteryPct, timeLeft.c_str(), false, /*iconRight*/ true);
            if (ImGui::IsItemHovered()) {
                const std::string full = FormatDuration(v.batteryMinutesLeft, false);
                if (full.empty()) ImGui::SetTooltip(tr("Estimated battery · %.2f V", "Bateria estimada · %.2f V"), v.batteryMv / 1000.f);
                else ImGui::SetTooltip(tr("Estimated battery · about %s left · %.2f V", "Bateria estimada · cerca de %s restantes · %.2f V"),
                                       full.c_str(), v.batteryMv / 1000.f);
            }
        }
        ax += batteryGap;
    };
    switch (v.state) {
        case DeviceState::Connected:
        case DeviceState::WaitingSync:
            battery();
            details();
            at(iconW, iconW);
            if (IconButton("##disc", icon::Close, tr("Disconnect and forget", "Desconectar e esquecer"), DANGER, 32.f, DANGER)) {
                m_confirmForget = v.address; m_confirmName = v.name;
            }
            break;
        case DeviceState::Connecting:
            at(Dp(20.f), Dp(20.f));
            Spinner(10.f, ACCENT);
            break;
        case DeviceState::Available:
            if (v.known) { at(Dp(20.f), Dp(20.f)); Spinner(10.f, ACCENT); break; }
            {
                const float bw = Dp(104.f), bh = Dp(BUTTON_H);
                const ImVec2 wp = ImGui::GetWindowPos();
                SignalBars(dl, {wp.x + right - bw - Dp(26.f), wp.y + rowY + rowH * 0.5f + Dp(7.f) - ImGui::GetScrollY()},
                           Dp(14.f), v.rssi, MUTED);
                ax = right - bw;
                at(bw, bh);
                if (Button(tr("Connect##c", "Conectar##c"), ButtonStyle::Primary, {bw, bh})) { Click(); m_dev.Connect(v.address); }
            }
            break;
        default:   // known, offline
            battery();
            details();
            at(iconW, iconW);
            if (IconButton("##forget", icon::Delete, tr("Forget controller", "Esquecer controle"), DANGER, 32.f, DANGER)) {
                m_confirmForget = v.address; m_confirmName = v.name;
            }
            break;
    }
    ImGui::SetCursorPosY(rowY + rowH);
    ImGui::Dummy({0, 0});
    if (EndCard() && openable) { Click(); Navigate(Page::Device, v.address); }
    ImGui::PopID();
}

void AppUI::DrawEmptyState() {
    BeginCard("##empty", 26.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float g = Dp(64.f);
    const float t = float(ImGui::GetTime());
    ControllerGlyph(dl, {p.x + w * 0.5f - Dp(18.f), p.y + g * 0.5f}, g, ControllerKind::JoyConL, FAINT, false);
    ControllerGlyph(dl, {p.x + w * 0.5f + Dp(18.f), p.y + g * 0.5f + std::sin(t * 2.f) * Dp(2.f)}, g,
                    ControllerKind::JoyConR, ACCENT, false);
    ImGui::Dummy({w, g + Dp(12.f)});
    TextCentered(g_fonts.semibold, SIZE_BODY + 1.f, TEXT, tr("No controllers yet", "Nenhum controle por aqui"));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, Dp(2.f)});
    TextCentered(g_fonts.regular, SIZE_SMALL, MUTED, tr("Hold the controller's sync button (the small button", "Segure o botão sync do controle (o pequeno botão"));
    TextCentered(g_fonts.regular, SIZE_SMALL, MUTED, tr("on its side) until the lights start running.", "na lateral) até as luzes começarem a correr."));
    ImGui::PopStyleVar();
    EndCard();
}

void AppUI::DrawHome(const std::vector<DeviceView>& devices) {
    const AppSettings& s = m_settings;

    if (!m_dev.VigemAvailable()) {
        BeginCard("##vigem", 16.f);
        TextColored(WARNING, "%s  %s", icon::Warning, tr("ViGEmBus not found", "ViGEmBus não encontrado"));
        TextSmall(MUTED, "%s", tr("It creates the virtual controller that games and Steam see. Install it and reopen the app.",
                                  "Ele cria o controle virtual que os jogos e a Steam enxergam. Instale e reabra o app."));
        if (Button(tr("Download ViGEmBus", "Baixar ViGEmBus"), ButtonStyle::Secondary, {-1.f, 0}))
            ShellExecuteW(nullptr, L"open", L"https://github.com/nefarius/ViGEmBus/releases/latest", nullptr, nullptr, SW_SHOWNORMAL);
        EndCard();
        ImGui::Dummy({0, Dp(2.f)});
    }

    DrawUpdateBanner();
    DrawSearchCard(s);

    std::vector<const DeviceView*> mine, nearby;
    for (const auto& d : devices) {
        if (d.inSession || d.known || d.state == DeviceState::Connected || d.state == DeviceState::Connecting)
            mine.push_back(&d);
        else if (d.state == DeviceState::Available)
            nearby.push_back(&d);
    }
    auto rank = [](const DeviceView* v) {
        switch (v->state) {
            case DeviceState::Connected:   return 0;
            case DeviceState::Connecting:  return 1;
            case DeviceState::WaitingSync: return 2;
            case DeviceState::Available:   return 3;
            default:                       return 4;
        }
    };
    std::stable_sort(mine.begin(), mine.end(), [&](auto a, auto b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a->player < b->player;
    });

    if (!nearby.empty()) {
        SectionTitle(tr("NEARBY", "ENCONTRADOS POR PERTO"));
        for (auto* d : nearby) DrawDeviceRow(*d);
    }
    if (!mine.empty()) {
        SectionTitle(tr("YOUR CONTROLLERS", "SEUS CONTROLES"));
        for (auto* d : mine) DrawDeviceRow(*d);
    }
    if (mine.empty() && nearby.empty()) {
        ImGui::Dummy({0, Dp(2.f)});
        DrawEmptyState();
    }
}


} // namespace sw2
