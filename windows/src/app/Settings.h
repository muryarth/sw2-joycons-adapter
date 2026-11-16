#pragma once
// Persistent settings: app preferences + profiles of known controllers.
// Stored in %APPDATA%\sw2-joycons-adapter\settings.json. Nothing is ever
// written to the controllers themselves.

#include "core/Protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sw2 {

enum class UpdatePolicy { Immediate = 0, Hz120 = 1, Hz60 = 2 };

// Per-controller input configuration (POD: cheap to copy in the input path).
struct InputConfig {
    StickCalibration calLeft;    // left stick (Joy-Con L, Pro/GC left)
    StickCalibration calRight;   // right stick (Joy-Con R, Pro/GC right)
    JoyConMapping    mapping;    // single Joy-Con usage
    bool             mouseEnabled = true;  // right Joy-Con optical mouse (Chat button)
    // Pro Controller back buttons: by default they click the sticks (L3/R3),
    // handy when pressing a stick mid-game is awkward.
    ButtonMapping    gl = ButtonMapping::L3;
    ButtonMapping    gr = ButtonMapping::R3;
    // Motion sensors forwarded to the virtual controller (and the DSU server).
    // In a combined Joy-Con pair: both on = average, one on = that side only.
    bool             gyro = true;
};

struct DeviceProfile {
    uint64_t       address = 0;
    ControllerKind kind    = ControllerKind::Unknown;
    std::string    name;
    InputConfig    input;
};

struct AppSettings {
    // General
    int   language       = 0;      // 0 = English (default), 1 = Português
    bool  autoSearch     = true;
    bool  combineJoyCons = true;
    bool  swapABXY       = false;
    float deadzone       = 0.08f;
    GyroSource gyroSource = GyroSource::Both;
    int   idleDisconnectMin = 0;   // disconnect (not forget) after this long without input; 0 = never

    // Feedback
    bool  sounds = true;
    float volume = 0.6f;

    // Steam Input compatibility
    UpdatePolicy updatePolicy = UpdatePolicy::Immediate;
    int heartbeatMs = 8;
    int watchdogMs  = 500;

    // Emulators
    bool dsuServer = false;

    // Window: closing it keeps the app running in the notification area.
    // (Starting with Windows lives in the registry Run key, see platform/Autostart.h.)
    bool closeToTray = true;

    std::vector<DeviceProfile> known;

    DeviceProfile*       Find(uint64_t address);
    const DeviceProfile* Find(uint64_t address) const;
    void                 Forget(uint64_t address);
};

std::wstring SettingsDirectory();
AppSettings  LoadSettings();
bool         SaveSettings(const AppSettings& s);

std::string FormatAddress(uint64_t a);

} // namespace sw2
