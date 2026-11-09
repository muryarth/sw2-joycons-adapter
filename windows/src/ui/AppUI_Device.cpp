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
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sw2 {

using namespace theme;
using namespace ui;

// ===========================================================================
// Device page
// ===========================================================================

void AppUI::DrawDevicePage(const std::vector<DeviceView>& devices) {
    const DeviceView* found = nullptr;
    for (auto& d : devices) if (d.address == m_selected) found = &d;
    if (!found) { Navigate(Page::Home); return; }
    const DeviceView& v = *found;
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
        ControllerGlyph(dl, {p.x + g * 0.5f, p.y + g * 0.5f}, g * 0.74f, v.kind, connected ? ACCENT : FAINT, connected);
        ImGui::Dummy({g, g});
        ImGui::SameLine(0, Dp(14.f));
        ImGui::SetCursorPosY(y - Dp(2.f));
        ImGui::BeginGroup();
        if (prof) {
            if (m_renameFor != v.address) {
                std::snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", v.name.c_str());
                m_renameFor = v.address;
            }
            // Inline rename: looks like a title, reveals a field on hover/focus.
            ImGui::PushFont(g_fonts.semibold, SIZE_TITLE - 2.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {Dp(6.f), Dp(3.f)});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Rgb4(0, 0.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Rgb4(SURFACE_2));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Rgb4(SURFACE_2));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - Dp(6.f));
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##name", m_renameBuf, sizeof(m_renameBuf));
            if (ImGui::IsItemDeactivatedAfterEdit() && m_renameBuf[0]) {
                std::string n = m_renameBuf;
                m_dev.UpdateProfile(v.address, [n](DeviceProfile& dp) { dp.name = n; });
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Click to rename", "Clique para renomear"));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::PopFont();
        } else {
            ImGui::PushFont(g_fonts.semibold, SIZE_TITLE - 2.f);
            ImGui::TextUnformatted(v.name.c_str());
            ImGui::PopFont();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
        TextSmall(MUTED, "%s · %s", KindName(v.kind), FormatAddress(v.address).c_str());
        if (v.onUsb || v.charging) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
            BatteryInline(-1, nullptr, /*charging*/ true);
        } else         if (v.batteryPct >= 0) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
            BatteryInline(v.batteryPct);
            ImGui::SameLine(0, Dp(6.f));
            const std::string left = FormatDuration(v.batteryMinutesLeft, /*compact*/ false);
            if (left.empty()) TextSmall(FAINT, tr("estimated · %.2f V", "estimativa · %.2f V"), v.batteryMv / 1000.f);
            else              TextSmall(FAINT, tr("about %s left", "cerca de %s restantes"), left.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%.2f V", v.batteryMeasured
                    ? tr("Estimate based on how fast the battery drained during this session.",
                         "Estimativa baseada no consumo medido nesta sessão.")
                    : tr("Estimate based on Nintendo's rated battery life; it adapts after ~15 min of use.",
                         "Estimativa baseada na autonomia oficial da Nintendo; se ajusta após ~15 min de uso."),
                    v.batteryMv / 1000.f);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(6.f));
        if (connected)
            TextSmall(SUCCESS, "%s P%d%s", tr("Connected as", "Conectado como"), v.player, v.combined ? tr(" (L+R pair)", " (par L+R)") : "");
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

        SectionTitle(tr("DIAGNOSTICS", "DIAGNÓSTICO"));
        BeginCard("##diag", 18.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, Dp(5.f)});
        TextSmall(MUTED, "%s: %.1f ms   ·   %s: %.1f ms", tr("Packet interval", "Intervalo entre pacotes"), v.intervalMs,
                  tr("Jitter", "Variação"), v.jitterMs);
        TextSmall(MUTED, "%s: %s", tr("Signal", "Sinal"), v.rssi ? (std::to_string(v.rssi) + " dBm").c_str() : "—");
        TextSmall(MUTED, "%s: %s", tr("Data reporting", "Envio de dados"),
                  v.streaming == Streaming::Continuous ? tr("continuous (silence release on)", "contínuo (liberação por silêncio ativa)")
                : v.streaming == Streaming::OnChange ? tr("only on change (released on disconnect)", "só quando algo muda (liberação pela desconexão)")
                                   : tr("measuring…", "medindo…"));
        ImGui::PopStyleVar();
        EndCard();
    }

    ImGui::Dummy({0, Dp(10.f)});
    if (Button(connected || v.state == DeviceState::WaitingSync ? tr("Disconnect and forget", "Desconectar e esquecer")
                                                                : tr("Forget controller", "Esquecer controle"),
               ButtonStyle::Danger, {-1.f, 0}, icon::Close)) {
        m_confirmForget = v.address;
        m_confirmName = v.name;
    }
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
