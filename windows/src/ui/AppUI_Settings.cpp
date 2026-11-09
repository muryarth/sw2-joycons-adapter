// Settings page.

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
// Settings
// ===========================================================================

void AppUI::DrawSettingsPage() {
    AppSettings s = m_settings;   // editable copy: toggles write here, then UpdateSettings applies
    auto apply = [&](auto fn) { Click(); m_dev.UpdateSettings(fn); };

    SectionTitle(tr("GENERAL", "GERAL"), true);
    BeginCard("##general", 18.f);
    Label(tr("Language", "Idioma"));
    const char* langs[] = {"English", "Português"};
    int lang = s.language;
    if (Segmented("##lang", &lang, langs, 2)) apply([lang](AppSettings& a) { a.language = lang; });
    Divider();
    if (ToggleRow("##auto", tr("Auto search", "Buscar automaticamente"),
                  tr("While the app is open, listens for controllers in sync mode and connects the known ones.",
                     "Com o app aberto, procura controles em modo sync e conecta os conhecidos."), &s.autoSearch))
        apply([v = s.autoSearch](AppSettings& a) { a.autoSearch = v; });
    Divider();
    if (ToggleRow("##combine", tr("Combine Joy-Cons", "Combinar Joy-Cons"),
                  tr("A left and a right Joy-Con become a single controller.", "Um Joy-Con L e um R viram um único controle."), &s.combineJoyCons))
        apply([v = s.combineJoyCons](AppSettings& a) { a.combineJoyCons = v; });
    Divider();
    if (ToggleRow("##swap", tr("Swap A/B and X/Y", "Inverter A/B e X/Y"),
                  tr("For games that expect the Xbox layout.", "Para jogos que esperam o layout do Xbox."), &s.swapABXY))
        apply([v = s.swapABXY](AppSettings& a) { a.swapABXY = v; });
    Divider();
    Label(tr("Stick deadzone", "Zona morta dos analógicos"));
    int dz = int(std::lround(s.deadzone * 100.f));
    if (Slider("##dz", &dz, 0, 30, "%d%%", tr("none", "nenhuma"))) {
        const float v = dz / 100.f;
        m_dev.UpdateSettings([v](AppSettings& a) { a.deadzone = v; });
    }
    Divider();
    Label(tr("Gyro of a Joy-Con pair", "Giroscópio do par de Joy-Cons"));
    const char* gyro[] = {tr("Average", "Média"), tr("Left", "Esquerdo"), tr("Right", "Direito")};
    int g = int(s.gyroSource);
    if (Segmented("##gyro", &g, gyro, 3)) apply([g](AppSettings& a) { a.gyroSource = static_cast<GyroSource>(g); });
    EndCard();

    SectionTitle(tr("SOUNDS", "SONS"));
    BeginCard("##sounds", 18.f);
    if (ToggleRow("##snd", tr("Status sounds", "Sons de status"),
                  tr("Short cues when controllers are found, connected or lost.", "Avisos curtos ao encontrar, conectar e perder controles."), &s.sounds))
        apply([v = s.sounds](AppSettings& a) { a.sounds = v; });
    ImGui::BeginDisabled(!s.sounds);
    int vol = int(std::lround(s.volume * 100.f));
    const float y = ImGui::GetCursorPosY();
    if (IconButton("##preview", s.volume > 0.f ? icon::Volume : icon::Mute, tr("Play", "Ouvir"), MUTED, 28.f)) {
        m_sounds.SetVolume(s.volume);
        m_sounds.Play(Sound::Connected);
    }
    ImGui::SameLine(0, Dp(8.f));
    ImGui::SetCursorPosY(y + Dp(2.f));
    if (Slider("##vol", &vol, 0, 100, "%d%%", tr("muted", "mudo"))) {
        const float v = vol / 100.f;
        m_dev.UpdateSettings([v](AppSettings& a) { a.volume = v; });
    }
    ImGui::EndDisabled();
    EndCard();

    SectionTitle(tr("STEAM COMPATIBILITY", "COMPATIBILIDADE COM A STEAM"));
    BeginCard("##steam", 18.f);
    Label(tr("Update rate", "Taxa de atualização"));
    const char* pol[] = {tr("Immediate", "Imediata"), "120 Hz", "60 Hz"};
    int pv = int(s.updatePolicy);
    if (Segmented("##policy", &pv, pol, 3)) apply([pv](AppSettings& a) { a.updatePolicy = static_cast<UpdatePolicy>(pv); });
    Divider();
    Label(tr("Release inputs after no signal", "Soltar comandos sem sinal após"));
    int wd = s.watchdogMs;
    if (Slider("##wd", &wd, 0, 2000, "%d ms", tr("off", "desligado")))
        m_dev.UpdateSettings([wd](AppSettings& a) { a.watchdogMs = wd; });
    TextSmall(MUTED, "%s", tr("Only for controllers that stream continuously: if data stops, every button is released.",
                              "Só para controles com envio contínuo: se os dados pararem, todos os botões são soltos."));
    Divider();
    Label(tr("State refresh", "Reenvio do estado"));
    int hb = s.heartbeatMs;
    if (Slider("##hb", &hb, 0, 50, "%d ms", tr("off", "desligado")))
        m_dev.UpdateSettings([hb](AppSettings& a) { a.heartbeatMs = hb; });
    TextSmall(MUTED, "%s", tr("Repeats the current state like a real DualShock 4, so Steam never keeps a stale state.",
                              "Repete o estado atual como um DualShock 4 real, para a Steam nunca ficar com um estado antigo."));
    EndCard();

    SectionTitle(tr("EMULATORS", "EMULADORES"));
    BeginCard("##emu", 18.f);
    if (ToggleRow("##dsu", tr("DSU server (gyro)", "Servidor DSU (giroscópio)"),
                  tr("For Cemu, Yuzu/Ryujinx, Dolphin… Address 127.0.0.1, port 26760.",
                     "Para Cemu, Yuzu/Ryujinx, Dolphin… Endereço 127.0.0.1, porta 26760."), &s.dsuServer))
        apply([v = s.dsuServer](AppSettings& a) { a.dsuServer = v; });
    EndCard();

    SectionTitle(tr("DIAGNOSTICS", "DIAGNÓSTICO"));
    BeginCard("##diag", 18.f);
    {
        const bool searching = m_dev.Searching();
        const bool failed = m_dev.ScannerFailed();
        KeyValueBadge(tr("Bluetooth search", "Busca Bluetooth"),
                      failed ? tr("Unavailable", "Indisponível") : searching ? tr("Active", "Ativa") : tr("Stopped", "Parada"),
                      failed ? DANGER : searching ? SUCCESS : FAINT);
        Divider();
        KeyValueBadge(tr("Coexistence scanning (Win 11 24H2+)", "Busca com coexistência (Win 11 24H2+)"),
                      m_dev.CoexistenceSupported() ? tr("Available", "Disponível") : tr("Standard mode", "Modo padrão"),
                      m_dev.CoexistenceSupported() ? SUCCESS : WARNING);
        Divider();
        KeyValueBadge(tr("Virtual controller driver (ViGEmBus)", "Driver de controle virtual (ViGEmBus)"),
                      m_dev.VigemAvailable() ? tr("Installed", "Instalado") : tr("Missing", "Ausente"),
                      m_dev.VigemAvailable() ? SUCCESS : DANGER);
        Divider();
        // Data folder with a direct shortcut.
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        const float y = ImGui::GetCursorPosY();
        const float bw = Dp(120.f);
        ImGui::PushTextWrapPos(right - bw - Dp(12.f));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(tr("Settings and log", "Configurações e registro"));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(7.f));
        TextSmall(MUTED, "%%APPDATA%%\\sw2-joycons-adapter");
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        const float rowH = std::max(ImGui::GetItemRectSize().y, Dp(BUTTON_H));
        ImGui::SetCursorPos({right - bw, y + (rowH - Dp(BUTTON_H)) * 0.5f});
        if (Button(tr("Open folder", "Abrir pasta"), ButtonStyle::Secondary, {bw, 0})) {
            Click();
            ShellExecuteW(nullptr, L"open", SettingsDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SetCursorPosY(y + rowH);
        ImGui::Dummy({0, Dp(4.f)});
    }
    const float hw = HalfWidth();
    if (Button(m_showLog ? tr("Hide log", "Ocultar registro") : tr("Show log", "Mostrar registro"), ButtonStyle::Secondary, {m_showLog ? hw : -1.f, 0})) {
        Click(); m_showLog = !m_showLog;
    }
    if (m_showLog) {
        const auto lines = m_dev.LogLines();
        std::string all;
        for (auto& l : lines) { all += l; all += "\n"; }
        ImGui::SameLine();
        if (Button(tr("Copy", "Copiar"), ButtonStyle::Ghost, {hw, 0}, icon::Copy)) {
            ImGui::SetClipboardText(all.c_str());
            PushToast(tr("Log copied", "Registro copiado"), MUTED, icon::Copy);
        }
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Rgb4(SURFACE_2));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Dp(10.f));
        ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
        ImGui::InputTextMultiline("##log", all.data(), all.size() + 1, {-FLT_MIN, Dp(180.f)}, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    EndCard();

    SectionTitle(tr("ABOUT", "SOBRE"));
    BeginCard("##about", 18.f);
    {
        // Identity: logo + name + version badge
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float logo = Dp(40.f);
        const float y = ImGui::GetCursorPosY();
        AppLogo(dl, ImGui::GetCursorScreenPos(), logo);
        ImGui::Dummy({logo, logo});
        ImGui::SameLine(0, Dp(12.f));
        ImGui::SetCursorPosY(y);
        ImGui::BeginGroup();
        ImGui::PushFont(g_fonts.semibold, SIZE_BODY + 1.f);
        ImGui::TextUnformatted("SW2 Adapter");
        ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(6.f));
        char ver[32];
        std::snprintf(ver, sizeof(ver), "v%s", APP_VERSION);
        Badge(ver, MUTED);   // neutral: a version is information, not a status
        ImGui::EndGroup();
        ImGui::Dummy({0, Dp(2.f)});

        // Update status
        const UpdateInfo u = m_updates.Get();
        char latest[64];
        std::snprintf(latest, sizeof(latest), tr("v%s available", "v%s disponível"), u.latest.c_str());
        switch (u.state) {
            case UpdateInfo::State::Available:
                KeyValueBadge(tr("Updates", "Atualizações"), latest, ACCENT);
                break;
            case UpdateInfo::State::UpToDate:
                KeyValueBadge(tr("Updates", "Atualizações"), tr("Up to date", "Atualizado"), SUCCESS);
                break;
            case UpdateInfo::State::Checking:
            case UpdateInfo::State::Idle:
                KeyValueBadge(tr("Updates", "Atualizações"), tr("Checking…", "Verificando…"), FAINT);
                break;
            case UpdateInfo::State::Failed:
                KeyValueBadge(tr("Updates", "Atualizações"), tr("Couldn't check", "Não verificado"), WARNING);
                break;
        }
        if (u.state == UpdateInfo::State::Failed) {
            const char* why = u.error == "offline"               ? tr("no internet connection", "sem conexão com a internet")
                            : u.error == "no public release yet" ? tr("no public release published yet", "nenhuma versão pública publicada ainda")
                            : u.error == "rate limited"          ? tr("GitHub rate limit, try again later", "limite do GitHub, tente mais tarde")
                                                                 : u.error.c_str();
            TextSmall(FAINT, tr("Reason: %s.", "Motivo: %s."), why);
        }

        ImGui::Dummy({0, Dp(2.f)});
        const float half = HalfWidth();
        if (u.state == UpdateInfo::State::Available) {
            if (Button(tr("Download update", "Baixar atualização"), ButtonStyle::Primary, {half, 0})) { Click(); OpenUrl(u.url); }
        } else {
            ImGui::BeginDisabled(u.state == UpdateInfo::State::Checking);
            if (Button(tr("Check again", "Verificar de novo"), ButtonStyle::Secondary, {half, 0}, icon::Refresh)) {
                Click();
                m_updateNotified = false;
                m_updateBannerDismissed = false;
                m_updates.Start();
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (Button(tr("Releases page", "Página de versões"), ButtonStyle::Secondary, {half, 0}, icon::Link)) { Click(); OpenUrl(RELEASES_URL); }

        ImGui::Dummy({0, Dp(2.f)});
        Divider();
        TextSmall(MUTED, "%s", tr("Based on joycon2cpp by TheFrano (MIT). Uses ViGEmBus and Dear ImGui.",
                                  "Baseado no joycon2cpp, de TheFrano (licença MIT). Usa ViGEmBus e Dear ImGui."));
        TextSmall(MUTED, "%s", tr("Nothing is written to the controllers: they stay paired with your Switch 2.",
                                  "Nada é gravado nos controles: eles continuam pareados com o seu Switch 2."));
    }
    EndCard();
}


} // namespace sw2
