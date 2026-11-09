#pragma once
// Notifies when a Nintendo controller is plugged in / unplugged over USB.
//
// Plugging the cable makes a Switch 2 controller drop its Bluetooth link and
// switch to USB on its own; this lets the app explain that to the user
// instead of reporting a generic connection loss. Event driven
// (CM_Register_Notification): no polling.

#include <cstdint>
#include <functional>

namespace sw2 {

class UsbWatcher {
public:
    // productId: the USB PID (same values as the BLE advertisement).
    using Handler = std::function<void(uint16_t productId, bool arrived)>;

    explicit UsbWatcher(Handler handler);
    ~UsbWatcher();
    UsbWatcher(const UsbWatcher&) = delete;
    UsbWatcher& operator=(const UsbWatcher&) = delete;

    bool Start();   // also reports controllers already plugged in
    void Stop();

    // Called by the Windows notification callback (and for the initial scan).
    void HandleInterface(const wchar_t* symbolicLink, bool arrived);

private:
    Handler m_handler;
    void*   m_registration = nullptr;   // HCMNOTIFICATION
};

} // namespace sw2
