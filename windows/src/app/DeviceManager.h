#pragma once
// Owns discovery, connections, virtual controllers ("slots") and the
// session rules:
//  * App focused + auto search on  -> continuous scan, list everything in sync.
//  * App in background             -> scan ONLY while a controller connected in
//                                     this session is missing; stop when it is back.
//  * Known controllers (saved) connect automatically when they show up in sync.
//  * Unknown controllers are listed with a "Connect" button.
//  * "Disconnect" drops the link, removes it from the session and forgets it.
//  * "Wake" advertisements (a controller calling its Switch 2) are ignored.
//
// Implementation is split by responsibility:
//   DeviceManager.cpp        lifecycle, settings, events, scanning, connections, views
//   DeviceManager_Slots.cpp  virtual controllers, rumble, player LEDs
//   DeviceManager_Input.cpp  input reports -> virtual controller, stats, battery
//
// Locking order (never take them in the opposite order):
//   m_mutex -> Slot::mutex -> VirtualPad (internal) -> m_eventMutex

#include "app/OpticalMouse.h"
#include "app/Settings.h"
#include "ble/BleScanner.h"
#include "ble/Switch2Link.h"
#include "output/DsuServer.h"
#include "output/VirtualPad.h"
#include "platform/UsbWatcher.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace sw2 {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

enum class DeviceState { Available, Offline, Connecting, Connected, WaitingSync };

// How a controller sends input, learned right after connecting. Only
// continuous streams make the "no data -> release everything" watchdog safe.
enum class Streaming { Measuring, Continuous, OnChange };

enum class EventType { Found, Connected, Reconnected, Lost, Disconnected, Failed, MouseMode, BatteryLow, UsbConnected, UsbRemoved, Info };

struct Event {
    EventType   type;
    std::string text;
    uint64_t    address = 0;
};

struct DeviceView {
    uint64_t       address = 0;
    ControllerKind kind = ControllerKind::Unknown;
    std::string    name;
    DeviceState    state = DeviceState::Offline;
    bool           known = false;
    bool           inSession = false;
    bool           visible = false;    // seen in sync mode recently
    int            player = 0;         // 1-based, 0 = none
    bool           combined = false;   // part of an L+R pair
    int16_t        rssi = 0;
    float          intervalMs = 0.f;   // average time between input packets
    float          jitterMs = 0.f;
    int            mouseMode = 0;      // OpticalMouse::Mode
    Streaming      streaming = Streaming::Measuring;
    int            batteryMv = 0;      // smoothed, 0 = unknown
    int            batteryPct = -1;    // estimated, -1 = unknown
    int            batteryMinutesLeft = -1;   // estimated, -1 = unknown
    bool           batteryMeasured = false;   // estimate from measured drain (else rated life)
    bool           onUsb = false;              // plugged in: the controller paused Bluetooth itself
    bool           charging = false;           // on the cable while still connected over Bluetooth
    std::string    error;
};

struct LiveInput {
    bool          valid = false;
    DS4_REPORT_EX report{};
    bool          hasLeft = false, hasRight = false;
    int           rawLX = 2048, rawLY = 2048, rawRX = 2048, rawRY = 2048;
};

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    bool Init();                 // connects to ViGEmBus, starts workers
    void Shutdown();
    bool VigemAvailable() const { return m_vigemOk; }

    // UI thread, every frame (also while minimized).
    void Tick(bool focused);
    void SearchNow(int seconds = 30);   // manual search when auto search is off
    bool Searching() const;
    bool ScannerFailed() const { return m_scanner.Failed(); }
    bool CoexistenceSupported() const { return m_scanner.CoexistenceSupported(); }

    // UI actions
    void Connect(uint64_t address);
    void DisconnectAndForget(uint64_t address);
    void Identify(uint64_t address);
    void TestRumble(uint64_t address);

    // Settings / profiles: applied immediately, saved shortly after (debounced).
    AppSettings Settings() const;
    void UpdateSettings(const std::function<void(AppSettings&)>& fn);
    void UpdateProfile(uint64_t address, const std::function<void(DeviceProfile&)>& fn);

    // Views for the UI
    std::vector<DeviceView> Snapshot() const;
    bool LiveInputFor(uint64_t address, LiveInput& out) const;
    std::vector<Event> DrainEvents();
    std::vector<std::string> LogLines() const;
    void SetDemo(bool on) { m_demo = on; }   // --demo: fake rows for UI review
    void SetTrace(bool on) { m_trace = on; } // --trace: log raw input reports (diagnostics)

private:
    struct Slot;
    struct Device {
        uint64_t       address = 0;
        ControllerKind kind = ControllerKind::Unknown;
        DeviceState    state = DeviceState::Offline;
        bool           inSession = false;
        // Discovery
        TimePoint      lastSeen{};
        bool           wasVisible = false;
        int16_t        rssi = 0;
        // Connection
        std::shared_ptr<Switch2Link> link;
        std::shared_ptr<Slot> slot;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::string    error;
        int            failures = 0;
        TimePoint      retryAfter{};
        // Input statistics
        TimePoint      lastPacket{};
        float          intervalMs = 0.f, jitterMs = 0.f;
        Streaming      streaming = Streaming::Measuring;
        TimePoint      streamCheckStart{};
        std::vector<uint8_t> lastReport;
        uint64_t       traceCount = 0;         // --trace bookkeeping
        size_t         traceLastSize = 0;
        // Last reports, dumped to the log when the link drops or a cable is
        // plugged in, so odd packets around a disconnect can be diagnosed.
        std::deque<std::pair<TimePoint, std::vector<uint8_t>>> recent;
        std::deque<std::pair<TimePoint, float>> batteryHistory;   // smoothed mV every 250 ms, last 5 s
        TimePoint      batteryHoldUntil{};     // after charging: keep the level until the voltage settled
        // Battery
        float          batteryMv = 0.f;        // smoothed voltage
        bool           batteryWarned = false;  // low-battery cue shown
        TimePoint      drainRefTime{};         // reference sample for the drain rate
        int            drainRefPct = -1;
        float          drainPctPerHour = 0.f;  // 0 = not measured yet
        // Features
        OpticalMouse   mouse;                  // touched only by this device's input thread
        std::atomic<int> mouseMode{0};         // mirror of mouse.CurrentMode() for other threads
        TimePoint      testRumbleUntil{};
        // USB cable: plugging it makes the controller drop Bluetooth on its own.
        bool           onUsb = false;
        TimePoint      lostPendingSince{};     // drop seen; 'lost' cue waits briefly for a cable
        TimePoint      usbPendingSince{};      // cable seen while connected; waiting for Bluetooth to drop
        bool           chargingOnBle = false;   // cable in but still on Bluetooth (nothing opened it over USB)
        bool           chargingByCurrent = false;   // battery current in the reports says it is charging
        int            currentStreak = 0;           // debounce for chargingByCurrent
        bool           chargingShown = false;       // chargingByCurrent || chargingOnBle, as last applied
    };
    struct Slot {
        int           player = 0;
        PVIGEM_TARGET target = nullptr;
        std::shared_ptr<VirtualPad> pad;
        uint64_t      left = 0, right = 0, solo = 0;   // member addresses
        std::mutex    mutex;                           // guards members' buffers + last
        std::vector<uint8_t> leftReport, rightReport;
        DS4_REPORT_EX last{};                          // pre-swap, for the live test
        bool          hasLast = false;
        // Rumble requested by the game, written by this slot's listener thread.
        std::atomic<float> amp{0.f};
        std::shared_ptr<std::atomic<bool>> listen = std::make_shared<std::atomic<bool>>(true);
    };
    struct Runtime {   // global options needed in the input path
        float      deadzone = 0.08f;
        bool       swap = false;
        GyroSource gyro = GyroSource::Both;
    };

    // --- DeviceManager.cpp ---
    void OnAdvertisement(uint64_t address, const AdvertisementInfo& info, int16_t rssi);
    void StartConnectLocked(const std::shared_ptr<Device>& d);
    void ConnectWorker(std::shared_ptr<Device> d, std::shared_ptr<std::atomic<bool>> cancel);
    void OnConnectFailed(const std::shared_ptr<Device>& d, const std::atomic<bool>& cancel, const std::string& why);
    bool OnConnected(const std::shared_ptr<Device>& d, const std::shared_ptr<Switch2Link>& link,
                     const std::atomic<bool>& cancel);
    void OnLinkStatus(const std::shared_ptr<Device>& d, bool connected);
    void OnUsb(uint16_t productId, bool arrived);
    void ConfirmPendingLossesLocked(TimePoint now);
    void DumpRecentLocked(Device& d, const char* why);
    void ConfirmUsbLocked(const std::shared_ptr<Device>& d);
    int SessionCountLocked(ControllerKind kind) const;
    static void RollBackBatteryLocked(Device& d, TimePoint before);
    void RefreshChargingLocked(Device& d, TimePoint now);
    bool UsbOnlyLocked(const DeviceProfile& p) const;
    void ApplyRuntimeLocked();
    void MarkDirtyLocked();
    void FlushSaveLocked(bool force);
    void Emit(EventType t, const std::string& text, uint64_t address = 0);
    void Log(const std::string& line);
    std::string NameLocked(uint64_t address, ControllerKind kind) const;

    // --- DeviceManager_Slots.cpp ---
    std::shared_ptr<Slot> AssignSlotLocked(const std::shared_ptr<Device>& d);
    std::shared_ptr<Slot> CreateSlotLocked(const std::shared_ptr<Device>& d);
    void RemoveFromSlotLocked(const std::shared_ptr<Device>& d);
    void RefreshLedsLocked(const std::shared_ptr<Slot>& s);
    void UpdateWatchdogLocked(const std::shared_ptr<Slot>& s);
    bool AnyOtherMemberConnectedLocked(const Slot& s, uint64_t except) const;
    int  NextPlayerLocked() const;
    static uint8_t LedPattern(int player);
    void StartOutputListener(const std::shared_ptr<Slot>& s);
    void UnplugSlot(const std::shared_ptr<Slot>& s);
    void RumbleLoop();

    // --- DeviceManager_Input.cpp ---
    void OnInput(const std::shared_ptr<Device>& d, std::vector<uint8_t> report);
    void UpdateStatsLocked(Device& d, TimePoint now);
    void UpdateBatteryLocked(Device& d, const std::vector<uint8_t>& report, TimePoint now);
    void UpdateDrainRateLocked(Device& d, int pct, TimePoint now);
    static int MinutesLeft(const Device& d, int pct, bool& measured);
    void HandleMouse(Device& d, const std::shared_ptr<Switch2Link>& link, std::vector<uint8_t>& report,
                     const InputConfig& cfg, uint8_t playerLeds);

    mutable std::mutex m_mutex;
    AppSettings m_settings;
    Runtime     m_runtime;
    bool        m_saveDirty = false;
    TimePoint   m_saveDue{};
    std::map<uint64_t, std::shared_ptr<Device>> m_devices;
    std::vector<std::shared_ptr<Slot>> m_slots;
    bool        m_focused = true;
    bool        m_demo = false;
    bool        m_trace = false;
    std::set<uint64_t> m_wakeLogged;
    TimePoint   m_manualSearchUntil{};

    PVIGEM_CLIENT m_vigem = nullptr;
    bool          m_vigemOk = false;
    DsuServer     m_dsu;
    BleScanner    m_scanner;
    UsbWatcher    m_usb;
    std::map<ControllerKind, int> m_usbUnmatched;   // cables plugged into controllers not in this session

    std::atomic<bool> m_running{false};
    std::thread       m_rumbleThread;
    std::atomic<int>  m_listeners{0};

    mutable std::mutex m_eventMutex;
    std::vector<Event> m_events;
    std::deque<std::string> m_log;
};

} // namespace sw2
