#pragma once
// One GATT connection to a Switch 2 controller.
// Every outgoing command is checked against sw2::IsCommandAllowed().
//
// Connection order mirrors joycon2cpp (known to work on real hardware):
//   open -> enable command-response notifications -> preferred connection
//   params -> vendor init -> subscribe input -> (after data flows) keep-alive.

#include "core/Protocol.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sw2 {

class Switch2Link : public std::enable_shared_from_this<Switch2Link> {
public:
    using InputHandler  = std::function<void(const std::vector<uint8_t>&)>;
    using StatusHandler = std::function<void(bool connected)>;
    using LogFn         = std::function<void(const std::string&)>;

    // Blocking: opens the device, discovers characteristics and enables the
    // command-response notifications. Returns nullptr on failure.
    static std::shared_ptr<Switch2Link> Open(uint64_t address, ControllerKind kind, std::string& error,
                                             const std::atomic<bool>* cancel, const LogFn& log);
    ~Switch2Link();

    uint64_t       Address() const { return m_address; }
    ControllerKind Kind() const    { return m_kind; }

    // Blocking (~1.3 s): the vendor init sequence the console sends on connect.
    void SendInit();

    // Subscribes to input notifications and connection status. Returns false
    // only if the subscription itself threw; the CCCD write status is
    // reported through the log (some controllers stream even when Windows
    // reports a non-success status, as joycon2cpp relied on).
    bool Start(InputHandler onInput, StatusHandler onStatus, const LogFn& log);

    uint32_t PacketCount() const { return m_packets.load(); }
    void     KeepAlive();   // GattSession::MaintainConnection(true)

    void SetPlayerLeds(uint8_t pattern);   // async
    void Identify();                        // short vibration sample, async

    // Rumble with back-pressure: at most one write in flight. Returns false
    // when skipped (caller retries next tick).
    bool Rumble(float amp);

    // Release the BLE link (the controller is NOT paired with this PC, so it
    // does not try to come back on its own).
    void Close();

private:
    Switch2Link() = default;
    bool WriteRaw(const winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic& ch,
                  const std::vector<uint8_t>& bytes, bool waitResult);
    bool SendGenericCommand(const Packet& cmd);
    void EnableNotify(const winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic& ch,
                      winrt::event_token& token, const char* name, const LogFn& log);

    uint64_t       m_address = 0;
    ControllerKind m_kind = ControllerKind::Unknown;

    winrt::Windows::Devices::Bluetooth::BluetoothLEDevice m_device{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattSession m_session{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_input{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_write{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_command{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_vibration{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_respBasic{nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_respExt{nullptr};

    winrt::event_token m_inputToken{};
    winrt::event_token m_statusToken{};
    winrt::event_token m_respBasicToken{};
    winrt::event_token m_respExtToken{};

    std::mutex         m_mutex;
    std::atomic<bool>  m_closed{false};
    std::atomic<uint32_t> m_packets{0};
    std::shared_ptr<std::atomic<bool>> m_rumbleBusy = std::make_shared<std::atomic<bool>>(false);
    uint8_t            m_rumbleCounter = 0;
    bool               m_rumbleActive  = false;
};

} // namespace sw2
