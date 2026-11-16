#include "platform/Autostart.h"

#include <Windows.h>

#include <string>

namespace sw2 {

namespace {
constexpr wchar_t RUN_KEY[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RUN_VALUE[] = L"SW2 Adapter";   // same name the installer uses

std::wstring Command() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return L"\"" + std::wstring(path) + L"\" " + AUTOSTART_FLAG;
}
} // namespace

bool IsAutostartEnabled() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}

bool SetAutostart(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS st;
    if (enabled) {
        const std::wstring cmd = Command();
        st = RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, RUN_VALUE);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

} // namespace sw2
