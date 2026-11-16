// Controller page: identity, mapping, sticks, live test, diagnostics.

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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <map>
#include <vector>

namespace sw2 {

using namespace theme;
using namespace ui;

namespace {
constexpr double READOUT_EVERY = 0.5;   // seconds between refreshes of numeric readouts

// Link quality while connected, from how regularly reports arrive: 0..4 bars
// (0 = not measurable, e.g. a controller that only reports on change).
int LinkLevel(const DeviceView& v) {
    if (v.state != DeviceState::Connected || v.streaming == Streaming::OnChange) return 0;
    return v.linkLevel;   // smoothed and with hysteresis, see DeviceManager::UpdateStatsLocked
}

// Tooltip of the connection ladder: the verdict as the headline (in the
// ladder's colour, with the bars), what it means, then the numbers behind it.
void LinkTooltip(const DeviceView& v, int level, unsigned color) {
    if (!ImGui::BeginTooltip()) return;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {Dp(6.f), Dp(3.f)});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = Dp(250.f), sh = Dp(13.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushFont(g_fonts.bold ? g_fonts.bold : g_fonts.semibold, SIZE_BODY + 2.f);
    const float lineH = ImGui::GetTextLineHeight();
    SignalBars(dl, {p.x, p.y + (lineH + sh) * 0.5f}, sh, level, color);
    std::string word = SignalWord(level);
    if (!word.empty()) word[0] = char(std::toupper(static_cast<unsigned char>(word[0])));
    dl->AddText({p.x + sh * 1.2f + Dp(9.f), p.y}, Rgb(color), word.c_str());
    ImGui::PopFont();
    ImGui::Dummy({w, lineH + Dp(2.f)});

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    TextSmall(MUTED, "%s",
        level >= 4 ? tr("Inputs are arriving on time.", "Os comandos estão chegando no tempo certo.")
      : level == 3 ? tr("Small variations, not noticeable while playing.", "Pequenas variações, imperceptíveis ao jogar.")
      : level == 2 ? tr("Unstable: you may feel an occasional delay. Move closer to the PC or clear what is between them.",
                        "Instável: pode haver atraso de vez em quando. Aproxime o controle do PC ou tire obstáculos do caminho.")
                   : tr("Poor: delays and missed inputs are likely. Move closer to the Bluetooth antenna of the PC.",
                        "Ruim: atrasos e comandos perdidos são prováveis. Aproxime o controle da antena Bluetooth do PC."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, Dp(2.f)});

    char a[24], b[24], c[24];
    std::snprintf(a, sizeof(a), "%.1f ms", v.intervalMs);
    std::snprintf(b, sizeof(b), "%.1f ms", v.jitterMs);
    if (v.latePct < 0.05f) std::snprintf(c, sizeof(c), "0%%");
    else                   std::snprintf(c, sizeof(c), "%.1f%%", v.latePct);
    Segments({{tr("Packet interval  ", "Intervalo entre pacotes  "), FAINT}, {a, TEXT, true}});
    Segments({{tr("Jitter  ", "Variação  "), FAINT}, {b, TEXT, true}});
    Segments({{tr("Late packets  ", "Pacotes atrasados  "), FAINT}, {c, v.latePct >= 6.f ? WARNING : TEXT, true}});
    ImGui::PopStyleVar();
    ImGui::EndTooltip();
}
} // namespace

// ===========================================================================
// Device page
// ===========================================================================

void AppUI::DrawDevicePage(const std::vector<DeviceView>& devices) {
    const DeviceView* found = nullptr;
    for (auto& d : devices) if (d.address == m_selected) found = &d;
    if (!found) { Navigate(Page::Home); return; }
    // Timing numbers change with every packet: show a reading taken twice a
    // second instead (see READOUT_EVERY).
    DeviceView shown = *found;
    if (m_statsFor != shown.address || ImGui::GetTime() - m_statsAt >= READOUT_EVERY) {
        m_statsFor = shown.address;
        m_statsAt = ImGui::GetTime();
        m_statInterval = shown.intervalMs; m_statJitter = shown.jitterMs; m_statLate = shown.latePct;
    }
    shown.intervalMs = m_statInterval; shown.jitterMs = m_statJitter; shown.latePct = m_statLate;
    const DeviceView& v = shown;
    const AppSettings& s = m_settings;
    const DeviceProfile* prof = s.Find(v.address);
    const bool connected = v.state == DeviceState::Connected;

    // ---- identity card ----
    BeginCard("##identity", 18.f);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float g = Dp(60.f);
        const float y = ImGui::GetCursorPosY();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, {p.x + g, p.y + g}, Rgb(SURFACE_2), Dp(16.f));
        ControllerGlyph(dl, {p.x + g * 0.5f, p.y + g * 0.5f}, g * 0.74f, v.kind, connected ? KindColor(v.kind) : FAINT, connected);

        // Connection "ladder" in the top-right corner: link quality while
        // connected (report timing), signal strength while in sync mode.
        {
            const float sh = Dp(14.f), sw = sh * 1.2f;
            const float cardRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            const ImVec2 bl = {cardRight - sw, p.y + sh + Dp(2.f)};
            const int level = connected ? LinkLevel(v) : v.visible ? v.signal : 0;
            if (connected || v.visible) {
                SignalBars(dl, bl, sh, level, level >= 3 ? SUCCESS : level == 2 ? WARNING : level == 1 ? DANGER : FAINT);
                if (ImGui::IsMouseHoveringRect({bl.x - Dp(4.f), bl.y - sh - Dp(4.f)}, {bl.x + sw + Dp(4.f), bl.y + Dp(4.f)})) {
                    if (connected && level > 0)
                        LinkTooltip(v, level, level >= 3 ? SUCCESS : level == 2 ? WARNING : DANGER);
                    else if (connected)
                        ImGui::SetTooltip("%s", v.streaming == Streaming::OnChange
                            ? tr("This controller only reports on change: timing is not a quality measure.",
                                 "Este controle só envia quando algo muda: o tempo não mede a qualidade.")
                            : tr("Measuring the connection…", "Medindo a conexão…"));
                    else
                        ImGui::SetTooltip(tr("Signal: %s · %d dBm", "Sinal: %s · %d dBm"), SignalWord(level), int(v.rssi));
                }
            }
        }
        ImGui::Dummy({g, g});
        ImGui::SameLine(0, Dp(14.f));
        ImGui::SetCursorPosY(y - Dp(2.f));
        ImGui::BeginGroup();
        if (prof) {
            if (m_renameFor != v.address) {
                std::snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", v.name.c_str());
                m_renameFor = v.address;
            }
            // Inline rename: looks like a title. The whole row is the click
            // target, so hovering shows exactly that: a rounded field eases
            // out from the name to the full width of the row, and gets an
            // accent outline while typing.
            ImGui::PushFont(g_fonts.semibold, SIZE_TITLE - 2.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {Dp(6.f), Dp(3.f)});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Rgb4(0, 0.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Rgb4(0, 0.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Rgb4(0, 0.f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - Dp(6.f));
            const float fieldW = ImGui::GetContentRegionAvail().x - Dp(30.f);   // room for the connection ladder
            {
                ImGuiStorage* st = ImGui::GetStateStorage();
                const ImGuiID hid = ImGui::GetID("##namehover");
                const float ht = Animate(ImGui::GetID("##namehv"), st->GetInt(hid, 0) ? 1.f : 0.f, 12.f);
                const float at = Animate(ImGui::GetID("##nameact"), st->GetInt(hid, 0) == 2 ? 1.f : 0.f, 16.f);
                if (ht > 0.002f) {
                    if (ht < 0.998f) g_animating = true;
                    const float e = ht * ht * (3.f - 2.f * ht);   // ease in-out
                    const ImVec2 fp = ImGui::GetCursorScreenPos();
                    const float restW = std::min(fieldW, Dp(6.f) + ImGui::CalcTextSize(m_renameBuf).x + Dp(30.f));   // name + pencil
                    const ImVec2 fz = {fp.x + restW + (fieldW - restW) * e, fp.y + ImGui::GetFrameHeight()};
                    ImDrawList* fdl = ImGui::GetWindowDrawList();
                    fdl->AddRectFilled(fp, fz, Rgb(SURFACE_2, e), Dp(8.f));
                    fdl->AddRect(fp, fz, LerpColor(Rgb(BORDER, e), Rgb(ACCENT, 0.9f), at), Dp(8.f), 0, 1.f);
                }
            }
            ImGui::SetNextItemWidth(fieldW);
            ImGui::InputText("##name", m_renameBuf, sizeof(m_renameBuf));
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("##namehover"), ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0);
            if (ImGui::IsItemDeactivatedAfterEdit() && m_renameBuf[0]) {
                std::string n = m_renameBuf;
                m_dev.UpdateProfile(v.address, [n](DeviceProfile& dp) { dp.name = n; });
            }
            if (ImGui::IsItemDeactivated() && !m_renameBuf[0]) m_renameFor = 0;   // left empty: back to the saved name
            // Pencil after the name, so the rename is discoverable (the field
            // spans the row: clicking the pencil starts editing too).
            const bool editing = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
            const ImVec2 fmin = ImGui::GetItemRectMin(), fmax = ImGui::GetItemRectMax();
            const float nameW = ImGui::CalcTextSize(m_renameBuf).x;
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::PopFont();
            if (hovered && !editing) ImGui::SetTooltip("%s", tr("Click to rename", "Clique para renomear"));   // in the normal font, not while typing
            if (!editing) {
                ImGui::PushFont(g_fonts.regular, SIZE_SMALL - 2.f);   // about as tall as the name's capitals
                const ImVec2 is = ImGui::CalcTextSize(icon::Edit);
                const float ix = std::min(fmin.x + Dp(6.f) + nameW + Dp(8.f), fmax.x - is.x - Dp(4.f));
                ImGui::GetWindowDrawList()->AddText({ix, (fmin.y + fmax.y - is.y) * 0.5f + Dp(1.f)}, Rgb(hovered ? TEXT : MUTED), icon::Edit);
                ImGui::PopFont();
            }
        } else {
            ImGui::PushFont(g_fonts.semibold, SIZE_TITLE - 2.f);
            ImGui::TextUnformatted(v.name.c_str());
            ImGui::PopFont();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
        {
            // Model in strong text; the Bluetooth address as its own chip, so
            // the two read as different things (what it is / which one it is).
            // Clicking the chip copies the address.
            const std::string id = FormatAddress(v.address);
            ImGui::PushFont(g_fonts.regular, SIZE_SMALL - 1.f);
            const char* tag = "ID";
            const ImVec2 tagSize = ImGui::CalcTextSize(tag), idSize = ImGui::CalcTextSize(id.c_str());
            const float padX = Dp(7.f), gapIn = Dp(5.f), chipH = idSize.y + Dp(5.f);
            const float chipW = padX * 2.f + tagSize.x + gapIn + idSize.x;
            ImGui::PopFont();

            const float lineY = ImGui::GetCursorPosY() + Dp(2.f);
            ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(TEXT));
            ImGui::SetCursorPosY(lineY + (chipH - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextUnformatted(KindName(v.kind));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::PushFont(g_fonts.regular, SIZE_SMALL - 1.f);
            ImGui::SameLine(0, Dp(8.f));
            ImGui::SetCursorPosY(lineY);
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const bool copy = ImGui::InvisibleButton("##copyid", {chipW, chipH});
            const float hv = Animate(ImGui::GetID("##copyidhv"), ImGui::IsItemHovered() ? 1.f : 0.f, 16.f);
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            cdl->AddRectFilled(cp, {cp.x + chipW, cp.y + chipH}, LerpColor(Rgb(SURFACE_2), Rgb(BORDER), hv * 0.6f), chipH * 0.5f);
            cdl->AddRect(cp, {cp.x + chipW, cp.y + chipH}, Rgb(BORDER, 0.8f), chipH * 0.5f, 0, 1.f);
            const float ty = cp.y + (chipH - idSize.y) * 0.5f;
            cdl->AddText({cp.x + padX, ty}, Rgb(FAINT), tag);
            cdl->AddText({cp.x + padX + tagSize.x + gapIn, ty}, LerpColor(Rgb(MUTED), Rgb(TEXT), hv), id.c_str());
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s", tr("Bluetooth address of this controller · click to copy",
                                           "Endereço Bluetooth deste controle · clique para copiar"));
            }
            if (copy) {
                Click();
                ImGui::SetClipboardText(id.c_str());
                PushToast(tr("ID copied", "ID copiado"), SUCCESS, icon::Check);
            }
            ImGui::SetCursorPosY(lineY + chipH + Dp(9.f));
            ImGui::Dummy({0, 0});
            ImGui::SetCursorPosY(lineY + chipH + Dp(9.f));
        }
        if (v.onUsb || v.charging) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
            BatteryInline(-1, nullptr, /*charging*/ true);
        } else         if (v.batteryPct >= 0) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
            // Same explanation on the icon and on the text next to it.
            auto batteryTip = [&] {
                BatteryTooltip(v.batteryPct, FormatDuration(v.batteryMinutesLeft, false).c_str(), v.batteryMv,
                    v.batteryMeasured
                        ? tr("Remaining time based on how fast the battery drained during this session.",
                             "Tempo restante baseado no consumo medido nesta sessão.")
                        : tr("Remaining time based on Nintendo's rated battery life; it adapts after ~15 min of use.",
                             "Tempo restante baseado na autonomia oficial da Nintendo; se ajusta após ~15 min de uso."));
            };
            BatteryInline(v.batteryPct);
            if (ImGui::IsItemHovered()) batteryTip();
            ImGui::SameLine(0, Dp(8.f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Dp(1.f));
            const std::string left = FormatDuration(v.batteryMinutesLeft, /*compact*/ false);
            if (left.empty()) TextSmall(FAINT, tr("estimated · %.2f V", "estimativa · %.2f V"), v.batteryMv / 1000.f);
            else Segments({{tr("about ", "cerca de "), FAINT}, {left.c_str(), TEXT, true}, {tr(" left", " restantes"), FAINT}});
            if (ImGui::IsItemHovered()) batteryTip();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(6.f));
        if (connected) {
            char pl[8];
            std::snprintf(pl, sizeof(pl), "P%d", v.player);
            Segments({{tr("Connected as ", "Conectado como "), SUCCESS}, {pl, SUCCESS, true},
                      {v.combined ? tr(" · L+R pair", " · par L+R") : "", MUTED}});
        }
        else if (v.onUsb)
            TextSmall(MUTED, "%s", tr("Plugged in · charging. Unplug and press sync to play over Bluetooth.",
                                      "No cabo · carregando. Desplugue e aperte sync para jogar pelo Bluetooth."));
        else if (v.state == DeviceState::WaitingSync)
            TextSmall(WARNING, "%s", tr("Disconnected · press sync to bring it back", "Desconectado · aperte sync para voltar"));
        else
            TextSmall(FAINT, "%s", tr("Offline · press sync on the controller to connect", "Offline · aperte sync no controle para conectar"));
        ImGui::EndGroup();

        if (connected) {
            ImGui::Dummy({0, Dp(2.f)});
            const float hw = HalfWidth();
            if (Button(tr("Identify", "Identificar"), ButtonStyle::Secondary, {hw, 0})) { Click(); m_dev.Identify(v.address); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Plays a short cue on the controller so you know which one it is",
                                                                  "Toca um aviso curto no controle para você saber qual é"));
            ImGui::SameLine();
            if (Button(tr("Test rumble", "Testar vibração"), ButtonStyle::Secondary, {hw, 0})) { Click(); m_dev.TestRumble(v.address); }
        }
    }
    EndCard();

    // Unknown devices can't open this page; defaults keep --demo rows usable.
    const InputConfig in = prof ? prof->input : InputConfig{};

    // ---- mapping ----
    if (IsJoyCon(v.kind)) {
        SectionTitle(tr("USAGE", "USO"));
        BeginCard("##mapping", 18.f);
        if (v.combined) {
            TextSmall(MUTED, tr("This Joy-Con is combined with the other side into a single controller (P%d). "
                                "To use it alone, turn off \"Combine Joy-Cons\" in Settings.",
                                "Este Joy-Con está combinado com o outro lado como um único controle (P%d). "
                                "Para usá-lo sozinho, desligue \"Combinar Joy-Cons\" nas configurações."), v.player);
        } else {
            Label(tr("Orientation", "Posição"));
            const char* orient[] = {tr("Vertical", "Vertical"), tr("Horizontal", "Horizontal")};
            int o = in.mapping.orientation == Orientation::Upright ? 0 : 1;
            if (Segmented("##orient", &o, orient, 2)) {
                Click();
                m_dev.UpdateProfile(v.address, [o](DeviceProfile& dp) {
                    dp.input.mapping.orientation = o == 0 ? Orientation::Upright : Orientation::Sideways;
                });
            }
            if (in.mapping.orientation == Orientation::Sideways) {
                Divider();
                bool remap = in.mapping.remapSideways;
                if (ToggleRow("##remap", tr("Rotate buttons when horizontal", "Girar botões na horizontal"),
                              tr("Buttons follow the way you hold the controller.", "Os botões seguem a posição em que você segura o controle."), &remap)) {
                    Click();
                    m_dev.UpdateProfile(v.address, [remap](DeviceProfile& dp) { dp.input.mapping.remapSideways = remap; });
                }
            }
        }
        if (v.kind == ControllerKind::JoyConL) {
            Divider();
            bool dpad = in.mapping.dpadAsDpad;
            if (ToggleRow("##dpad", tr("Arrows as D-pad", "Setas como direcional"),
                          tr("When off, the arrows become the action buttons.", "Desligado, as setas viram os botões de ação."), &dpad)) {
                Click();
                m_dev.UpdateProfile(v.address, [dpad](DeviceProfile& dp) { dp.input.mapping.dpadAsDpad = dpad; });
            }
        }
        if (v.kind == ControllerKind::JoyConR && !v.combined) {
            Divider();
            bool mouse = in.mouseEnabled;
            if (ToggleRow("##mouse", tr("Optical mouse", "Mouse óptico"),
                          tr("The Chat button cycles off, fast, normal and slow. R clicks, ZR is the right button.",
                             "O botão Chat alterna: desligado, rápido, normal e lento. R clica, ZR é o botão direito."), &mouse)) {
                Click();
                m_dev.UpdateProfile(v.address, [mouse](DeviceProfile& dp) { dp.input.mouseEnabled = mouse; });
            }
        }
        EndCard();
    }

    if (v.kind == ControllerKind::ProController) {
        SectionTitle(tr("BACK BUTTONS", "BOTÕES TRASEIROS"));
        BeginCard("##glgr", 18.f);
        TextSmall(MUTED, "%s", tr("Choose which button GL and GR press.", "Escolha qual botão o GL e o GR acionam."));
        const int n = int(ButtonMapping::Count);
        auto combo = [&](const char* label, const char* id, ButtonMapping cur, bool isGL) {
            const float y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(y + Dp(6.f));
            ImGui::TextUnformatted(label);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
            ImGui::SetCursorPosY(y);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {Dp(12.f), Dp(8.f)});
            if (ImGui::BeginCombo(id, ButtonMappingName(cur))) {
                for (int i = 0; i < n; ++i) {
                    const auto m = static_cast<ButtonMapping>(i);
                    if (ImGui::Selectable(ButtonMappingName(m), m == cur)) {
                        Click();
                        m_dev.UpdateProfile(v.address, [m, isGL](DeviceProfile& dp) { (isGL ? dp.input.gl : dp.input.gr) = m; });
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
        };
        combo("GL", "##gl", in.gl, true);
        combo("GR", "##gr", in.gr, false);
        EndCard();
    }

    // ---- sticks ----
    SectionTitle(tr("STICKS", "ANALÓGICOS"));
    BeginCard("##sticks", 18.f);
    auto stickRow = [&](const char* label, bool left) {
        const StickCalibration& c = left ? in.calLeft : in.calRight;
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        const float y = ImGui::GetCursorPosY();
        const float bw = Dp(104.f);
        const float buttonsW = c.calibrated ? bw * 2 + Dp(8.f) : bw;
        ImGui::BeginGroup();
        ImGui::SetCursorPosY(y + Dp(1.f));
        ImGui::TextUnformatted(label);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
        if (c.calibrated) TextSmall(SUCCESS, "%s", tr("Calibrated", "Calibrado"));
        else              TextSmall(MUTED, "%s", tr("Default (not calibrated)", "Padrão (sem calibração)"));
        ImGui::EndGroup();
        const float rowH = std::max(ImGui::GetItemRectSize().y, Dp(BUTTON_H));
        ImGui::SameLine(right - buttonsW);
        ImGui::SetCursorPosY(y + (rowH - Dp(BUTTON_H)) * 0.5f);
        ImGui::PushID(left ? "l" : "r");
        if (c.calibrated) {
            if (Button(tr("Reset", "Restaurar"), ButtonStyle::Ghost, {bw, 0})) {
                Click();
                m_dev.UpdateProfile(v.address, [left](DeviceProfile& dp) { (left ? dp.input.calLeft : dp.input.calRight) = StickCalibration{}; });
            }
            ImGui::SameLine(0, Dp(8.f));
        }
        ImGui::BeginDisabled(!connected);
        if (Button(tr("Calibrate", "Calibrar"), ButtonStyle::Secondary, {bw, 0})) {
            Click();
            m_cal = Calibration{};
            m_cal.open = true;
            m_cal.address = v.address;
            m_cal.leftStick = left;
        }
        ImGui::EndDisabled();
        if (!connected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Connect the controller to calibrate", "Conecte o controle para calibrar"));
        ImGui::PopID();
        ImGui::SetCursorPosY(y + rowH);
        ImGui::Dummy({0, 0});
    };
    const bool hasLeft = v.kind != ControllerKind::JoyConR, hasRight = v.kind != ControllerKind::JoyConL;
    if (hasLeft)  stickRow(v.kind == ControllerKind::JoyConL ? tr("Stick", "Analógico") : tr("Left stick", "Analógico esquerdo"), true);
    if (hasLeft && hasRight) Divider();
    if (hasRight) stickRow(v.kind == ControllerKind::JoyConR ? tr("Stick", "Analógico") : tr("Right stick", "Analógico direito"), false);
    EndCard();

    if (connected) {
        SectionTitle(tr("LIVE TEST", "TESTE AO VIVO"));
        BeginCard("##live", 18.f);
        DrawLiveTest(v);
        EndCard();

    }

    // ---- gyroscope: per-controller switch, and its test while connected ----
    if (v.kind != ControllerKind::GameCube && (prof || connected)) {
        SectionTitle(tr("GYROSCOPE", "GIROSCÓPIO"));
        BeginCard("##gyro", 18.f);
        if (prof) {
            bool gyroOn = in.gyro;
            if (ToggleRow("##gyroon", tr("Send motion to games", "Enviar movimento para os jogos"),
                          IsJoyCon(v.kind)
                              ? tr("Off: games never get this controller's motion. In an L+R pair with both on, they get the average.",
                                   "Desligado: os jogos não recebem o movimento deste controle. Num par L+R com os dois ligados, recebem a média.")
                              : tr("Off: games never get this controller's motion.",
                                   "Desligado: os jogos não recebem o movimento deste controle."), &gyroOn)) {
                Click();
                m_dev.UpdateProfile(v.address, [gyroOn](DeviceProfile& dp) { dp.input.gyro = gyroOn; });
            }
        }
        if (connected) {
            if (prof) Divider();
            DrawGyroTest(v);
        }
        EndCard();
    }

    if (connected) {

        SectionTitle(tr("DIAGNOSTICS", "DIAGNÓSTICO"));
        BeginCard("##diag", 18.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, Dp(5.f)});
        char interval[24], jitter[24];
        std::snprintf(interval, sizeof(interval), "%.1f ms", v.intervalMs);
        std::snprintf(jitter, sizeof(jitter), "%.1f ms", v.jitterMs);
        const int link = LinkLevel(v);
        const unsigned linkColor = link >= 3 ? SUCCESS : link == 2 ? WARNING : link == 1 ? DANGER : MUTED;
        Segments({{tr("Connection: ", "Conexão: "), MUTED}, {link ? SignalWord(link) : tr("not measured", "não medida"), linkColor, true}});
        Segments({{tr("Packet interval: ", "Intervalo entre pacotes: "), MUTED}, {interval, TEXT, true},
                  {"   ·   ", FAINT}, {tr("Jitter: ", "Variação: "), MUTED}, {jitter, TEXT, true}});
        Segments({{tr("Data reporting: ", "Envio de dados: "), MUTED},
                  {v.streaming == Streaming::Continuous ? tr("continuous", "contínuo")
                 : v.streaming == Streaming::OnChange   ? tr("only on change", "só quando algo muda")
                                                        : tr("measuring…", "medindo…"), TEXT, true},
                  {v.streaming == Streaming::Continuous ? tr(" · silence release on", " · liberação por silêncio ativa")
                 : v.streaming == Streaming::OnChange   ? tr(" · released on disconnect", " · liberação pela desconexão") : "", FAINT}});
        ImGui::PopStyleVar();
        EndCard();
    }

    ImGui::Dummy({0, Dp(10.f)});
    const bool inSession = connected || v.state == DeviceState::WaitingSync;
    float forgetW = -1.f;
    if (inSession) {
        // End of play: drop the link (the controller goes to sleep) but keep it saved.
        const float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
        const bool sideBySide = avail >= Dp(400.f);
        const float w = sideBySide ? (avail - gap) * 0.5f : -1.f;
        if (Button(tr("Disconnect##end", "Desconectar##end"), ButtonStyle::Secondary, {w, 0}, icon::Power)) {
            Click();
            m_dev.Disconnect(v.address);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("Ends the session and lets the controller sleep. It stays saved: press sync to play again.",
                                       "Encerra a sessão e deixa o controle dormir. Ele continua salvo: aperte sync para jogar de novo."));
        if (sideBySide) { ImGui::SameLine(0, gap); forgetW = w; }
    }
    if (Button(inSession ? tr("Disconnect and forget", "Desconectar e esquecer") : tr("Forget controller", "Esquecer controle"),
               ButtonStyle::Danger, {forgetW, 0}, icon::Close)) {
        m_confirmForget = v.address;
        m_confirmName = v.name;
    }
}

// ===========================================================================
// Gyroscope test: this controller's own motion sensors
// ===========================================================================
// A small 3D model of the controller turns with the real one; next to it, one
// bar per rotation axis. The orientation comes from both sensors: the
// gyroscope gives the rotation, the accelerometer (gravity) keeps the tilt
// from drifting. Turning flat on a table has no such reference and drifts
// slowly: "Recenter" starts over.

namespace {
struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Unit(V3 a) { const float n = std::sqrt(Dot(a, a)); return n > 1e-6f ? a * (1.f / n) : V3{0.f, 0.f, 1.f}; }

struct Quat { float w, x, y, z; };
Quat Mul(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Quat Unit(Quat q) {
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return n > 1e-6f ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{1.f, 0.f, 0.f, 0.f};
}
V3 Rotate(Quat q, V3 v) {   // v' = q v q*
    const V3 u = {q.x, q.y, q.z};
    const V3 t = Cross(u, v) * 2.f;
    return v + t * q.w + Cross(u, t);
}
Quat Conj(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
// Shortest rotation taking unit vector a to unit vector b.
Quat Between(V3 a, V3 b) {
    const float d = Dot(a, b);
    if (d < -0.9999f) return {0.f, 1.f, 0.f, 0.f};   // opposite: half a turn about x
    const V3 c = Cross(a, b);
    return Unit(Quat{1.f + d, c.x, c.y, c.z});
}

// Model axes: x to the right, y away from the player, z out of the face with
// the buttons. The sensors are assumed to use the same axes; if a controller
// turns the wrong way on screen, this is the one place to remap them.
V3 SensorToModel(const int16_t s[3]) { return {float(s[0]), float(s[1]), float(s[2])}; }

constexpr float GYRO_RAD_PER_LSB = 0.0010653f;   // assumed 2000 deg/s full scale (0.061 deg/s per unit)
constexpr float GYRO_FULL = 12000.f;             // raw rotation that fills a bar
constexpr float GYRO_MOVING = 0.06f;             // of a bar: above this the axis counts as moving
constexpr float TILT_GAIN = 3.f;                 // how firmly gravity pulls the tilt back (1/s)

ImU32 Shade(unsigned hex, float k) {
    k = std::clamp(k, 0.f, 1.f);
    return IM_COL32(int(((hex >> 16) & 255) * k), int(((hex >> 8) & 255) * k), int((hex & 255) * k), 255);
}

// A generic gamepad: the outline seen from above (body and two grips, a
// closed smooth curve) extruded into a slab with rounded edges. Built once.
struct PadMesh {
    std::vector<V3> pos, normal;          // per vertex (smooth shading)
    struct Tri { int v[3]; int next[3]; bool flat; };   // next[k]: triangle across the edge v[k] -> v[k+1], or -1; flat: top/bottom face
    std::vector<Tri> tris;
    float top = 0.f;                      // height of the flat face with the buttons
};

const PadMesh& GamepadMesh() {
    static const PadMesh mesh = [] {
        PadMesh m;
        // Right half of the outline, from the middle of the far edge round to
        // the middle of the near edge (x right, y away from the player).
        static const ImVec2 half[] = {{0.00f, 0.50f}, {0.50f, 0.53f}, {0.88f, 0.44f}, {1.04f, 0.10f}, {1.03f, -0.42f},
                                      {0.90f, -0.80f}, {0.66f, -0.84f}, {0.50f, -0.58f}, {0.30f, -0.36f}, {0.00f, -0.31f}};
        std::vector<ImVec2> ctrl(std::begin(half), std::end(half));
        for (int k = int(std::size(half)) - 2; k >= 1; --k) ctrl.push_back({-half[k].x, half[k].y});
        // Closed Catmull-Rom curve through the control points.
        constexpr int STEPS = 4;
        std::vector<ImVec2> ring;
        const int cn = int(ctrl.size());
        for (int k = 0; k < cn; ++k) {
            const ImVec2 p0 = ctrl[(k + cn - 1) % cn], p1 = ctrl[k], p2 = ctrl[(k + 1) % cn], p3 = ctrl[(k + 2) % cn];
            for (int st = 0; st < STEPS; ++st) {
                const float t = st / float(STEPS), t2 = t * t, t3 = t2 * t;
                ring.push_back({0.5f * (2.f * p1.x + (p2.x - p0.x) * t + (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 + (3.f * p1.x - p0.x - 3.f * p2.x + p3.x) * t3),
                                0.5f * (2.f * p1.y + (p2.y - p0.y) * t + (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 + (3.f * p1.y - p0.y - 3.f * p2.y + p3.y) * t3)});
            }
        }
        const int n = int(ring.size());
        std::vector<ImVec2> out(n);   // outward direction at each outline point
        for (int k = 0; k < n; ++k) {
            const ImVec2 a = ring[(k + n - 1) % n], b = ring[(k + 1) % n];
            const float tx = b.x - a.x, ty = b.y - a.y, len = std::sqrt(tx * tx + ty * ty);
            out[k] = {-ty / len, tx / len};
        }
        // Layers from the bottom to the top: height and how far the outline is
        // pulled in (the rounded edge).
        static const float layer[][2] = {{-0.20f, 0.100f}, {-0.17f, 0.045f}, {-0.11f, 0.012f}, {-0.03f, 0.f},
                                         {0.03f, 0.f},     {0.11f, 0.012f},  {0.17f, 0.045f},  {0.20f, 0.100f}};
        const int layers = int(std::size(layer));
        m.top = layer[layers - 1][0];
        for (int l = 0; l < layers; ++l)
            for (int k = 0; k < n; ++k)
                m.pos.push_back({ring[k].x - out[k].x * layer[l][1], ring[k].y - out[k].y * layer[l][1], layer[l][0]});

        // Adds a triangle wound so that its normal points along `outward`.
        auto add = [&](int a, int b, int c, V3 outward, bool flat = false) {
            const V3 e1 = m.pos[b] + m.pos[a] * -1.f, e2 = m.pos[c] + m.pos[a] * -1.f;
            if (Dot(Cross(e1, e2), outward) < 0.f) std::swap(b, c);
            m.tris.push_back({{a, b, c}, {-1, -1, -1}, flat});
        };
        for (int l = 0; l + 1 < layers; ++l)
            for (int k = 0; k < n; ++k) {
                const int k2 = (k + 1) % n;
                const int a = l * n + k, b = l * n + k2, c = (l + 1) * n + k2, d = (l + 1) * n + k;
                const V3 o = {out[k].x + out[k2].x, out[k].y + out[k2].y, 0.f};
                add(a, b, c, o);
                add(a, c, d, o);
            }
        // Flat faces: the pulled-in outline, cut into triangles (ear clipping;
        // the outline is not convex because of the grips).
        {
            std::vector<int> idx(n);
            for (int k = 0; k < n; ++k) idx[k] = k;
            auto P = [&](int k) { return ImVec2{m.pos[k].x, m.pos[k].y}; };   // bottom layer = same outline as the top one
            auto cross = [](ImVec2 a, ImVec2 b, ImVec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
            float area = 0.f;
            for (int k = 0; k < n; ++k) area += P(k).x * P((k + 1) % n).y - P((k + 1) % n).x * P(k).y;
            const float sign = area < 0.f ? -1.f : 1.f;
            auto emit = [&](int a, int b, int c) {
                add(a, b, c, {0.f, 0.f, -1.f}, true);
                const int t = (layers - 1) * n;
                add(t + a, t + b, t + c, {0.f, 0.f, 1.f}, true);
            };
            while (idx.size() > 3) {
                bool cut = false;
                const int cnt = int(idx.size());
                for (int k = 0; k < cnt && !cut; ++k) {
                    const int ia = idx[(k + cnt - 1) % cnt], ib = idx[k], ic = idx[(k + 1) % cnt];
                    if (cross(P(ia), P(ib), P(ic)) * sign <= 1e-7f) continue;   // reflex corner
                    bool empty = true;
                    for (int other : idx) {
                        if (other == ia || other == ib || other == ic) continue;
                        const float d1 = cross(P(ia), P(ib), P(other)) * sign, d2 = cross(P(ib), P(ic), P(other)) * sign,
                                    d3 = cross(P(ic), P(ia), P(other)) * sign;
                        if (d1 >= 0.f && d2 >= 0.f && d3 >= 0.f) { empty = false; break; }
                    }
                    if (!empty) continue;
                    emit(ia, ib, ic);
                    idx.erase(idx.begin() + k);
                    cut = true;
                }
                if (!cut) break;   // degenerate: stop rather than loop
            }
            if (idx.size() == 3) emit(idx[0], idx[1], idx[2]);
        }
        // Smooth normals, and who is across each edge (for the outline).
        m.normal.assign(m.pos.size(), V3{0.f, 0.f, 0.f});
        std::map<std::pair<int, int>, std::pair<int, int>> edges;   // directed edge -> (triangle, corner)
        for (int t = 0; t < int(m.tris.size()); ++t) {
            const auto& tri = m.tris[t];
            const V3 fn = Cross(m.pos[tri.v[1]] + m.pos[tri.v[0]] * -1.f, m.pos[tri.v[2]] + m.pos[tri.v[0]] * -1.f);
            for (int k = 0; k < 3; ++k) {
                m.normal[tri.v[k]] = m.normal[tri.v[k]] + fn;
                edges[{tri.v[k], tri.v[(k + 1) % 3]}] = {t, k};
            }
        }
        for (V3& nrm : m.normal) nrm = Unit(nrm);
        for (int t = 0; t < int(m.tris.size()); ++t)
            for (int k = 0; k < 3; ++k)
                if (const auto it = edges.find({m.tris[t].v[(k + 1) % 3], m.tris[t].v[k]}); it != edges.end())
                    m.tris[t].next[k] = it->second.first;
        return m;
    }();
    return mesh;
}
} // namespace

void AppUI::DrawGyroTest(const DeviceView& v) {
    LiveInput li;
    if (!m_dev.LiveInputFor(v.address, li) || !li.hasMotion) {
        TextCentered(g_fonts.regular, SIZE_SMALL, MUTED,
                     tr("No motion data from this controller yet.", "Este controle ainda não enviou dados de movimento."));
        return;
    }

    // ---- orientation ----
    const V3 accel = SensorToModel(li.accel), gyroRaw = SensorToModel(li.gyro);
    const V3 up = Unit(accel);   // at rest the accelerometer points away from the ground
    const double nowT = ImGui::GetTime();
    Quat q = {m_gyroQ[0], m_gyroQ[1], m_gyroQ[2], m_gyroQ[3]};
    if (m_gyroFor != v.address) {
        q = Between(up, {0.f, 0.f, 1.f});   // start level with gravity, facing the screen
        m_gyroFor = v.address;
    } else {
        const float dt = std::clamp(float(nowT - m_gyroTime), 0.f, 0.05f);
        V3 w = gyroRaw * GYRO_RAD_PER_LSB;
        const V3 upEstimated = Rotate(Conj(q), {0.f, 0.f, 1.f});
        w = w + Cross(up, upEstimated) * TILT_GAIN;
        q = Unit(Mul(q, Quat{1.f, w.x * dt * 0.5f, w.y * dt * 0.5f, w.z * dt * 0.5f}));
    }
    m_gyroTime = nowT;
    m_gyroQ[0] = q.w; m_gyroQ[1] = q.x; m_gyroQ[2] = q.y; m_gyroQ[3] = q.z;
    g_animating = true;   // follows the sensors every frame

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float H = Dp(132.f), viewW = Dp(150.f);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::PushID("gyro");

    // ---- 3D model ----
    // One generic gamepad for every controller, drawn as line art: a flat
    // fill (it only hides what is behind), the outline, the rim of the face
    // and the sticks and buttons as thin strokes.
    const PadMesh& mesh = GamepadMesh();
    const ImU32 fillColor = Rgb(0x2C2E3A), lineColor = Rgb(0xE6E7EC), rimColor = Rgb(MUTED, 0.75f), markColor = Rgb(0xB9BCC8);
    const float lineW = Dp(1.5f), markW = Dp(1.2f);
    const float unit = Dp(47.f);

    // Camera in front of the player and above, looking down at the controller.
    const float elevation = 0.66f;   // ~38 degrees
    const float ce = std::cos(elevation), se = std::sin(elevation);
    const V3 toCamera = {0.f, -ce, se};
    constexpr float PERSPECTIVE = 0.14f;
    const V3 camera = toCamera * (1.f / PERSPECTIVE);
    const ImVec2 centre = {o.x + viewW * 0.5f, o.y + H * 0.5f - Dp(4.f)};
    auto project = [&](V3 world, float& depth) {
        depth = Dot(world, toCamera);
        const float k = unit / (1.f - depth * PERSPECTIVE);
        return ImVec2{centre.x + world.x * k, centre.y - (world.y * se + world.z * ce) * k};
    };

    dl->AddRectFilled(o, {o.x + viewW, o.y + H}, Rgb(SURFACE_2, 0.45f), Dp(12.f));
    // Floor shadow: where "level" is.
    {
        ImVec2 pts[28];
        for (int i = 0; i < 28; ++i) {
            const float t = i * 6.2831853f / 28.f;
            float depth;
            pts[i] = project({std::cos(t) * 1.05f, std::sin(t) * 0.80f - 0.10f, -0.95f}, depth);
        }
        dl->AddConvexPolyFilled(pts, 28, Rgb(0x000000, 0.16f));
    }

    const int vn = int(mesh.pos.size()), tn = int(mesh.tris.size());
    std::vector<V3> world(vn);
    std::vector<ImVec2> screen(vn);
    std::vector<float> depth(vn);
    for (int i = 0; i < vn; ++i) {
        world[i] = Rotate(q, mesh.pos[i]);
        screen[i] = project(world[i], depth[i]);
    }
    std::vector<char> front(tn);
    std::vector<std::pair<float, int>> order;
    for (int t = 0; t < tn; ++t) {
        const int* tv = mesh.tris[t].v;
        const V3 fn = Cross(world[tv[1]] + world[tv[0]] * -1.f, world[tv[2]] + world[tv[0]] * -1.f);
        front[t] = Dot(fn, camera + world[tv[0]] * -1.f) > 0.f;
        if (front[t]) order.push_back({depth[tv[0]] + depth[tv[1]] + depth[tv[2]], t});
    }
    std::sort(order.begin(), order.end());
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    for (const auto& [d, t] : order) {
        const auto& tri = mesh.tris[t];
        dl->PrimReserve(3, 3);
        const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteIdx(base); dl->PrimWriteIdx(ImDrawIdx(base + 1)); dl->PrimWriteIdx(ImDrawIdx(base + 2));
        for (int k = 0; k < 3; ++k) dl->PrimWriteVtx(screen[tri.v[k]], white, fillColor);
    }
    // Lines go on top of the whole fill (drawn in between, the next triangles
    // would eat into them). One that lies behind a nearer part of the body is
    // left out: its middle is tested against the triangles facing the camera.
    auto hidden = [&](ImVec2 m, float depthSum, int own, int ownOther) {
        for (const auto& [d, t] : order) {
            if (t == own || t == ownOther || d < depthSum + 0.25f) continue;   // not clearly nearer
            const int* tv = mesh.tris[t].v;
            const ImVec2 a = screen[tv[0]], b = screen[tv[1]], c = screen[tv[2]];
            const float d1 = (b.x - a.x) * (m.y - a.y) - (b.y - a.y) * (m.x - a.x);
            const float d2 = (c.x - b.x) * (m.y - b.y) - (c.y - b.y) * (m.x - b.x);
            const float d3 = (a.x - c.x) * (m.y - c.y) - (a.y - c.y) * (m.x - c.x);
            if ((d1 >= 0.f && d2 >= 0.f && d3 >= 0.f) || (d1 <= 0.f && d2 <= 0.f && d3 <= 0.f)) return true;
        }
        return false;
    };
    for (const auto& [d, t] : order) {
        const auto& tri = mesh.tris[t];
        for (int k = 0; k < 3; ++k) {
            const int other = tri.next[k];
            const bool outline = other < 0 || !front[other];
            const bool rim = !outline && tri.flat && !mesh.tris[other].flat;
            if (!outline && !rim) continue;
            const int ia = tri.v[k], ib = tri.v[(k + 1) % 3];
            const ImVec2 a = screen[ia], b = screen[ib];
            if (hidden({(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, (depth[ia] + depth[ib]) * 1.5f, t, other)) continue;
            dl->AddLine(a, b, outline ? lineColor : rimColor, outline ? lineW : Dp(1.f));
        }
    }
    // Sticks and buttons, stroked on the face while it looks at the camera.
    // (Not when the face is almost edge-on: the marks would be slivers.)
    if (Dot(Rotate(q, {0.f, 0.f, 1.f}), Unit(camera + Rotate(q, {0.f, 0.f, mesh.top}) * -1.f)) > 0.16f) {
        const float zf = mesh.top + 0.004f;
        auto ring = [&](float x, float y, float z, float r, bool filled) {
            ImVec2 pts[20];
            for (int i = 0; i < 20; ++i) {
                const float t = i * 6.2831853f / 20.f;
                float dp;
                pts[i] = project(Rotate(q, {x + std::cos(t) * r, y + std::sin(t) * r, z}), dp);
            }
            if (filled) dl->AddConvexPolyFilled(pts, 20, fillColor);
            dl->AddPolyline(pts, 20, markColor, ImDrawFlags_Closed, markW);
        };
        // Left stick (upper), right stick (lower): a well and a raised cap.
        for (const ImVec2 st : {ImVec2{-0.58f, 0.21f}, ImVec2{0.30f, -0.05f}}) {
            ring(st.x, st.y, zf, 0.175f, false);
            ring(st.x, st.y, zf + 0.10f, 0.125f, true);
        }
        // D-pad: the outline of a plus.
        {
            const float cx = -0.30f, cy = -0.05f, a = 0.050f, b = 0.145f;
            const float px[12] = {-a, a, a, b, b, a, a, -a, -a, -b, -b, -a};
            const float py[12] = {b, b, a, a, -a, -a, -b, -b, -a, -a, a, a};
            ImVec2 pts[12];
            for (int i = 0; i < 12; ++i) { float dp; pts[i] = project(Rotate(q, {cx + px[i], cy + py[i], zf}), dp); }
            dl->AddPolyline(pts, 12, markColor, ImDrawFlags_Closed, markW);
        }
        // Face buttons, and the two small buttons in the middle.
        for (const ImVec2 kb : {ImVec2{0.60f, 0.345f}, ImVec2{0.735f, 0.21f}, ImVec2{0.60f, 0.075f}, ImVec2{0.465f, 0.21f}})
            ring(kb.x, kb.y, zf, 0.058f, false);
        ring(-0.13f, 0.23f, zf, 0.030f, false);
        ring(0.13f, 0.23f, zf, 0.030f, false);
    }

    // ---- rotation bars (gyroscope) ----
    const float barsX = o.x + viewW + Dp(18.f);
    const float labelW = Dp(16.f), barH = Dp(8.f);
    const float barL = barsX + labelW, barR = o.x + avail - Dp(4.f), mid = (barL + barR) * 0.5f, half = (barR - barL) * 0.5f;
    const char* names[3] = {"X", "Y", "Z"};
    bool moving = false;
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    for (int i = 0; i < 3; ++i) {
        const float y = o.y + H * (0.24f + 0.26f * i);
        // Square-root scale: slow rotations stay visible, fast ones still fit.
        const float raw = std::clamp(li.gyro[i] / GYRO_FULL, -1.f, 1.f);
        const float target = (raw < 0.f ? -1.f : 1.f) * std::sqrt(std::abs(raw));
        const float val = Animate(ImGui::GetID(names[i]), target, 20.f);
        const bool on = std::abs(val) > GYRO_MOVING;
        moving |= on;
        const ImVec2 ts = ImGui::CalcTextSize(names[i]);
        dl->AddText({barsX, y - ts.y * 0.5f}, Rgb(on ? TEXT : MUTED), names[i]);
        dl->AddRectFilled({barL, y - barH * 0.5f}, {barR, y + barH * 0.5f}, Rgb(SURFACE_2), barH * 0.5f);
        const float x2 = mid + val * half;
        if (std::abs(x2 - mid) > 0.5f)
            dl->AddRectFilled({std::min(mid, x2), y - barH * 0.5f}, {std::max(mid, x2), y + barH * 0.5f}, Rgb(ACCENT), barH * 0.5f);
        dl->AddLine({mid, y - barH}, {mid, y + barH}, Rgb(MUTED, 0.7f), 1.f);
    }
    ImGui::PopFont();
    ImGui::PopID();

    ImGui::Dummy({avail, H});
    const float st = Animate(ImGui::GetID("##gyromoving"), moving ? 1.f : 0.f, 10.f);
    TextCentered(g_fonts.semibold, SIZE_SMALL, st > 0.5f ? SUCCESS : MUTED,
                 st > 0.5f ? tr("Moving", "Em movimento") : tr("Still", "Parado"));
    TextCentered(g_fonts.regular, SIZE_SMALL, FAINT,
                 tr("The model turns with the controller. Resting on a table, the bars return to the centre.",
                    "O modelo gira junto com o controle. Parado na mesa, as barras voltam para o centro."));
    ImGui::Dummy({0, Dp(2.f)});
    if (Button(tr("Recenter##gyro", "Recentrar##gyro"), ButtonStyle::Ghost, {Dp(120.f), Dp(30.f)})) {
        Click();
        m_gyroFor = 0;   // start over from gravity, facing the screen
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Turning sideways drifts slowly (the controller has no compass): this puts the model back facing you.",
                                   "O giro para os lados desvia aos poucos (o controle não tem bússola): isto recoloca o modelo de frente para você."));
    // Raw readings, for telling which axis is which.
    ImGui::SameLine(0, Dp(12.f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Dp(7.f));
    if (nowT - m_motionAt >= READOUT_EVERY) {   // twice a second: readable instead of flickering
        m_motionAt = nowT;
        for (int i = 0; i < 3; ++i) { m_motionShown[i] = li.accel[i]; m_motionShown[3 + i] = li.gyro[i]; }
    }
    TextSmall(FAINT, tr("accel %d %d %d · gyro %d %d %d", "acel %d %d %d · giro %d %d %d"),
              m_motionShown[0], m_motionShown[1], m_motionShown[2], m_motionShown[3], m_motionShown[4], m_motionShown[5]);
}

// ===========================================================================
// Live test: a controller diagram with Switch button names
// ===========================================================================

void AppUI::DrawLiveTest(const DeviceView& v) {
    LiveInput li;
    if (!m_dev.LiveInputFor(v.address, li) || !li.valid) {
        TextCentered(g_fonts.regular, SIZE_SMALL, MUTED, tr("Move the controller to see it here.", "Mexa no controle para ver os comandos aqui."));
        return;
    }
    const auto& r = li.report.Report;
    const USHORT b = r.wButtons;
    const int dpad = b & 0xF;
    const bool dUp = dpad == 0 || dpad == 1 || dpad == 7, dRight = dpad >= 1 && dpad <= 3;
    const bool dDown = dpad >= 3 && dpad <= 5, dLeft = dpad >= 5 && dpad <= 7;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float W = std::min(avail, Dp(440.f));
    const float H = Dp(222.f);
    const ImVec2 o = {ImGui::GetCursorScreenPos().x + (avail - W) * 0.5f, ImGui::GetCursorScreenPos().y};
    auto P = [&](float fx, float py) { return ImVec2{o.x + W * fx, o.y + Dp(py)}; };

    ImGui::PushID("live");
    auto ease = [&](const char* id, bool on) { return Animate(ImGui::GetID(id), on ? 1.f : 0.f, 22.f); };
    auto keyColors = [&](float t, ImU32& fill, ImU32& text, ImU32& border) {
        fill = LerpColor(Rgb(SURFACE_2), Rgb(ACCENT), t);
        text = LerpColor(Rgb(MUTED), Rgb(0xFFFFFF), t);
        border = LerpColor(Rgb(BORDER), Rgb(ACCENT_HI), t);
    };
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    auto centeredText = [&](ImVec2 c, ImU32 col, const char* s) {
        const ImVec2 ts = ImGui::CalcTextSize(s);
        dl->AddText({c.x - ts.x * 0.5f, c.y - ts.y * 0.5f}, col, s);
    };
    // Rounded pill key; `analog` (0..1) fills it from the left (triggers).
    auto pill = [&](const char* id, ImVec2 c, float w, float h, const char* label, bool on, float analog = -1.f) {
        const float t = ease(id, on);
        ImU32 fill, text, border; keyColors(t, fill, text, border);
        const ImVec2 a = {c.x - w * 0.5f, c.y - h * 0.5f}, z = {c.x + w * 0.5f, c.y + h * 0.5f};
        dl->AddRectFilled(a, z, Rgb(SURFACE_2), h * 0.5f);
        if (analog > 0.01f && t < 1.f) {
            // Analog level: the pill shape clipped at the current value.
            dl->PushClipRect(a, {a.x + w * analog, z.y}, true);
            dl->AddRectFilled(a, z, Rgb(ACCENT, 0.45f), h * 0.5f);
            dl->PopClipRect();
        }
        dl->AddRectFilled(a, z, LerpColor(Rgb(SURFACE_2, 0.f), fill, t), h * 0.5f);
        dl->AddRect(a, z, border, h * 0.5f, 0, 1.f);
        centeredText(c, text, label);
    };
    auto round = [&](const char* id, ImVec2 c, float rad, const char* label, bool on) {
        const float t = ease(id, on);
        ImU32 fill, text, border; keyColors(t, fill, text, border);
        dl->AddCircleFilled(c, rad, fill, 28);
        dl->AddCircle(c, rad, border, 28, 1.f);
        if (label) centeredText(c, text, label);
    };
    auto square = [&](const char* id, ImVec2 c, float s, bool on, int arrow /*0 none,1 up,2 right,3 down,4 left*/) {
        const float t = ease(id, on);
        ImU32 fill, text, border; keyColors(t, fill, text, border);
        dl->AddRectFilled({c.x - s * 0.5f, c.y - s * 0.5f}, {c.x + s * 0.5f, c.y + s * 0.5f}, fill, Dp(5.f));
        dl->AddRect({c.x - s * 0.5f, c.y - s * 0.5f}, {c.x + s * 0.5f, c.y + s * 0.5f}, border, Dp(5.f), 0, 1.f);
        const float k = s * 0.2f;
        switch (arrow) {
            case 1: dl->AddTriangleFilled({c.x, c.y - k}, {c.x - k, c.y + k * 0.6f}, {c.x + k, c.y + k * 0.6f}, text); break;
            case 2: dl->AddTriangleFilled({c.x + k, c.y}, {c.x - k * 0.6f, c.y - k}, {c.x - k * 0.6f, c.y + k}, text); break;
            case 3: dl->AddTriangleFilled({c.x, c.y + k}, {c.x - k, c.y - k * 0.6f}, {c.x + k, c.y - k * 0.6f}, text); break;
            case 4: dl->AddTriangleFilled({c.x - k, c.y}, {c.x + k * 0.6f, c.y - k}, {c.x + k * 0.6f, c.y + k}, text); break;
            default: break;
        }
    };
    auto stick = [&](const char* id, ImVec2 c, float rad, BYTE x, BYTE y, bool pressed) {
        const float t = ease(id, pressed);
        dl->AddCircleFilled(c, rad, Rgb(SURFACE_2), 40);
        dl->AddCircle(c, rad, LerpColor(Rgb(BORDER), Rgb(ACCENT_HI), t), 40, 1.f + t);
        dl->AddCircle(c, rad * 0.5f, Rgb(BORDER, 0.5f), 32, 1.f);
        // Position: stick values are already limited to a circle; clamp anyway
        // so the dot never leaves the ring.
        float fx = (x - 128) / 127.f, fy = (y - 128) / 127.f;
        const float len = std::sqrt(fx * fx + fy * fy);
        if (len > 1.f) { fx /= len; fy /= len; }
        const float travel = rad - Dp(8.f);
        const ImVec2 d = {c.x + fx * travel, c.y + fy * travel};
        dl->AddLine(c, d, Rgb(ACCENT, 0.35f), Dp(2.f));
        dl->AddCircleFilled(d, Dp(7.f) + t * Dp(1.5f), LerpColor(Rgb(ACCENT), Rgb(ACCENT_HI), t), 24);
    };

    // Shoulders
    const float pw = Dp(54.f), ph = Dp(24.f);
    pill("zl", P(0.11f, 18.f), pw, ph, "ZL", (b & DS4_BUTTON_TRIGGER_LEFT) != 0, r.bTriggerL / 255.f);
    pill("l",  P(0.26f, 18.f), pw, ph, "L",  (b & DS4_BUTTON_SHOULDER_LEFT) != 0);
    pill("r",  P(0.74f, 18.f), pw, ph, "R",  (b & DS4_BUTTON_SHOULDER_RIGHT) != 0);
    pill("zr", P(0.89f, 18.f), pw, ph, "ZR", (b & DS4_BUTTON_TRIGGER_RIGHT) != 0, r.bTriggerR / 255.f);

    // Left stick, D-pad
    const float sr = Dp(31.f);
    stick("ls", P(0.20f, 96.f), sr, r.bThumbLX, r.bThumbLY, (b & DS4_BUTTON_THUMB_LEFT) != 0);
    const ImVec2 dc = P(0.34f, 178.f);
    const float ds = Dp(21.f), doff = Dp(22.f);
    square("du", {dc.x, dc.y - doff}, ds, dpad != 8 && dUp, 1);
    square("dr", {dc.x + doff, dc.y}, ds, dpad != 8 && dRight, 2);
    square("dd", {dc.x, dc.y + doff}, ds, dpad != 8 && dDown, 3);
    square("dl", {dc.x - doff, dc.y}, ds, dpad != 8 && dLeft, 4);

    // Centre: − + Capture Home
    pill("minus", P(0.40f, 78.f), Dp(32.f), Dp(20.f), "−", (b & DS4_BUTTON_SHARE) != 0);
    pill("plus",  P(0.60f, 78.f), Dp(32.f), Dp(20.f), "+", (b & DS4_BUTTON_OPTIONS) != 0);
    {
        const ImVec2 cc = P(0.44f, 114.f);
        const float t = ease("cap", (r.bSpecial & DS4_SPECIAL_BUTTON_TOUCHPAD) != 0);
        ImU32 fill, text, border; keyColors(t, fill, text, border);
        const float s = Dp(18.f);
        dl->AddRectFilled({cc.x - s * 0.5f, cc.y - s * 0.5f}, {cc.x + s * 0.5f, cc.y + s * 0.5f}, fill, Dp(4.f));
        dl->AddRect({cc.x - s * 0.5f, cc.y - s * 0.5f}, {cc.x + s * 0.5f, cc.y + s * 0.5f}, border, Dp(4.f), 0, 1.f);
        dl->AddCircleFilled(cc, s * 0.2f, text);
    }
    round("home", P(0.56f, 114.f), Dp(11.f), nullptr, (r.bSpecial & DS4_SPECIAL_BUTTON_PS) != 0);
    {
        // House glyph inside Home.
        const ImVec2 hc = P(0.56f, 114.f);
        const float t = ease("home", (r.bSpecial & DS4_SPECIAL_BUTTON_PS) != 0);
        const ImU32 col = LerpColor(Rgb(MUTED), Rgb(0xFFFFFF), t);
        const float k = Dp(5.f);
        dl->AddTriangleFilled({hc.x, hc.y - k}, {hc.x - k, hc.y}, {hc.x + k, hc.y}, col);
        dl->AddRectFilled({hc.x - k * 0.65f, hc.y - Dp(0.5f)}, {hc.x + k * 0.65f, hc.y + k * 0.8f}, col);
    }

    // Face buttons (Nintendo layout: X top, A right, B bottom, Y left)
    const ImVec2 fc = P(0.80f, 96.f);
    const float fr = Dp(14.f), foff = Dp(25.f);
    round("x", {fc.x, fc.y - foff}, fr, "X", (b & DS4_BUTTON_TRIANGLE) != 0);
    round("a", {fc.x + foff, fc.y}, fr, "A", (b & DS4_BUTTON_CIRCLE) != 0);
    round("b", {fc.x, fc.y + foff}, fr, "B", (b & DS4_BUTTON_CROSS) != 0);
    round("y", {fc.x - foff, fc.y}, fr, "Y", (b & DS4_BUTTON_SQUARE) != 0);

    // Right stick
    stick("rs", P(0.66f, 178.f), sr, r.bThumbRX, r.bThumbRY, (b & DS4_BUTTON_THUMB_RIGHT) != 0);

    // Back buttons (Pro Controller 2), at the grips. They show the physical
    // press; whatever they are mapped to lights up as well.
    if (li.hasBack) {
        pill("gl", P(0.11f, 186.f), pw, ph, "GL", li.gl);
        pill("gr", P(0.89f, 186.f), pw, ph, "GR", li.gr);
    }

    // Tiny captions under the centre keys.
    ImGui::PopFont();
    ImGui::PushFont(g_fonts.regular, SIZE_SMALL - 2.f);
    centeredText(P(0.44f, 134.f), Rgb(FAINT), "Capture");
    centeredText(P(0.56f, 134.f), Rgb(FAINT), "Home");
    ImGui::PopFont();
    ImGui::PopID();

    ImGui::Dummy({avail, H});
    TextCentered(g_fonts.regular, SIZE_SMALL, FAINT,
                 tr("Hold a button still: it should stay lit.", "Segure um botão parado: ele deve continuar aceso."));
}


} // namespace sw2
