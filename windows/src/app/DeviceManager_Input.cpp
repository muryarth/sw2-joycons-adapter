// Input reports -> virtual controller, plus per-device statistics and battery.

#include "app/DeviceManager.h"
#include "core/I18n.h"
#include "output/Ds4Bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace sw2 {

namespace {
constexpr float STATS_SMOOTHING      = 0.05f;   // EMA weight of a new packet interval
constexpr float ON_CHANGE_GAP_MS     = 150.f;   // a gap this long means "reports only on change"
constexpr auto  CONTINUOUS_PROOF     = std::chrono::seconds(2);
// Connection ladder: slow averages (a few seconds), so it shows how the link
// has been doing lately instead of twitching with every late packet; a level
// only changes when clearly past its threshold and not more than once per
// LINK_LEVEL_DWELL.
constexpr float LINK_SMOOTHING       = 0.008f;
constexpr float LINK_HYSTERESIS      = 1.25f;
constexpr auto  LINK_LEVEL_DWELL     = std::chrono::milliseconds(1500);
int RawLinkLevel(float jitterMs, float lateRatio) {
    if (jitterMs < 2.5f && lateRatio < 0.02f) return 4;
    if (jitterMs < 5.f  && lateRatio < 0.06f) return 3;
    if (jitterMs < 10.f && lateRatio < 0.15f) return 2;
    return 1;
}
constexpr float ON_CHANGE_SILENCE_MS = 1000.f;  // once proven continuous: one silence this long...
constexpr int   ON_CHANGE_REPEAT_GAPS = 3;      // ...or this many long gaps...
constexpr auto  ON_CHANGE_REPEAT_WINDOW = std::chrono::seconds(10);   // ...within this window
constexpr float BATTERY_SMOOTHING    = 0.02f;   // voltage sags under rumble: smooth heavily
constexpr int   BATTERY_LOW_PCT      = 19;      // the indicator turns red below 20% (BatteryColor)
constexpr int   BATTERY_REARM_PCT    = 25;      // warn again only after it recovered
// Remaining-time estimate
// The voltage sags under load (rumble) and recovers when idle, so the level
// wanders by a few percent. The old estimate compared against one reference
// point and restarted on any 3% rise, falling back to the rated life, which
// made the time jump up. Now: a slowly smoothed level, sampled once a minute,
// a least-squares slope over a sliding window, and a projected "empty at"
// moment that counts down and only eases towards each new estimate.
constexpr auto  DRAIN_SETTLE         = std::chrono::seconds(90);    // let the voltage settle after connecting
constexpr float LEVEL_TAU_S          = 120.f;                       // level smoothing time constant
constexpr auto  DRAIN_SAMPLE_EVERY   = std::chrono::seconds(60);
constexpr auto  DRAIN_WINDOW         = std::chrono::minutes(45);    // slope over the last 45 min...
constexpr auto  DRAIN_MIN_SPAN       = std::chrono::minutes(15);    // ...once there are at least 15
constexpr float DRAIN_MIN_RATE       = 0.3f;                        // %/h; flatter = voltage recovering, keep the old rate
constexpr float DRAIN_MAX_LIFE       = 1.5f;                        // never project more than 1.5x the rated life
constexpr auto  DRAIN_MAX_GAP        = std::chrono::minutes(10);    // longer without samples (PC slept): start the window over
constexpr float DRAIN_JUMP_PCT       = 10.f;                        // a rise this big not seen as charging: start over
constexpr float ETA_EASE             = 0.25f;                       // per sample: how far the projection moves to a new estimate
constexpr size_t RECENT_REPORTS      = 120;                         // kept for the disconnect dump
constexpr auto  BATTERY_HISTORY_STEP = std::chrono::milliseconds(250);
constexpr auto  BATTERY_HISTORY_SPAN = std::chrono::seconds(5);
constexpr int   CURRENT_DEBOUNCE     = 3;                            // reports (~45 ms) before trusting a change
// Stutter diagnostics: holes in a continuous input stream, summarised in the log.
constexpr float INPUT_GAP_MS         = 60.f;                         // ~4 missed reports
constexpr auto  GAP_REPORT_EVERY     = std::chrono::seconds(60);

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
        TrackActivityLocked(*d, report, now);
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
    bool motion = cfg.gyro;   // per controller: off = its sensors never reach the game
    if (solo) {
        out = d->kind == ControllerKind::ProController
            ? BuildProReport(report, cfg.calLeft, cfg.calRight, tune, cfg.gl, cfg.gr)
            : BuildGameCubeReport(report, cfg.calLeft, cfg.calRight, tune);
    } else {
        if (slot->left == d->address)  slot->leftReport = report;
        if (slot->right == d->address) slot->rightReport = report;
        if (combined) {
            // A pair sends the average of both sides, or only the side that is on.
            const GyroSource source = cfgLeft.gyro == cfgRight.gyro ? GyroSource::Both
                                    : cfgLeft.gyro ? GyroSource::Left : GyroSource::Right;
            motion = cfgLeft.gyro || cfgRight.gyro;
            out = BuildDualJoyConReport(slot->leftReport, slot->rightReport, cfgLeft.calLeft, cfgRight.calRight,
                                        tune, cfgLeft.mapping.dpadAsDpad, source);
        } else {
            const bool isLeft = d->kind == ControllerKind::JoyConL;
            out = BuildSingleJoyConReport(report, isLeft, isLeft ? cfg.calLeft : cfg.calRight, tune, cfg.mapping);
        }
    }
    if (!motion) {
        out.Report.wGyroX = out.Report.wGyroY = out.Report.wGyroZ = 0;
        out.Report.wAccelX = out.Report.wAccelY = out.Report.wAccelZ = 0;
    }
    slot->last = ToVigem(out);   // the live test shows the physical buttons: keep the pre-swap state
    slot->hasLast = true;
    if (rt.swap) ApplySwapABXY(out);
    if (slot->pad) slot->pad->Submit(ToVigem(out));
}

// Counts holes in the input stream of a continuously streaming controller and
// logs a one-line summary per minute when there were any: how many, the worst,
// and whether rumble or Bluetooth scanning were active, which helps tell radio
// interference (headsets, other devices) from the app's own traffic.
void DeviceManager::TrackInputGapsLocked(Device& d, float dt, TimePoint now) {
    if (d.streaming == Streaming::Continuous && dt >= INPUT_GAP_MS) {
        ++d.gapCount;
        d.gapWorstMs = std::max(d.gapWorstMs, dt);
        if (d.slot && d.slot->amp.load() > 0.f) ++d.gapDuringRumble;
        if (Searching()) ++d.gapDuringScan;
    }
    if (d.gapWindowStart == TimePoint{}) { d.gapWindowStart = now; return; }
    if (now - d.gapWindowStart < GAP_REPORT_EVERY) return;
    if (d.gapCount > 0) {
        char line[200];
        std::snprintf(line, sizeof(line), ": %u input gaps (>= %.0f ms) in the last minute, worst %.0f ms · during rumble %u · during scan %u · avg interval %.1f ms",
                      d.gapCount, INPUT_GAP_MS, d.gapWorstMs, d.gapDuringRumble, d.gapDuringScan, d.intervalMs);
        Log(NameLocked(d.address, d.kind) + line);
    }
    d.gapWindowStart = now;
    d.gapCount = d.gapDuringRumble = d.gapDuringScan = 0;
    d.gapWorstMs = 0.f;
}

// "Activity" for the idle auto-disconnect: a button changed, a stick moved
// clearly away from where it rested, or an analog trigger moved. Motion
// sensors are ignored (they change all the time, even on a table).
void DeviceManager::TrackActivityLocked(Device& d, const std::vector<uint8_t>& report, TimePoint now) {
    constexpr int STICK_MOVE = 160;     // of 4096: well above resting noise
    constexpr int TRIGGER_MOVE = 24;    // of 255
    if (report.size() < 16) return;
    bool active = d.lastActivity == TimePoint{};
    for (int i = 0; i < 4; ++i)
        if (report[3 + i] != d.actButtons[i]) { active = true; d.actButtons[i] = report[3 + i]; }
    int raw[4];
    ExtractRawStick(report, true, raw[0], raw[1]);
    ExtractRawStick(report, false, raw[2], raw[3]);
    for (int i = 0; i < 4; ++i) {
        if (d.actStick[i] < 0 || std::abs(raw[i] - d.actStick[i]) > STICK_MOVE) {
            if (d.actStick[i] >= 0) active = true;
            d.actStick[i] = raw[i];
        }
    }
    if (report.size() > 0x3D) {
        for (int i = 0; i < 2; ++i) {
            const int t = report[0x3C + i];
            if (d.actTrigger[i] < 0 || std::abs(t - d.actTrigger[i]) > TRIGGER_MOVE) {
                if (d.actTrigger[i] >= 0) active = true;
                d.actTrigger[i] = t;
            }
        }
    }
    if (active) d.lastActivity = now;
}

void DeviceManager::UpdateStatsLocked(Device& d, TimePoint now) {
    if (d.lastPacket == TimePoint{}) {   // first packet after (re)connecting
        d.streamCheckStart = now;
        d.lastPacket = now;
        return;
    }
    const float dt = std::chrono::duration<float, std::milli>(now - d.lastPacket).count();
    d.lastPacket = now;
    TrackInputGapsLocked(d, dt, now);
    if (d.intervalMs <= 0.f) {
        d.intervalMs = dt;
    } else {
        // One long gap counts as a bad packet, not as hundreds of ms of jitter:
        // otherwise a single hiccup drops the connection ladder to the bottom.
        const float sdt = std::min(dt, INPUT_GAP_MS);
        d.jitterMs   += (std::abs(sdt - d.intervalMs) - d.jitterMs) * STATS_SMOOTHING;
        d.intervalMs += (sdt - d.intervalMs) * STATS_SMOOTHING;
    }

    // Learn whether this controller streams continuously or only on change.
    // A controller already proven continuous is not reclassified by a single
    // hiccup (a rumble test or a Bluetooth scan can hold reports back for a
    // few hundred ms): that takes a real silence, or long gaps that repeat.
    if (d.streaming == Streaming::Continuous) {
        const bool late = dt > std::max(d.intervalMs * 2.f, d.intervalMs + 12.f);
        d.lateRatio  += ((late ? 1.f : 0.f) - d.lateRatio) * LINK_SMOOTHING;
        d.jitterSlow += (d.jitterMs - d.jitterSlow) * LINK_SMOOTHING;
        if (d.linkLevel == 0) {
            d.linkLevel = RawLinkLevel(d.jitterSlow, d.lateRatio);
            d.linkLevelAt = now;
        } else if (now - d.linkLevelAt >= LINK_LEVEL_DWELL) {
            const int up   = RawLinkLevel(d.jitterSlow * LINK_HYSTERESIS, d.lateRatio * LINK_HYSTERESIS);
            const int down = RawLinkLevel(d.jitterSlow / LINK_HYSTERESIS, d.lateRatio / LINK_HYSTERESIS);
            const int next = up > d.linkLevel ? d.linkLevel + 1 : down < d.linkLevel ? d.linkLevel - 1 : d.linkLevel;
            if (next != d.linkLevel) { d.linkLevel = next; d.linkLevelAt = now; }
        }
    }

    bool onChange = false;
    if (d.streaming != Streaming::OnChange && dt > ON_CHANGE_GAP_MS) {
        onChange = d.streaming == Streaming::Measuring || dt > ON_CHANGE_SILENCE_MS;
        if (!onChange) {
            if (now - d.longGapWindow > ON_CHANGE_REPEAT_WINDOW) { d.longGapWindow = now; d.longGaps = 0; }
            onChange = ++d.longGaps >= ON_CHANGE_REPEAT_GAPS;
        }
    }
    if (onChange) {
        d.streaming = Streaming::OnChange;
        d.linkLevel = 0;   // timing says nothing about such a controller
        Log(NameLocked(d.address, d.kind) + " reports only on change · silence watchdog disabled");
        if (d.slot) UpdateWatchdogLocked(d.slot);
    } else if (d.streaming == Streaming::Measuring && now - d.streamCheckStart > CONTINUOUS_PROOF) {
        d.streaming = Streaming::Continuous;
        d.jitterSlow = d.jitterMs;   // start the ladder from what was measured so far
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
    if (d.usbPendingSince != TimePoint{} || d.chargingShown) {
        // Diagnostics while charging over Bluetooth: raw voltage and battery
        // current every 30 s, to learn how a full charge shows up in the
        // reports (current tapering to ~0 at the top voltage?).
        if (d.chargingShown && report.size() >= 0x23 && now - d.chargeLogAt >= std::chrono::seconds(30)) {
            d.chargeLogAt = now;
            char line[96];
            std::snprintf(line, sizeof(line), ": charging · mV=%d I=%d", BatteryMillivolts(report),
                          int(int16_t(report[0x21] | (report[0x22] << 8))));
            Log(NameLocked(d.address, d.kind) + line);
        }
        return;
    }
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
    UpdateDrainRateLocked(d, now);
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
void DeviceManager::UpdateDrainRateLocked(Device& d, TimePoint now) {
    using Hours = std::chrono::duration<float, std::ratio<3600>>;
    using Secs  = std::chrono::duration<float>;
    if (d.streamCheckStart == TimePoint{} || now - d.streamCheckStart < DRAIN_SETTLE) return;
    const float pct = BatteryPercentF(d.batteryMv);
    if (pct < 0.f) return;

    // Slow level, time-based so it behaves the same at any report rate.
    if (d.levelPct < 0.f || d.levelTime == TimePoint{}) {
        d.levelPct = pct;
    } else {
        const float dt = Secs(now - d.levelTime).count();
        d.levelPct += (pct - d.levelPct) * dt / (LEVEL_TAU_S + dt);
    }
    d.levelTime = now;

    if (!d.drainSamples.empty() && now - d.drainSamples.back().first < DRAIN_SAMPLE_EVERY) return;
    if (!d.drainSamples.empty() && now - d.drainSamples.back().first > DRAIN_MAX_GAP) d.drainSamples.clear();
    if (!d.drainSamples.empty()) {
        float lowest = d.drainSamples.front().second;
        for (const auto& s : d.drainSamples) lowest = std::min(lowest, s.second);
        if (d.levelPct >= lowest + DRAIN_JUMP_PCT) {   // charged without us noticing
            d.drainSamples.clear();
            d.drainPctPerHour = 0.f;
            d.emptyAt = {};
        }
    }
    d.drainSamples.emplace_back(now, d.levelPct);
    while (now - d.drainSamples.front().first > DRAIN_WINDOW) d.drainSamples.pop_front();

    // Least-squares slope (%/h) over the window.
    if (d.drainSamples.size() >= 5 && now - d.drainSamples.front().first >= DRAIN_MIN_SPAN) {
        const TimePoint t0 = d.drainSamples.front().first;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const double n = double(d.drainSamples.size());
        for (const auto& [t, y] : d.drainSamples) {
            const double x = Hours(t - t0).count();
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double den = n * sxx - sx * sx;
        if (den > 1e-9) {
            const float rate = float(-(n * sxy - sx * sy) / den);
            if (rate >= DRAIN_MIN_RATE) d.drainPctPerHour = rate;   // else: level recovering, keep the last rate
        }
    }

    // Project the moment it runs out and ease towards it, so the shown time
    // counts down minute by minute instead of jumping.
    const float rated = RatedHours(d.kind);
    float rate = d.drainPctPerHour > 0.f ? d.drainPctPerHour : rated > 0.f ? 100.f / rated : 0.f;
    if (rate <= 0.f) return;
    if (rated > 0.f) rate = std::max(rate, 100.f / (rated * DRAIN_MAX_LIFE));
    const float target = d.levelPct / rate;   // hours
    if (d.emptyAt == TimePoint{} || d.emptyAt <= now) {
        d.emptyAt = now + std::chrono::duration_cast<Clock::duration>(Hours(target));
    } else {
        const float left = Hours(d.emptyAt - now).count();
        d.emptyAt = now + std::chrono::duration_cast<Clock::duration>(Hours(left + (target - left) * ETA_EASE));
    }
}

void DeviceManager::ResetDrainLocked(Device& d) {
    d.levelPct = -1.f;
    d.levelTime = {};
    d.drainSamples.clear();
    d.drainPctPerHour = 0.f;
    d.emptyAt = {};
}

int DeviceManager::MinutesLeft(const Device& d, int pct, TimePoint now, bool& measured) {
    measured = d.drainPctPerHour > 0.f;
    if (pct < 0) return -1;
    if (d.emptyAt != TimePoint{})
        return std::max(0, int(std::chrono::duration_cast<std::chrono::minutes>(d.emptyAt - now).count()));
    const float rated = RatedHours(d.kind);   // first seconds after connecting
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
