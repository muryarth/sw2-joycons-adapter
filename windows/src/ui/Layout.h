#pragma once
// Layout building blocks shared by the pages.

#include <string>

namespace sw2::ui {

// Title + optional description on the left, pill toggle on the right. Returns true when toggled.
bool  ToggleRow(const char* id, const char* title, const char* desc, bool* value);
// Field label placed tight above its control.
void  Label(const char* text);
// Thin separator between rows of a card.
void  Divider();
// Width for two equal buttons side by side on the current row.
float HalfWidth();
// Opens a URL in the default browser.
void  OpenUrl(const std::string& url);
// Remaining time: compact "33h" / "45min", or full "33 h 10 min". Rounded to
// 10 min above one hour (it is an estimate). Empty when unknown (< 0).
std::string FormatDuration(int minutes, bool compact);

} // namespace sw2::ui
