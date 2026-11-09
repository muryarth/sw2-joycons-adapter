// Modal dialogs: stick calibration wizard and disconnect confirmation.

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
// Calibration wizard
// ===========================================================================

void AppUI::DrawCalibration() {
    if (!m_cal.open) return;
    const char* popupId = "##calibrate";
    if (!ImGui::IsPopupOpen(popupId)) ImGui::OpenPopup(popupId);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(Dp(420.f), vp->WorkSize.x - Dp(32.f)), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {Dp(24.f), Dp(22.f)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Dp(16.f));
    if (ImGui::BeginPopupModal(popupId, nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        LiveInput li;
        const bool live = m_dev.LiveInputFor(m_cal.address, li);
        const int rx = m_cal.leftStick ? li.rawLX : li.rawRX;
        const int ry = m_cal.leftStick ? li.rawLY : li.rawRY;
        if (live && m_cal.step == 1) {
            m_cal.minX = std::min(m_cal.minX, rx); m_cal.maxX = std::max(m_cal.maxX, rx);
            m_cal.minY = std::min(m_cal.minY, ry); m_cal.maxY = std::max(m_cal.maxY, ry);
            ++m_cal.samples;
        }

        TextCentered(g_fonts.semibold, SIZE_TITLE - 2.f, TEXT, tr("Calibrate stick", "Calibrar analógico"));
        char step[48];
        std::snprintf(step, sizeof(step), tr("Step %d of 2", "Passo %d de 2"), std::min(m_cal.step + 1, 2));
        TextCentered(g_fonts.regular, SIZE_SMALL, MUTED, step);
        ImGui::Dummy({0, Dp(4.f)});

        // Raw stick inside the 12-bit range, drawn as a round gate.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float box = Dp(156.f);
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 o = {p.x + (w - box) * 0.5f, p.y};
        const ImVec2 c = {o.x + box * 0.5f, o.y + box * 0.5f};
        auto map = [&](int x, int y) { return ImVec2{o.x + x / 4095.f * box, o.y + (1.f - y / 4095.f) * box}; };
        dl->AddCircleFilled(c, box * 0.5f, Rgb(SURFACE_2), 64);
        dl->AddCircle(c, box * 0.5f, Rgb(BORDER), 64, 1.f);
        dl->AddLine({c.x, o.y + Dp(6.f)}, {c.x, o.y + box - Dp(6.f)}, Rgb(BORDER, 0.6f));
        dl->AddLine({o.x + Dp(6.f), c.y}, {o.x + box - Dp(6.f), c.y}, Rgb(BORDER, 0.6f));
        if (m_cal.step >= 1 && m_cal.maxX > m_cal.minX)
            dl->AddRect(map(m_cal.minX, m_cal.maxY), map(m_cal.maxX, m_cal.minY), Rgb(SUCCESS, 0.8f), Dp(10.f), 0, Dp(1.5f));
        if (m_cal.step >= 1) dl->AddCircleFilled(map(m_cal.cx, m_cal.cy), Dp(3.f), Rgb(MUTED));
        if (live) dl->AddCircleFilled(map(rx, ry), Dp(7.f), Rgb(ACCENT), 24);
        ImGui::Dummy({w, box + Dp(10.f)});

        const float hw = HalfWidth();
        if (!live) {
            TextCentered(g_fonts.regular, SIZE_SMALL, DANGER, tr("The controller disconnected. Reconnect it to calibrate.",
                                                                 "O controle desconectou. Conecte-o novamente para calibrar."));
            ImGui::Dummy({0, Dp(4.f)});
            if (Button(tr("Close", "Fechar"), ButtonStyle::Secondary, {-1.f, 0})) { m_cal.open = false; ImGui::CloseCurrentPopup(); }
        } else if (m_cal.step == 0) {
            TextCentered(g_fonts.regular, SIZE_SMALL + 0.5f, TEXT, tr("Let go of the stick and leave it centred.", "Solte o analógico e deixe-o parado no centro."));
            ImGui::Dummy({0, Dp(6.f)});
            if (Button(tr("Cancel", "Cancelar"), ButtonStyle::Ghost, {hw, 0})) { m_cal.open = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (Button(tr("Capture centre", "Capturar centro"), ButtonStyle::Primary, {hw, 0})) {
                Click();
                m_cal.cx = rx; m_cal.cy = ry;
                m_cal.minX = m_cal.maxX = rx; m_cal.minY = m_cal.maxY = ry;
                m_cal.samples = 0;
                m_cal.step = 1;
            }
        } else {
            TextCentered(g_fonts.regular, SIZE_SMALL + 0.5f, TEXT, tr("Slowly rotate the stick along the edge a few times.", "Gire o analógico devagar, encostando nas bordas, algumas vezes."));
            const bool enough = m_cal.samples > 60 && m_cal.maxX - m_cal.minX > 1200 && m_cal.maxY - m_cal.minY > 1200;
            TextCentered(g_fonts.regular, SIZE_SMALL, enough ? SUCCESS : MUTED, enough ? tr("Done. You can finish.", "Pronto. Pode concluir.")
                                                                                         : tr("Keep rotating…", "Continue girando…"));
            ImGui::Dummy({0, Dp(6.f)});
            const float tw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.f;
            if (Button(tr("Cancel", "Cancelar"), ButtonStyle::Ghost, {tw, 0})) { m_cal.open = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (Button(tr("Restart", "Recomeçar"), ButtonStyle::Secondary, {tw, 0})) { Click(); m_cal.step = 0; }
            ImGui::SameLine();
            ImGui::BeginDisabled(!enough);
            if (Button(tr("Finish", "Concluir"), ButtonStyle::Primary, {tw, 0})) {
                Click();
                StickCalibration cal;
                cal.calibrated = true;
                cal.centerX = m_cal.cx; cal.centerY = m_cal.cy;
                cal.minX = m_cal.minX; cal.maxX = m_cal.maxX;
                cal.minY = m_cal.minY; cal.maxY = m_cal.maxY;
                const bool left = m_cal.leftStick;
                m_dev.UpdateProfile(m_cal.address, [cal, left](DeviceProfile& dp) { (left ? dp.input.calLeft : dp.input.calRight) = cal; });
                PushToast(tr("Stick calibrated", "Analógico calibrado"), SUCCESS, icon::Check);
                m_cal.open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// ===========================================================================
// Confirmation
// ===========================================================================

void AppUI::DrawConfirm() {
    if (!m_confirmForget) return;
    const char* popupId = "##confirm";
    if (!ImGui::IsPopupOpen(popupId)) ImGui::OpenPopup(popupId);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(Dp(380.f), vp->WorkSize.x - Dp(32.f)), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {Dp(24.f), Dp(22.f)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Dp(16.f));
    if (ImGui::BeginPopupModal(popupId, nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        // Red badge on top.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x, bsz = Dp(44.f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 c = {p.x + w * 0.5f, p.y + bsz * 0.5f};
        dl->AddCircleFilled(c, bsz * 0.5f, Rgb(DANGER, 0.15f));
        DrawIconCentered(dl, c, 16.f, Rgb(DANGER), icon::Close);
        ImGui::Dummy({w, bsz + Dp(4.f)});

        char title[160];
        std::snprintf(title, sizeof(title), tr("Disconnect and forget %s?", "Desconectar e esquecer %s?"), m_confirmName.c_str());
        TextCentered(g_fonts.semibold, SIZE_BODY + 2.f, TEXT, title);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::PushFont(nullptr, SIZE_SMALL);
        const char* body = tr("It leaves the known controllers list and will no longer be searched for. "
                              "Its pairing with your Switch 2 stays intact.",
                              "Ele sai da lista de controles conhecidos e não será mais procurado. "
                              "O pareamento com o seu Switch 2 continua intacto.");
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(MUTED));
        ImGui::TextWrapped("%s", body);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        ImGui::Dummy({0, Dp(8.f)});
        const float hw = HalfWidth();
        if (Button(tr("Cancel", "Cancelar"), ButtonStyle::Secondary, {hw, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_confirmForget = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (Button(tr("Disconnect", "Desconectar"), ButtonStyle::Danger, {hw, 0})) {
            m_dev.DisconnectAndForget(m_confirmForget);
            if (m_page == Page::Device && m_selected == m_confirmForget) Navigate(Page::Home);
            m_confirmForget = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}


} // namespace sw2
