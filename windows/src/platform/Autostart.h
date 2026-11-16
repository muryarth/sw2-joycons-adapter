#pragma once
// "Start with Windows": a value in HKCU\...\CurrentVersion\Run that launches
// the app hidden in the notification area (--tray). The installer writes the
// same value when its checkbox is ticked, so both stay in sync.

namespace sw2 {

constexpr wchar_t AUTOSTART_FLAG[] = L"--tray";

bool IsAutostartEnabled();
bool SetAutostart(bool enabled);   // false on registry failure

} // namespace sw2
