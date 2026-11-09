#pragma once
// VirtualPad: single, serialized output path from decoded controller state to the
// ViGEm DS4 target (and optional DSU sink).
//
// Why this exists (Steam Input compatibility):
//  * Every report to a target goes through one mutex, so reports can never be
//    submitted out of order from concurrent BLE callback / worker threads.
//  * Rate-limited policies no longer DROP the latest state: it is kept pending and
//    flushed by the pacer thread, so a button release is never lost.
//  * The latest state is re-sent periodically (heartbeat), like a real DS4 does,
//    so HID readers such as Steam always converge on the current state.
//  * If the physical controller stops sending data (BLE hiccup / disconnect), a
//    watchdog releases every button and centers the sticks instead of leaving the
//    last state "stuck".
//  * Each report is stamped with an advancing DS4 timestamp + frame counter and a
//    sane battery value, and the touchpad is reported as "not touched" unless touch
//    emulation is explicitly enabled.

#include <Windows.h>
#include <ViGEm/Client.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class VirtualPad {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using DsuSink   = std::function<void(const DS4_REPORT_EX&)>;

    VirtualPad(PVIGEM_CLIENT client, PVIGEM_TARGET target, std::string name);

    // Optional sink that receives every *fresh* report (not heartbeats).
    void SetDsuSink(DsuSink sink);

    // Called from input threads with a freshly decoded report.
    void Submit(const DS4_REPORT_EX& report);

    // Immediately release everything (e.g. the BLE device disconnected).
    void ForceNeutral(const char* reason);

    // Stop touching the ViGEm target (shutdown).
    void Close();

    // The silence watchdog is only safe for controllers that stream
    // continuously. Controllers that report only on change (e.g. Pro
    // Controller 2) go silent while a button is held, so it must stay off.
    void SetWatchdogEnabled(bool on) { m_watchdogOn = on; }

    const std::string& Name() const { return m_name; }

    // Called by the pacer thread.
    void Tick(TimePoint now);

private:
    void SendLocked(TimePoint now, bool fresh);
    void NeutralizeLocked(TimePoint now, const char* reason);
    void StampLocked(DS4_REPORT_EX& r, TimePoint now);

    PVIGEM_CLIENT m_client;
    PVIGEM_TARGET m_target;
    std::string   m_name;
    DsuSink       m_dsu;

    std::mutex    m_mutex;
    DS4_REPORT_EX m_latest{};
    bool          m_hasLatest   = false;
    bool          m_pending     = false;
    bool          m_neutralized = false;
    bool          m_closed      = false;
    TimePoint     m_lastInput{};
    TimePoint     m_lastSend{};
    TimePoint     m_epoch{};
    uint8_t       m_frameCounter = 0;
    std::atomic<bool> m_watchdogOn{true};
};

namespace VirtualPadManager {
    struct Settings {
        int  minIntervalUs = 0;    // 0 = forward immediately
        int  heartbeatMs   = 8;    // 0 = disabled
        int  watchdogMs    = 500;  // 0 = disabled
        bool emulateTouch  = false;
    };

    void     SetSettings(const Settings& s);
    Settings GetSettings();

    void SetLogger(std::function<void(const std::string&)> logger);

    std::shared_ptr<VirtualPad> Create(PVIGEM_CLIENT client, PVIGEM_TARGET target, const std::string& name);

    void Start();
    void Stop();   // stops the pacer and closes every pad
}
