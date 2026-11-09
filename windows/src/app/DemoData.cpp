#include "app/DemoData.h"
#include "output/Ds4Bridge.h"

#include <ViGEm/Common.h>

#include <chrono>
#include <cmath>

namespace sw2::demo {

namespace {
constexpr uint64_t DEMO_BASE = 0xD0A0;
}

bool IsDemoAddress(uint64_t address) { return (address & ~uint64_t(0xF)) == DEMO_BASE; }

void AppendViews(std::vector<DeviceView>& out) {
    auto add = [&](uint64_t offset, ControllerKind kind, const char* name, DeviceState state,
                   int player, bool combined, bool known, int batteryMv) {
        DeviceView v;
        v.address    = DEMO_BASE + offset;
        v.kind       = kind;
        v.name       = name;
        v.state      = state;
        v.player     = player;
        v.combined   = combined;
        v.known      = known;
        v.inSession  = state == DeviceState::Connected || state == DeviceState::WaitingSync;
        v.visible    = state == DeviceState::Available;
        v.rssi       = -64;
        v.intervalMs = 7.9f;
        v.jitterMs   = 0.6f;
        if (state == DeviceState::Connected) {
            v.batteryMv  = batteryMv;
            v.batteryPct = BatteryPercent(batteryMv);
            v.batteryMinutesLeft = int(v.batteryPct / 100.f * 20.f * 60.f);
        }
        out.push_back(v);
    };
    add(1, ControllerKind::JoyConL,       "Joy-Con 2 (L)",      DeviceState::Connected,   1, true,  true,  3480);
    add(2, ControllerKind::JoyConR,       "Joy-Con 2 (R)",      DeviceState::Connected,   1, true,  true,  3080);
    add(3, ControllerKind::ProController, "Pro do Arthur",      DeviceState::WaitingSync, 2, false, true,  0);
    add(4, ControllerKind::GameCube,      "GameCube (NSO)",     DeviceState::Available,   0, false, false, 0);
    add(5, ControllerKind::JoyConR,       "Joy-Con 2 (R) azul", DeviceState::Offline,     0, false, true,  0);
    add(6, ControllerKind::JoyConL,       "Joy-Con 2 (L)",      DeviceState::Connecting,  0, false, true,  0);
    add(7, ControllerKind::ProController, "Pro Controller 2",   DeviceState::Offline,     0, false, true,  0);
    out.back().onUsb = true;   // charging on the cable
}

void FillLiveInput(LiveInput& out) {
    out = LiveInput{};
    out.valid = true;
    out.hasLeft = out.hasRight = true;
    out.report = ToVigem(NeutralReport());
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto& r = out.report.Report;
    r.bThumbLX = BYTE(128 + 127 * std::cos(t * 1.6));
    r.bThumbLY = BYTE(128 + 127 * std::sin(t * 1.6));
    r.bThumbRX = BYTE(128 + 90 * std::cos(-t));
    r.bThumbRY = BYTE(128 + 90 * std::sin(-t));
    r.bTriggerL = BYTE(127 + 127 * std::sin(t * 2.0));
    switch (int(t * 1.5) % 4) {
        case 0: r.wButtons |= DS4_BUTTON_CIRCLE; break;
        case 1: r.wButtons |= DS4_BUTTON_SHOULDER_RIGHT | DS4_BUTTON_TRIGGER_RIGHT; r.bTriggerR = 255; break;
        case 2: DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&r), DS4_BUTTON_DPAD_EAST); break;
        default: r.bSpecial |= DS4_SPECIAL_BUTTON_PS; break;
    }
}

} // namespace sw2::demo
