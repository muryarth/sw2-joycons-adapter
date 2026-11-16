#pragma once
// Small set of custom widgets drawn with ImDrawList. All interactive widgets
// ease their hover/press states instead of switching colours abruptly.

#include "core/Protocol.h"
#include "ui/Theme.h"

#include <initializer_list>

namespace sw2::ui {

extern theme::Fonts g_fonts;
extern float g_dpiScale;
extern bool  g_animating;   // set by animated widgets drawn this frame (frame pacing)

float Dp(float v);   // logical px -> physical px (DPI)

// Eases a value stored per ImGui id towards `target`.
float Animate(ImGuiID id, float target, float speed = 14.f);
ImU32 LerpColor(ImU32 a, ImU32 b, float t);

// iOS/Android-style pill switch. Returns true when toggled.
bool Toggle(const char* id, bool* value);

enum class ButtonStyle { Primary, Secondary, Ghost, Danger };
constexpr float BUTTON_H = 34.f;
// size.x: 0 = fit label, <0 = fill available width. size.y: 0 = BUTTON_H.
bool Button(const char* label, ButtonStyle style = ButtonStyle::Secondary, ImVec2 size = {0, 0}, const char* glyph = nullptr);
// Width of a button fitted to its label; `padding` is the total side space
// (Button() itself uses 32 when size.x = 0).
float ButtonFitWidth(const char* label, const char* glyph = nullptr, float padding = 32.f);
bool IconButton(const char* id, const char* glyph, const char* tooltip, unsigned color = theme::MUTED,
                float size = 32.f, unsigned hoverColor = 0);

// Thin track + round knob; the value label sits on the right. `fmt` receives
// the value (e.g. "%d%%"); `zeroLabel` replaces it when the value is 0.
bool Slider(const char* id, int* value, int minV, int maxV, const char* fmt, const char* zeroLabel = nullptr);

// Segmented control. Returns true when the selection changes.
bool Segmented(const char* id, int* value, const char* const* labels, int count, float width = 0.f);

// Text helpers
void TextColored(unsigned color, const char* fmt, ...);
void TextSmall(unsigned color, const char* fmt, ...);
void TextCentered(ImFont* font, float size, unsigned color, const char* text);
// `first`: the first title of a page sits right under the header, with the
// same gap every page uses before its first card (no between-section space).
// `waiting` (optional): animated dots and a short note after the title.
void SectionTitle(const char* text, bool first = false, const char* waiting = nullptr);
// One line of text cut with "…" to fit `maxWidth`; the full text shows as a
// tooltip when it was cut. Returns true when it was cut.
bool TextEllipsis(ImFont* font, float size, unsigned color, const char* text, float maxWidth);
// Identity colour of a controller (Joy-Con L blue, R red, others the accent).
unsigned KindColor(ControllerKind kind);
int  SignalLevelFromRssi(int rssi);    // 0..4 bars
const char* SignalWord(int level);     // "excellent" / "good" / ...
void Spinner(float radius, unsigned color);

// Status badge: tinted pill with coloured text (inline item).
float BadgeWidth(const char* text);
void  Badge(const char* text, unsigned color);
// "Label ............ [badge]" row, badge right-aligned and centred.
void  KeyValueBadge(const char* label, const char* badge, unsigned color);

// Vector glyphs
void ControllerGlyph(ImDrawList* dl, ImVec2 center, float size, ControllerKind kind, unsigned color, bool filled);
void AppLogo(ImDrawList* dl, ImVec2 topLeft, float size);
void SignalBars(ImDrawList* dl, ImVec2 bottomLeft, float height, int level, unsigned color);   // level 0..4
// Draws an icon glyph centred by its real ink bounds (Segoe icon glyphs
// have uneven side bearings, so text-box centring looks off).
void DrawIconCentered(ImDrawList* dl, ImVec2 center, float size, ImU32 color, const char* glyph);

// Inline battery indicator, coloured by level, with the percentage drawn inside
// the icon. pct < 0: nothing (unless charging).
// `suffix` (optional, e.g. "5h 07min") is drawn next to the icon in a softer tone.
void BatteryInline(int pct, const char* suffix = nullptr, bool charging = false, bool iconRight = false);
float BatteryInlineWidth(int pct, const char* suffix = nullptr, bool charging = false);
// One line made of differently styled pieces (label muted, value highlighted).
struct Seg { const char* text; unsigned color; bool strong = false; };
void Segments(std::initializer_list<Seg> segs);
unsigned BatteryColor(int pct);
// Tooltip for a battery reading: the percentage big and in the level colour
// over a level bar, then the remaining time, the voltage and an optional note.
void BatteryTooltip(int pct, const char* remaining, int millivolts, const char* note);

// Card container. `interactive` cards highlight on hover and EndCard()
// returns true when the card background (not an inner widget) is clicked.
void BeginCard(const char* id, float padding = 14.f, bool interactive = false);
bool EndCard();

} // namespace sw2::ui
