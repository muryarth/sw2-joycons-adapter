#include "VirtualPad.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {
std::mutex                                g_settingsMutex;
VirtualPadManager::Settings               g_settings;
std::mutex                                g_logMutex;
std::function<void(const std::string&)>   g_logger;

std::mutex                                g_padsMutex;
std::vector<std::weak_ptr<VirtualPad>>    g_pads;
std::atomic<bool>                         g_running{false};
std::thread                               g_pacer;

void Log(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logger) g_logger(s);
}

DS4_REPORT_EX NeutralFrom(const DS4_REPORT_EX& last) {
    DS4_REPORT_EX r{};
    DS4_REPORT_INIT(reinterpret_cast<PDS4_REPORT>(&r.Report));
    // Keep gravity so motion-aware consumers don't see a "free falling" pad.
    r.Report.wAccelX = last.Report.wAccelX;
    r.Report.wAccelY = last.Report.wAccelY;
    r.Report.wAccelZ = last.Report.wAccelZ;
    return r;
}
} // namespace

VirtualPad::VirtualPad(PVIGEM_CLIENT client, PVIGEM_TARGET target, std::string name)
    : m_client(client), m_target(target), m_name(std::move(name)), m_epoch(Clock::now())
{
    DS4_REPORT_INIT(reinterpret_cast<PDS4_REPORT>(&m_latest.Report));
}

void VirtualPad::SetDsuSink(DsuSink sink) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_dsu = std::move(sink);
}

void VirtualPad::StampLocked(DS4_REPORT_EX& r, TimePoint now) {
    const auto settings = VirtualPadManager::GetSettings();

    // DS4 timestamp unit is 16/3 us (5.33us); it wraps at 16 bits like real hardware.
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - m_epoch).count();
    r.Report.wTimestamp = static_cast<USHORT>((us * 3 / 16) & 0xFFFF);

    // Upper 6 bits of the "special" byte are a frame counter on a real DS4.
    m_frameCounter = static_cast<uint8_t>((m_frameCounter + 1) & 0x3F);
    r.Report.bSpecial = static_cast<BYTE>((r.Report.bSpecial & 0x03) | (m_frameCounter << 2));

    // Report "wired, full" so Steam doesn't show low-battery warnings.
    r.Report.bBatteryLvl        = 0xFF;
    r.Report.bBatteryLvlSpecial = 0x1B;

    if (!settings.emulateTouch) {
        // Bit 7 set = finger NOT touching. Without this Steam sees a finger
        // permanently resting on the touchpad.
        r.Report.bTouchPacketsN = 0;
        r.Report.sCurrentTouch.bIsUpTrackingNum1 = 0x80;
        r.Report.sCurrentTouch.bIsUpTrackingNum2 = 0x80;
        r.Report.sPreviousTouch[0].bIsUpTrackingNum1 = 0x80;
        r.Report.sPreviousTouch[0].bIsUpTrackingNum2 = 0x80;
        r.Report.sPreviousTouch[1].bIsUpTrackingNum1 = 0x80;
        r.Report.sPreviousTouch[1].bIsUpTrackingNum2 = 0x80;
    } else {
        r.Report.sCurrentTouch.bPacketCounter = static_cast<BYTE>(r.Report.wTimestamp >> 8);
    }
}

void VirtualPad::SendLocked(TimePoint now, bool fresh) {
    if (m_closed || !m_client || !m_target) return;
    DS4_REPORT_EX out = m_latest;
    StampLocked(out, now);
    if (fresh && m_dsu) m_dsu(out);
    vigem_target_ds4_update_ex(m_client, m_target, out);
    m_lastSend = now;
    m_pending  = false;
}

void VirtualPad::Submit(const DS4_REPORT_EX& report) {
    const auto now = Clock::now();
    const int minIntervalUs = VirtualPadManager::GetSettings().minIntervalUs;

    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_closed) return;
    if (m_neutralized) {
        m_neutralized = false;
        Log("[" + m_name + "] input resumed");
    }
    m_latest    = report;
    m_hasLatest = true;
    m_lastInput = now;
    m_pending   = true;

    if (minIntervalUs <= 0 || m_lastSend == TimePoint{} ||
        now - m_lastSend >= std::chrono::microseconds(minIntervalUs)) {
        SendLocked(now, true);
    }
    // otherwise the pacer flushes it when the interval elapses
}

void VirtualPad::NeutralizeLocked(TimePoint now, const char* reason) {
    if (m_closed || m_neutralized) return;
    m_latest      = NeutralFrom(m_latest);
    m_hasLatest   = true;
    m_neutralized = true;
    SendLocked(now, true);
    Log("[" + m_name + "] released all inputs (" + reason + ")");
}

void VirtualPad::ForceNeutral(const char* reason) {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lk(m_mutex);
    NeutralizeLocked(now, reason);
}

void VirtualPad::Close() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_closed = true;
}

void VirtualPad::Tick(TimePoint now) {
    const auto s = VirtualPadManager::GetSettings();
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_closed || !m_hasLatest) return;

    if (m_pending && (s.minIntervalUs <= 0 ||
                      now - m_lastSend >= std::chrono::microseconds(s.minIntervalUs))) {
        SendLocked(now, true);
        return;
    }

    if (s.watchdogMs > 0 && m_watchdogOn.load() && !m_neutralized &&
        now - m_lastInput >= std::chrono::milliseconds(s.watchdogMs)) {
        NeutralizeLocked(now, "no data from controller");
    } else if (s.heartbeatMs > 0 &&
               now - m_lastSend >= std::chrono::milliseconds(s.heartbeatMs)) {
        SendLocked(now, false);
    }
}

namespace VirtualPadManager {

void SetSettings(const Settings& s) {
    std::lock_guard<std::mutex> lk(g_settingsMutex);
    g_settings = s;
}

Settings GetSettings() {
    std::lock_guard<std::mutex> lk(g_settingsMutex);
    return g_settings;
}

void SetLogger(std::function<void(const std::string&)> logger) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_logger = std::move(logger);
}

std::shared_ptr<VirtualPad> Create(PVIGEM_CLIENT client, PVIGEM_TARGET target, const std::string& name) {
    auto pad = std::make_shared<VirtualPad>(client, target, name);
    {
        std::lock_guard<std::mutex> lk(g_padsMutex);
        g_pads.push_back(pad);
    }
    Start();
    return pad;
}

void Start() {
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;

    g_pacer = std::thread([] {
        // High resolution timer so a 1ms tick is actually ~1ms (Win10 1803+).
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        // Tick only as fast as the work needs: 1 ms to flush throttled
        // reports on time, 4 ms is plenty for heartbeat/watchdog, and an
        // idle 100 ms when there is no virtual controller at all.
        int tickMs = 100;
        while (g_running.load()) {
            if (timer) {
                LARGE_INTEGER due; due.QuadPart = -10000LL * tickMs;   // relative, 100 ns units
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, DWORD(tickMs + 5));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(tickMs));
            }

            std::vector<std::shared_ptr<VirtualPad>> pads;
            {
                std::lock_guard<std::mutex> lk(g_padsMutex);
                g_pads.erase(std::remove_if(g_pads.begin(), g_pads.end(),
                    [](const std::weak_ptr<VirtualPad>& w) { return w.expired(); }), g_pads.end());
                for (auto& w : g_pads) if (auto p = w.lock()) pads.push_back(std::move(p));
            }
            const auto now = VirtualPad::Clock::now();
            for (auto& p : pads) p->Tick(now);
            tickMs = pads.empty() ? 100 : (GetSettings().minIntervalUs > 0 ? 1 : 4);
        }
        if (timer) CloseHandle(timer);
    });
}

void Stop() {
    if (g_running.exchange(false) && g_pacer.joinable()) g_pacer.join();
    std::lock_guard<std::mutex> lk(g_padsMutex);
    for (auto& w : g_pads) if (auto p = w.lock()) p->Close();
    g_pads.clear();
}

} // namespace VirtualPadManager
