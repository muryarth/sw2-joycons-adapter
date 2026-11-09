#include "platform/UsbWatcher.h"

#include <Windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <usbiodef.h>   // GUID_DEVINTERFACE_USB_DEVICE

#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace sw2 {

namespace {
constexpr uint16_t NINTENDO_VID = 0x057E;

// "\\?\USB#VID_057E&PID_2069#00#{...}" -> VID/PID
bool ParseVidPid(const wchar_t* link, uint16_t& vid, uint16_t& pid) {
    std::wstring s(link);
    for (auto& c : s) c = wchar_t(std::towupper(c));
    const auto v = s.find(L"VID_"), p = s.find(L"PID_");
    if (v == std::wstring::npos || p == std::wstring::npos) return false;
    vid = uint16_t(std::wcstoul(s.c_str() + v + 4, nullptr, 16));
    pid = uint16_t(std::wcstoul(s.c_str() + p + 4, nullptr, 16));
    return true;
}

DWORD CALLBACK OnNotify(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action,
                        PCM_NOTIFY_EVENT_DATA data, DWORD) {
    if (action != CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL && action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
        return ERROR_SUCCESS;
    static_cast<UsbWatcher*>(context)->HandleInterface(data->u.DeviceInterface.SymbolicLink,
                                                       action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL);
    return ERROR_SUCCESS;
}
} // namespace

UsbWatcher::UsbWatcher(Handler handler) : m_handler(std::move(handler)) {}
UsbWatcher::~UsbWatcher() { Stop(); }

void UsbWatcher::HandleInterface(const wchar_t* link, bool arrived) {
    uint16_t vid = 0, pid = 0;
    if (ParseVidPid(link, vid, pid) && vid == NINTENDO_VID && m_handler) m_handler(pid, arrived);
}

bool UsbWatcher::Start() {
    if (m_registration) return true;
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_USB_DEVICE;
    HCMNOTIFICATION reg = nullptr;
    if (CM_Register_Notification(&filter, this, OnNotify, &reg) != CR_SUCCESS) return false;
    m_registration = reg;

    // Controllers already plugged in when the app starts.
    ULONG len = 0;
    GUID guid = GUID_DEVINTERFACE_USB_DEVICE;
    if (CM_Get_Device_Interface_List_SizeW(&len, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS && len > 1) {
        std::vector<wchar_t> list(len);
        if (CM_Get_Device_Interface_ListW(&guid, nullptr, list.data(), len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS)
            for (const wchar_t* p = list.data(); *p; p += std::wcslen(p) + 1) HandleInterface(p, true);
    }
    return true;
}

void UsbWatcher::Stop() {
    if (!m_registration) return;
    CM_Unregister_Notification(static_cast<HCMNOTIFICATION>(m_registration));
    m_registration = nullptr;
}

} // namespace sw2
