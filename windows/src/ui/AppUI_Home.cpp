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
        g_animating = true;
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
    else                    TextSmall(MUTED, "%s", tr("Paused in the background while a controller is connected.",
                                                      "Pausada em segundo plano enquanto há controle conectado."));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::SameLine(right - Dp(42.f));
    ImGui::SetCursorPosY(y + (b - Dp(24.f)) * 0.5f);
    bool v = s.autoSearch;
    if (Toggle("##autosearch", &v)) {
        if (v) m_sounds.Play(Sound::SearchStart); else Click();
        m_dev.UpdateSettings([v](AppSettings& a) { a.autoSearch = v; });
    }
    ImGui::SetCursorPosY(y + b);
    ImGui::Dummy({0, 0});

    if (!s.autoSearch) {
        if (Button(searching ? tr("Searching…##s", "Procurando…##s") : tr("Search now##s", "Procurar agora##s"),
                   ButtonStyle::Secondary, {-1.f, 0}, icon::Search)) {
            m_sounds.Play(Sound::SearchStart);
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
    const float rowX = ImGui::GetCursorPosX();

    unsigned glyphColor = FAINT;
    bool filled = false;
    switch (v.state) {
        case DeviceState::Connected:   glyphColor = KindColor(v.kind); filled = true; break;
        case DeviceState::Connecting:  glyphColor = MUTED; break;
        case DeviceState::WaitingSync: glyphColor = WARNING; break;
        case DeviceState::Available:   glyphColor = TEXT; break;
        default: break;
    }
    if (v.onUsb) { glyphColor = KindColor(v.kind); filled = true; }   // recognised on the cable: shown as healthy
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
            // Dots appear one by one, like the charging label.
            std::snprintf(status, sizeof(status), "%s%.*s", tr("Connecting", "Conectando"), int(ImGui::GetTime() * 2.5) % 4, "...");
            g_animating = true;
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

    // ---- layout: actions on the right, text gets what is left ----
    // On narrow windows the row sheds extras in order (remaining time, then the
    // details chevron: the whole card opens the page too), and the name and
    // status are cut with "…" (full text in a tooltip) instead of wrapping.
    const float iconW = Dp(32.f), gap = Dp(2.f);
    // Compact "Connect" pill sized to its label (a link icon says what it does).
    const char* connectLabel = tr("Connect##c", "Conectar##c");
    const float connectW = ButtonFitWidth(connectLabel, icon::Link, 22.f), connectH = Dp(30.f);
    const float textX = rowX + rowH + Dp(12.f);
    const std::string timeFull = FormatDuration(v.batteryMinutesLeft, /*compact*/ true);
    bool showTime = !timeFull.empty(), showDetails = true;
    float actionsW = 0.f, batteryW = 0.f, batteryGap = 0.f;
    auto measure = [&] {
        batteryW = v.onUsb || v.charging ? BatteryInlineWidth(-1, nullptr, true)
                 : v.state == DeviceState::Connected ? BatteryInlineWidth(v.batteryPct, showTime ? timeFull.c_str() : nullptr) : 0.f;
        batteryGap = batteryW > 0.f ? Dp(10.f) : 0.f;
        const float detailsW = showDetails ? iconW + gap : 0.f;
        switch (v.state) {
            case DeviceState::Connected:
            case DeviceState::WaitingSync: actionsW = batteryW + batteryGap + detailsW + iconW; break;
            case DeviceState::Connecting:  actionsW = iconW; break;   // spinner sits in the same column as the icon buttons
            case DeviceState::Available:   actionsW = v.known ? iconW : connectW + Dp(34.f); break;
            default:                       actionsW = batteryW + batteryGap + detailsW + iconW; break;
        }
    };
    auto textRoom = [&] { return right - actionsW - Dp(10.f) - textX; };
    measure();
    if (showTime && textRoom() < Dp(130.f)) { showTime = false; measure(); }
    if (openable && textRoom() < Dp(100.f)) { showDetails = false; measure(); }
    const std::string timeLeft = showTime ? timeFull : std::string();
    const float textW = textRoom();

    ImGui::SameLine(0, Dp(12.f));
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(rowY + Dp(2.f));
    TextEllipsis(g_fonts.semibold, SIZE_BODY, TEXT, v.name.c_str(), textW);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
    float statusW = textW;
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
        statusW -= bw + Dp(7.f);
    }
    TextEllipsis(g_fonts.regular, SIZE_SMALL, statusColor, status, statusW);
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
    // A 20 px spinner centred in a 32 px slot: lines up with the icon buttons
    // of the rows around it.
    auto spinner = [&] {
        const float sz = Dp(20.f);
        ImGui::SetCursorPos({ax + (iconW - sz) * 0.5f, rowY + (rowH - sz) * 0.5f});
        ax += iconW;
        Spinner(10.f, ACCENT);
    };
    auto details = [&] {
        if (!showDetails) return;
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
                BatteryTooltip(v.batteryPct, FormatDuration(v.batteryMinutesLeft, false).c_str(), v.batteryMv, nullptr);
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
            // One power button; the choice (keep it saved or forget it) opens below it.
            if (IconButton("##end", icon::Power, tr("Disconnect…", "Desconectar…"), DANGER, 32.f, DANGER)) {
                Click();
                ImGui::OpenPopup("##endmenu");
            }
            {
                const ImVec2 bmax = ImGui::GetItemRectMax();
                const float menuW = Dp(232.f);
                ImGui::SetNextWindowPos({bmax.x, bmax.y + Dp(6.f)}, ImGuiCond_Appearing, {1.f, 0.f});
                ImGui::SetNextWindowSize({menuW, 0});
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {Dp(6.f), Dp(6.f)});
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {Dp(6.f), Dp(4.f)});
                ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Dp(12.f));
                if (ImGui::BeginPopup("##endmenu")) {
                    if (Button(tr("Disconnect##m", "Desconectar##m"), ButtonStyle::Ghost, {-1.f, 0}, icon::Power)) {
                        Click();
                        m_dev.Disconnect(v.address);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", tr("The controller stays saved: press sync to play again.",
                                                   "O controle continua salvo: aperte sync para jogar de novo."));
                    if (Button(tr("Disconnect and forget##m", "Desconectar e esquecer##m"), ButtonStyle::Danger, {-1.f, 0}, icon::Close)) {
                        m_confirmForget = v.address; m_confirmName = v.name;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar(3);
            }
            break;
        case DeviceState::Connecting:
            spinner();
            break;
        case DeviceState::Available:
            if (v.known) { spinner(); break; }
            {
                const float bw = connectW, bh = connectH;
                const ImVec2 wp = ImGui::GetWindowPos();
                const ImVec2 bl = {wp.x + right - bw - Dp(28.f), wp.y + rowY + rowH * 0.5f + Dp(7.f) - ImGui::GetScrollY()};
                const float sh = Dp(14.f);
                SignalBars(dl, bl, sh, v.signal, MUTED);
                if (ImGui::IsMouseHoveringRect({bl.x - Dp(4.f), bl.y - sh - Dp(4.f)}, {bl.x + sh * 1.2f + Dp(4.f), bl.y + Dp(4.f)})) {
                    if (v.rssi) ImGui::SetTooltip(tr("Signal: %s · %d dBm", "Sinal: %s · %d dBm"), SignalWord(v.signal), int(v.rssi));
                    else        ImGui::SetTooltip("%s", tr("Signal: measuring…", "Sinal: medindo…"));
                }
                ax = right - bw;
                at(bw, bh);
                if (Button(connectLabel, ButtonStyle::Primary, {bw, bh}, icon::Link)) { Click(); m_dev.Connect(v.address); }
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
    ControllerGlyph(dl, {p.x + w * 0.5f - Dp(18.f), p.y + g * 0.5f}, g, ControllerKind::JoyConL, JOYCON_L, false);
    ControllerGlyph(dl, {p.x + w * 0.5f + Dp(18.f), p.y + g * 0.5f + std::sin(t * 2.f) * Dp(2.f)}, g,
                    ControllerKind::JoyConR, JOYCON_R, false);
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

    // "Your controllers" shows "connecting" / "waiting to connect" for its own
    // rows; "Nearby" shows "searching" while the scanner listens.
    bool anyConnecting = false, anyAvailable = false;
    for (const auto* d : mine) {
        anyConnecting |= d->state == DeviceState::Connecting;
        anyAvailable  |= d->state == DeviceState::Available && d->error.empty();
    }
    const char* waiting = anyConnecting ? tr("connecting", "conectando")
                        : anyAvailable  ? tr("waiting to connect", "aguardando conexão") : nullptr;
    if (!nearby.empty()) {
        SectionTitle(tr("NEARBY", "ENCONTRADOS POR PERTO"), false,
                     m_dev.Searching() && !m_dev.ScannerFailed() ? tr("searching", "procurando") : nullptr);
        for (auto* d : nearby) DrawDeviceRow(*d);
    }
    if (!mine.empty()) {
        SectionTitle(tr("YOUR CONTROLLERS", "SEUS CONTROLES"), false, waiting);
        for (auto* d : mine) DrawDeviceRow(*d);
    }
    if (mine.empty() && nearby.empty()) {
        ImGui::Dummy({0, Dp(2.f)});
        DrawEmptyState();
    }
}


} // namespace sw2
