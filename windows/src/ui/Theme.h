#pragma once
// Visual language: dark, quiet surfaces; one warm accent (the Joy-Con of the
// app icon); colour is reserved for state (connected / waiting / error).

#include "imgui.h"

namespace sw2::theme {

inline ImU32 Rgb(unsigned hex, float a = 1.f) {
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, int(a * 255));
}
inline ImVec4 Rgb4(unsigned hex, float a = 1.f) {
    return ImVec4(((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, a);
}

constexpr unsigned BG        = 0x21222B;   // window background
constexpr unsigned SURFACE   = 0x2A2B36;   // cards
constexpr unsigned SURFACE_2 = 0x323440;   // insets, tracks, hovered cards
constexpr unsigned SURFACE_3 = 0x3C3E4B;   // controls (buttons, toggles off)
constexpr unsigned HOVER     = 0x464858;
constexpr unsigned PRESSED   = 0x515366;
constexpr unsigned BORDER    = 0x3A3C49;
// Text tokens tuned for contrast on SURFACE (#2A2B36):
// TEXT ~13:1, MUTED ~7:1, FAINT ~4.6:1 (WCAG AA for small text).
constexpr unsigned TEXT      = 0xF4F5F8;
constexpr unsigned MUTED     = 0xBCC1CC;
constexpr unsigned FAINT     = 0x969BA8;
constexpr unsigned ACCENT    = 0xFF5A5F;
constexpr unsigned ACCENT_HI = 0xFF7378;
constexpr unsigned SUCCESS   = 0x3DD68C;
constexpr unsigned WARNING   = 0xF5B544;
constexpr unsigned DANGER    = 0xF06A6A;
// Joy-Con identity: neon blue left, neon red right (like the classic pair).
constexpr unsigned JOYCON_L  = 0x14B4EB;
constexpr unsigned JOYCON_R  = ACCENT;     // same red as the other controllers
// Solid battery fills: deeper than the text tokens so white numerals stay
// readable on top of them.
constexpr unsigned BATTERY_OK   = 0x1FA463;
constexpr unsigned BATTERY_MID  = 0xD08A12;
constexpr unsigned BATTERY_LOW  = 0xE0434A;

// Segoe Fluent Icons / Segoe MDL2 Assets code points (UTF-8, needs /utf-8).
namespace icon {
constexpr const char* Bluetooth = "";
constexpr const char* Settings  = "";
constexpr const char* Back      = "";
constexpr const char* Chevron   = "";
constexpr const char* Check     = "";
constexpr const char* Close     = "";
constexpr const char* Search    = "";
constexpr const char* Refresh   = "";
constexpr const char* Delete    = "";
constexpr const char* Volume    = "";
constexpr const char* Mute      = "";
constexpr const char* Info      = "";
constexpr const char* Warning   = "";
constexpr const char* Game      = "";
constexpr const char* Link      = "";
constexpr const char* Sync      = "";
constexpr const char* Edit      = "";
constexpr const char* Copy      = "";
constexpr const char* Power     = "";
} // namespace icon

struct Fonts {
    ImFont* regular  = nullptr;   // Segoe UI + icons
    ImFont* semibold = nullptr;   // Segoe UI Semibold + icons
    ImFont* bold     = nullptr;   // Segoe UI Bold (small numerals, e.g. inside the battery)
};

// Logical sizes (scaled by DPI through style.FontScaleDpi).
constexpr float SIZE_BODY  = 15.f;
constexpr float SIZE_SMALL = 13.5f;
constexpr float SIZE_TITLE = 20.f;
constexpr float SIZE_H2    = 12.5f;

} // namespace sw2::theme
