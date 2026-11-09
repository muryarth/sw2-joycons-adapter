#include "ble/BleScanner.h"

#include <chrono>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Storage::Streams;

namespace sw2 {

BleScanner::BleScanner(Handler h) : m_handler(std::move(h)) {}
BleScanner::~BleScanner() { Stop(); }

void BleScanner::Stop() {
    if (!m_watcher) return;
    try {
        m_watcher.Received(m_token);
        m_watcher.Stop();
    } catch (...) {}
    m_watcher = nullptr;
}

void BleScanner::Apply(ScanProfile p) {
    std::lock_guard<std::mutex> lk(m_mutex);
    bool running = false;
    try {
        if (m_watcher) {
            const auto st = m_watcher.Status();
            running = st == BluetoothLEAdvertisementWatcherStatus::Started ||
                      st == BluetoothLEAdvertisementWatcherStatus::Created;
        }
    } catch (...) {}
    // A watcher that Windows stopped (e.g. Bluetooth toggled off) is restarted.
    if (p == m_profile && (p == ScanProfile::Off || running)) return;
    if (m_watcher && !running) m_failed = true;
    const auto now = std::chrono::steady_clock::now();
    if (p != ScanProfile::Off && m_failed && now - m_lastAttempt < std::chrono::seconds(3)) return;
    Stop();
    m_profile = p;
    if (p == ScanProfile::Off) return;
    m_lastAttempt = now;

    try {
        BluetoothLEAdvertisementWatcher w;
        // Passive: the PC only listens; it never transmits scan requests.
        w.ScanningMode(BluetoothLEScanningMode::Passive);

        // Windows 11 24H2+: let the OS pick scan timings that coexist with the
        // active controller links. Older builds throw: fall back silently.
        if (m_coexSupported) {
            try {
                w.ScanParameters(p == ScanProfile::Coexistence
                                     ? BluetoothLEAdvertisementScanParameters::CoexistenceOptimized()
                                     : BluetoothLEAdvertisementScanParameters::LowLatency());
            } catch (...) {
                m_coexSupported = false;
            }
        }

        auto handler = m_handler;
        m_token = w.Received([handler](BluetoothLEAdvertisementWatcher const&,
                                       BluetoothLEAdvertisementReceivedEventArgs const& args) {
            try {
                auto mfg = args.Advertisement().ManufacturerData();
                for (uint32_t i = 0; i < mfg.Size(); ++i) {
                    auto sec = mfg.GetAt(i);
                    if (sec.CompanyId() != NINTENDO_COMPANY_ID) continue;
                    auto rdr = DataReader::FromBuffer(sec.Data());
                    std::vector<uint8_t> d(rdr.UnconsumedBufferLength());
                    rdr.ReadBytes(d);
                    AdvertisementInfo info;
                    if (ParseAdvertisement(d, info)) {
                        handler(args.BluetoothAddress(), info, args.RawSignalStrengthInDBm());
                        return;
                    }
                }
            } catch (...) {}
        });
        w.Start();
        m_watcher = w;
        m_failed = false;
    } catch (...) {
        // Bluetooth off / no adapter. Retried on the next profile change.
        m_failed = true;
        m_profile = ScanProfile::Off;
        m_watcher = nullptr;
    }
}

} // namespace sw2
