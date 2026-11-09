// Input reports -> virtual controller, plus per-device statistics and battery.

#include "app/DeviceManager.h"
#include "core/I18n.h"
#include "output/Ds4Bridge.h"

#include <cmath>
#include <cstdio>

namespace sw2 {

namespace {
constexpr float STATS_SMOOTHING      = 0.05f;   // EMA weight of a new packet interval
constexpr float ON_CHANGE_GAP_MS     = 150.f;   // a gap this long means "reports only on change"
constexpr auto  CONTINUOUS_PROOF     = std::chrono::seconds(2);
constexpr float BATTERY_SMOOTHING    = 0.02f;   // voltage sags under rumble: smooth heavily
constexpr int   BATTERY_LOW_PCT      = 15;
constexpr int   BATTERY_REARM_PCT    = 25;      // warn again only after it recovered
// Remaining-time estimate
constexpr auto  DRAIN_SETTLE         = std::chrono::seconds(90);    // let the voltage settle after connecting
constexpr float DRAIN_MIN_HOURS      = 0.25f;                       // measure over at least 15 min...
constexpr int   DRAIN_MIN_DROP_PCT   = 2;                           // ...and a visible drop
constexpr int   CHARGING_RISE_PCT    = 3;                           // a rise this big = charging: restart
constexpr size_t RECENT_REPORTS      = 120;                         // kept for the disconnect dump
constexpr auto  BATTERY_HISTORY_STEP = std::chrono::milliseconds(250);
constexpr auto  BATTERY_HISTORY_SPAN = std::chrono::seconds(5);
constexpr int   CURRENT_DEBOUNCE     = 3;                            // reports (~45 ms) before trusting a change

// Nintendo's rated battery life (hours). 0 = unknown: only measured estimates.
float RatedHours(ControllerKind k) {
    switch (k) {
        case ControllerKind::JoyConL:
        case ControllerKind::JoyConR:       return 20.f;
        case ControllerKind::ProController: return 40.f;
        default:                            return 0.f;
    }
}
} // namespace

void DeviceManager::OnInput(const std::shared_ptr<Device>& d, std::vector<uint8_t> report) {
    const auto now = Clock::now();
    std::shared_ptr<Slot> slot;
    std::shared_ptr<Switch2Link> link;
    InputConfig cfg, cfgLeft, cfgRight;
    Runtime rt;
    bool solo = false, combined = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (d->state != DeviceState::Connected) return;
        d->recent.emplace_back(now, report);
        if (d->recent.size() > RECENT_REPORTS) d->recent.pop_front();
        // A cable was plugged in: the controller is leaving Bluetooth and its
        // last packets are not trustworthy. Keep the virtual pad neutral.
        if (d->onUsb) return;
        if (m_trace) {
            // Diagnostics: every report whose size changes, plus one in 30.
            const bool sizeChanged = report.size() != d->traceLastSize;
            if (sizeChanged || d->traceCount % 30 == 0) {
                std::string hex;
                char b[4];
                for (uint8_t x : report) { std::snprintf(b, sizeof(b), "%02X ", x); hex += b; }
                Log("trace " + FormatAddress(d->address).substr(9) + " #" + std::to_string(d->traceCount) +
                    " len=" + std::to_string(report.size()) + (sizeChanged ? " SIZE CHANGED" : "") + " : " + hex);
            }
            d->traceLastSize = report.size();
            ++d->traceCount;
        }
        UpdateStatsLocked(*d, now);
        UpdateBatteryLocked(*d, report, now);
        d->lastReport = report;
        slot = d->slot;
        link = d->link;
        if (!slot) return;
        if (const auto* p = m_settings.Find(d->address)) cfg = p->input;
        if (const auto* p = m_settings.Find(slot->left))  cfgLeft = p->input;
        if (const auto* p = m_settings.Find(slot->right)) cfgRight = p->input;
        rt = m_runtime;
        solo = slot->solo == d->address;
        combined = slot->left && slot->right;
    }

    // Optical mouse: only for a right Joy-Con used on its own.
    const bool mouseAllowed = d->kind == ControllerKind::JoyConR && !combined && cfg.mouseEnabled;
    if (mouseAllowed) {
        HandleMouse(*d, link, report, cfg, LedPattern(slot->player));
    } else if (d->mouse.CurrentMode() != OpticalMouse::Off) {
        // Turned off (setting or pairing) while active: release buttons, restore LEDs.
        d->mouse.Disable();
        d->mouseMode = OpticalMouse::Off;
        if (link) link->SetPlayerLeds(LedPattern(slot->player));
    }

    StickTuning tune;
    tune.deadzone = rt.deadzone;

    std::lock_guard<std::mutex> sl(slot->mutex);
    Ds4Report out;
    if (solo) {
        out = d->kind == ControllerKind::ProController
            ? BuildProReport(report, cfg.calLeft, cfg.calRight, tune, cfg.gl, cfg.gr)
            : BuildGameCubeReport(report, cfg.calLeft, cfg.calRight, tune);
    } else {
        if (slot->left == d->address)  slot->leftReport = report;
        if (slot->right == d->address) slot->rightReport = report;
        if (combined) {
            out = BuildDualJoyConReport(slot->leftReport, slot->rightReport, cfgLeft.calLeft, cfgRight.calRight,
                                        tune, cfgLeft.mapping.dpadAsDpad, rt.gyro);
        } else {
            const bool isLeft = d->kind == ControllerKind::JoyConL;
            out = BuildSingleJoyConReport(report, isLeft, isLeft ? cfg.calLeft : cfg.calRight, tune, cfg.mapping);
        }
    }
    slot->last = ToVigem(out);   // the live test shows the physical buttons: keep the pre-swap state
    slot->hasLast = true;
    if (rt.swap) ApplySwapABXY(out);
    if (slot->pad) slot->pad->Submit(ToVigem(out));
}

void DeviceManager::UpdateStatsLocked(Device& d, TimePoint now) {
    if (d.lastPacket == TimePoint{}) {   // first packet after (re)connecting
        d.streamCheckStart = now;
        d.lastPacket = now;
        return;
    }
    const float dt = std::chrono::duration<float, std::milli>(now - d.lastPacket).count();
    d.lastPacket = now;
    if (d.intervalMs <= 0.f) {
        d.intervalMs = dt;
    } else {
        d.jitterMs   += (std::abs(dt - d.intervalMs) - d.jitterMs) * STATS_SMOOTHING;
        d.intervalMs += (dt - d.intervalMs) * STATS_SMOOTHING;
    }

    // Learn whether this controller streams continuously or only on change.
    if (d.streaming != Streaming::OnChange && dt > ON_CHANGE_GAP_MS) {
        d.streaming = Streaming::OnChange;
        Log(NameLocked(d.address, d.kind) + " reports only on change · silence watchdog disabled");
        if (d.slot) UpdateWatchdogLocked(d.slot);
    } else if (d.streaming == Streaming::Measuring && now - d.streamCheckStart > CONTINUOUS_PROOF) {
        d.streaming = Streaming::Continuous;
        if (d.slot) UpdateWatchdogLocked(d.slot);
    }
}

void DeviceManager::UpdateBatteryLocked(Device& d, const std::vector<uint8_t>& report, TimePoint now) {
    // Battery current (signed, next to the voltage): 0 on battery, positive
    // as soon as a charger feeds it. Debounced over a few reports.
    if (report.size() >= 0x23) {
        const bool on = int16_t(report[0x21] | (report[0x22] << 8)) > 0;
        if (on == d.chargingByCurrent) {
            d.currentStreak = 0;
        } else if (++d.currentStreak >= CURRENT_DEBOUNCE) {
            d.currentStreak = 0;
            d.chargingByCurrent = on;
            if (on && d.usbPendingSince != TimePoint{}) {   // the cable Windows just reported is this one
                d.usbPendingSince = {};
                d.chargingOnBle = true;
            }
            RefreshChargingLocked(d, now);
        }
    }
    // On the cable the charger lifts the voltage, so the estimate would climb
    // for no reason: freeze it until charging stops (see RefreshChargingLocked).
    if (d.usbPendingSince != TimePoint{} || d.chargingShown) return;
    if (d.batteryHoldUntil != TimePoint{}) {
        if (now < d.batteryHoldUntil) return;
        d.batteryHoldUntil = {};   // settled: measure from fresh readings
        d.batteryMv = 0.f;
        d.batteryHistory.clear();
    }
    const int mv = BatteryMillivolts(report);
    if (!mv) return;
    d.batteryMv = d.batteryMv > 0.f ? d.batteryMv + (mv - d.batteryMv) * BATTERY_SMOOTHING : float(mv);
    // Short history, so a cable can roll the level back to before the charger
    // lifted the voltage (Windows reports the cable up to ~1 s late).
    if (d.batteryHistory.empty() || now - d.batteryHistory.back().first >= BATTERY_HISTORY_STEP) d.batteryHistory.emplace_back(now, d.batteryMv);
    while (!d.batteryHistory.empty() && now - d.batteryHistory.front().first > BATTERY_HISTORY_SPAN) d.batteryHistory.pop_front();
    const int pct = BatteryPercent(int(d.batteryMv));
    UpdateDrainRateLocked(d, pct, now);
    if (pct >= 0 && pct <= BATTERY_LOW_PCT && !d.batteryWarned) {
        d.batteryWarned = true;
        char msg[160];
        std::snprintf(msg, sizeof(msg), tr("%s battery is low (≈%d%%)", "Bateria fraca no %s (≈%d%%)"),
                      NameLocked(d.address, d.kind).c_str(), pct);
        Emit(EventType::BatteryLow, msg, d.address);
    } else if (pct >= BATTERY_REARM_PCT) {
        d.batteryWarned = false;
    }
}

// Measures how fast the battery actually drains in this session. Until there
// is enough data the estimate falls back to Nintendo's rated battery life.
void DeviceManager::UpdateDrainRateLocked(Device& d, int pct, TimePoint now) {
    if (pct < 0) return;
    if (d.drainRefPct < 0) {
        if (d.streamCheckStart != TimePoint{} && now - d.streamCheckStart >= DRAIN_SETTLE) {
            d.drainRefTime = now;
            d.drainRefPct = pct;
        }
        return;
    }
    if (pct >= d.drainRefPct + CHARGING_RISE_PCT) {   // charging: start over
        d.drainRefTime = now;
        d.drainRefPct = pct;
        d.drainPctPerHour = 0.f;
        return;
    }
    const float hours = std::chrono::duration<float, std::ratio<3600>>(now - d.drainRefTime).count();
    const int drop = d.drainRefPct - pct;
    if (hours >= DRAIN_MIN_HOURS && drop >= DRAIN_MIN_DROP_PCT) d.drainPctPerHour = drop / hours;
}

int DeviceManager::MinutesLeft(const Device& d, int pct, bool& measured) {
    measured = d.drainPctPerHour > 0.f;
    if (pct < 0) return -1;
    if (measured) return int(pct / d.drainPctPerHour * 60.f);
    const float rated = RatedHours(d.kind);
    return rated > 0.f ? int(pct / 100.f * rated * 60.f) : -1;
}

void DeviceManager::HandleMouse(Device& d, const std::shared_ptr<Switch2Link>& link, std::vector<uint8_t>& report,
                                const InputConfig& cfg, uint8_t playerLeds) {
    if (!d.mouse.Process(report, cfg.calRight)) return;   // no mode change

    const auto mode = d.mouse.CurrentMode();
    d.mouseMode = mode;
    if (link) link->SetPlayerLeds(mode == OpticalMouse::Off ? playerLeds : OpticalMouse::ModeLedPattern(mode));
    const char* names[OpticalMouse::ModeCount] = {tr("off", "desligado"), tr("fast", "rápido"),
                                                  tr("normal", "normal"), tr("slow", "lento")};
    Emit(EventType::MouseMode, std::string(tr("Mouse mode ", "Modo mouse ")) + names[mode], d.address);
}

} // namespace sw2
