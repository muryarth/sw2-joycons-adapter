#pragma once

#include "app/DeviceManager.h"
#include "app/UpdateChecker.h"
#include "ui/Sounds.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sw2 {

class AppUI {
public:
    AppUI(DeviceManager& dev, Sounds& sounds);

    void SetupFonts(float dpiScale);   // call before the first frame / on DPI change
    void SetupStyle(float dpiScale);
    void Frame(bool focused);
    void Background();   // minimized: keep connection logic + status sounds alive

private:
    enum class Page { Home, Device, Settings };

    struct Toast {
        std::string text;
        unsigned    color;
        const char* glyph;
        double      born;
    };

    struct Calibration {
        bool     open = false;
        uint64_t address = 0;
        bool     leftStick = true;
        int      step = 0;          // 0 center, 1 extents, 2 review
        int      cx = 2048, cy = 2048;
        int      minX = 4095, maxX = 0, minY = 4095, maxY = 0;
        int      samples = 0;
    };

    void HandleEvents();
    void DrawHeader();
    void DrawHome(const std::vector<DeviceView>& devices);
    void DrawSearchCard(const AppSettings& s);
    void DrawDeviceRow(const DeviceView& v);
    void DrawEmptyState();
    void DrawDevicePage(const std::vector<DeviceView>& devices);
    void DrawSettingsPage();
    void DrawLiveTest(const DeviceView& v);
    void DrawCalibration();
    void DrawToasts();
    void DrawConfirm();
    void DrawUpdateBanner();
    void CheckUpdateNotice();

    void PushToast(const std::string& text, unsigned color, const char* glyph);
    void Navigate(Page p, uint64_t address = 0);
    void Click() { m_sounds.Play(Sound::Toggle); }

    DeviceManager& m_dev;
    Sounds&        m_sounds;
    AppSettings    m_settings;   // snapshot taken once per frame
    Page           m_page = Page::Home;
    uint64_t       m_selected = 0;
    bool           m_focused = true;
    std::vector<Toast> m_toasts;
    Calibration    m_cal;
    char           m_renameBuf[64] = {};
    uint64_t       m_renameFor = 0;
    uint64_t       m_confirmForget = 0;
    std::string    m_confirmName;
    bool           m_showLog = false;
    UpdateChecker  m_updates;
    bool           m_updateNotified = false;
    bool           m_updateBannerDismissed = false;
};

} // namespace sw2
