#pragma once
// Passive BLE advertisement scanner for Switch 2 controllers.

#include "core/Protocol.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>

#include <chrono>
#include <functional>
#include <mutex>

namespace sw2 {

enum class ScanProfile {
    Off,
    Fast,          // nothing connected: aggressive scanning, devices appear instantly
    Coexistence,   // controllers connected: let the radio favour active links
};

class BleScanner {
public:
    using Handler = std::function<void(uint64_t address, const AdvertisementInfo& info, int16_t rssi)>;

    explicit BleScanner(Handler h);
    ~BleScanner();

    // Cheap to call every frame; only restarts the watcher on change.
    void Apply(ScanProfile p);
    ScanProfile Current() const { return m_profile; }
    bool CoexistenceSupported() const { return m_coexSupported; }
    bool Failed() const { return m_failed; }

private:
    void Stop();

    Handler m_handler;
    winrt::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher m_watcher{nullptr};
    winrt::event_token m_token{};
    ScanProfile m_profile = ScanProfile::Off;
    bool m_coexSupported = true;
    bool m_failed = false;
    std::chrono::steady_clock::time_point m_lastAttempt{};
    std::mutex m_mutex;
};

} // namespace sw2
