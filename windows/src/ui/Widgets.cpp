#include "ui/Widgets.h"
#include "core/I18n.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sw2::ui {

theme::Fonts g_fonts;
float g_dpiScale = 1.f;

using namespace theme;

float Dp(float v) { return v * g_dpiScale; }

float Animate(ImGuiID id, float target, float speed) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float* v = st->GetFloatRef(id, target);
    const float dt = ImGui::GetIO().DeltaTime;
    *v += (target - *v) * std::min(1.f, dt * speed);
    if (std::abs(*v - target) < 0.002f) *v = target;
    return *v;
}

ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32({ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                           ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t});
}

static ImGuiID SubId(ImGuiID id, int salt) { return ImHashData(&salt, sizeof(salt), id); }

// Applies the current style alpha (BeginDisabled() lowers it) to a colour.
static ImU32 A(ImU32 c) {
    const float a = ImGui::GetStyle().Alpha;
    if (a >= 1.f) return c;
    const ImU32 alpha = ImU32(((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

// ---------------------------------------------------------------------------

bool Toggle(const char* id, bool* value) {
    const float h = Dp(24.f), w = Dp(42.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##toggle", {w, h});
    const bool hovered = ImGui::IsItemHovered();
    if (clicked) *value = !*value;
    const ImGuiID iid = ImGui::GetItemID();
    const float t = Animate(iid, *value ? 1.f : 0.f);
    const float hv = Animate(SubId(iid, 1), hovered ? 1.f : 0.f, 18.f);
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 off = LerpColor(Rgb(SURFACE_3), Rgb(HOVER), hv);
    const ImU32 on  = LerpColor(Rgb(ACCENT), Rgb(ACCENT_HI), hv);
    if (hv > 0.f) dl->AddRect({p.x - Dp(2.f), p.y - Dp(2.f)}, {p.x + w + Dp(2.f), p.y + h + Dp(2.f)},
                              Rgb(*value ? ACCENT : MUTED, 0.25f * hv), h * 0.5f + Dp(2.f), 0, Dp(1.5f));
    dl->AddRectFilled(p, {p.x + w, p.y + h}, A(LerpColor(off, on, t)), h * 0.5f);
    const float r = h * 0.5f - Dp(3.f);
    const float x = p.x + h * 0.5f + t * (w - h);
    dl->AddCircleFilled({x, p.y + h * 0.5f + Dp(0.8f)}, r, IM_COL32(0, 0, 0, 45));
    dl->AddCircleFilled({x, p.y + h * 0.5f}, r, A(Rgb(0xFFFFFF)));
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

bool Button(const char* label, ButtonStyle style, ImVec2 size, const char* glyph) {
    ImGui::PushID(label);
    const char* text = label;                                // visible part ends at "##"
    const char* textEnd = ImGui::FindRenderedTextEnd(label);
    ImGui::PushFont(g_fonts.semibold, SIZE_BODY - 1.f);
    const ImVec2 ts = ImGui::CalcTextSize(text, textEnd);
    const ImVec2 gs = glyph ? ImGui::CalcTextSize(glyph) : ImVec2{0, 0};
    const float gap = glyph && ts.x > 0 ? Dp(8.f) : 0.f;
    const float contentW = ts.x + gs.x + gap;
    float w = size.x, h = size.y > 0 ? size.y : Dp(BUTTON_H);
    if (w == 0.f) w = contentW + Dp(32.f);
    else if (w < 0.f) w = ImGui::GetContentRegionAvail().x;

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##btn", {w, h});
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    const ImGuiID iid = ImGui::GetItemID();
    const float hv = Animate(iid, hovered ? 1.f : 0.f, 18.f);
    const float pr = Animate(SubId(iid, 1), held ? 1.f : 0.f, 24.f);

    ImU32 bg = 0, bgHover = 0, fg = Rgb(TEXT), outline = 0;
    switch (style) {
        case ButtonStyle::Primary:
            bg = Rgb(ACCENT); bgHover = Rgb(ACCENT_HI); fg = Rgb(0xFFFFFF); break;
        case ButtonStyle::Secondary:
            bg = Rgb(SURFACE_3); bgHover = Rgb(HOVER); outline = Rgb(0xFFFFFF, 0.10f); break;
        case ButtonStyle::Ghost:
            bg = Rgb(SURFACE_3, 0.f); bgHover = Rgb(SURFACE_3); fg = LerpColor(Rgb(MUTED), Rgb(TEXT), hv); break;
        case ButtonStyle::Danger:
            bg = Rgb(DANGER, 0.08f); bgHover = Rgb(DANGER, 0.18f); fg = Rgb(DANGER); outline = Rgb(DANGER, 0.45f); break;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = Dp(10.f);
    // Press: shrink by a pixel for tactile feedback.
    const float inset = pr * Dp(1.f);
    const ImVec2 a = {p.x + inset, p.y + inset}, b = {p.x + w - inset, p.y + h - inset};
    ImU32 fill = LerpColor(bg, bgHover, hv);
    if (style == ButtonStyle::Primary && pr > 0.f) fill = LerpColor(fill, Rgb(ACCENT), pr);
    fg = A(fg); fill = A(fill);
    if (style == ButtonStyle::Primary) dl->AddRectFilled({a.x, a.y + Dp(1.f)}, {b.x, b.y + Dp(1.f)}, A(Rgb(0x000000, 0.18f)), rounding);
    dl->AddRectFilled(a, b, fill, rounding);
    if (outline) dl->AddRect(a, b, A(LerpColor(outline, style == ButtonStyle::Danger ? Rgb(DANGER, 0.8f) : Rgb(0xFFFFFF, 0.22f), hv)), rounding, 0, 1.f);

    const float cx = p.x + (w - contentW) * 0.5f;
    const float fontH = ImGui::GetFontSize();
    if (glyph) DrawIconCentered(dl, {cx + gs.x * 0.5f, p.y + h * 0.5f}, 13.f, fg, glyph);
    if (ts.x > 0) dl->AddText({cx + gs.x + gap, p.y + (h - fontH) * 0.5f}, fg, text, textEnd);
    ImGui::PopFont();
    if (hovered && !disabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopID();
    return clicked;
}

bool IconButton(const char* id, const char* glyph, const char* tooltip, unsigned color, float size, unsigned hoverColor) {
    const float s = Dp(size);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, {s, s});
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    const float hv = Animate(iid, hovered ? 1.f : 0.f, 18.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const unsigned hc = hoverColor ? hoverColor : color;
    const ImVec2 c = {p.x + s * 0.5f, p.y + s * 0.5f};
    if (hv > 0.f) {
        // Tinted circle in the button's own colour (red X gets a red halo).
        dl->AddCircleFilled(c, s * 0.5f * (0.82f + 0.18f * hv), Rgb(hc, (held ? 0.24f : 0.14f) * hv));
    }
    DrawIconCentered(dl, c, 15.f, A(LerpColor(Rgb(color), Rgb(hoverColor ? hoverColor : TEXT), hv)), glyph);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

bool Slider(const char* id, int* value, int minV, int maxV, const char* fmt, const char* zeroLabel) {
    ImGui::PushID(id);
    char label[48];
    if (*value == 0 && zeroLabel) std::snprintf(label, sizeof(label), "%s", zeroLabel);
    else                          std::snprintf(label, sizeof(label), fmt, *value);
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    const float labelW = std::max(ImGui::CalcTextSize(label).x, Dp(60.f));
    ImGui::PopFont();

    const float total = ImGui::GetContentRegionAvail().x;
    const float trackW = total - labelW - Dp(14.f);
    const float h = Dp(24.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slider", {trackW, h});
    const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    const ImGuiID iid = ImGui::GetItemID();
    bool changed = false;
    const float knobR = Dp(8.f);
    const float x0 = p.x + knobR, x1 = p.x + trackW - knobR;
    if (active) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.f, 1.f);
        const int nv = minV + int(std::lround(t * (maxV - minV)));
        if (nv != *value) { *value = nv; changed = true; }
    }
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const float hv = Animate(SubId(iid, 1), (hovered || active) ? 1.f : 0.f, 18.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = maxV > minV ? float(*value - minV) / float(maxV - minV) : 0.f;
    const float cy = p.y + h * 0.5f, kx = x0 + t * (x1 - x0);
    const float th = Dp(4.f);
    dl->AddRectFilled({x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, A(Rgb(SURFACE_3)), th);
    dl->AddRectFilled({x0, cy - th * 0.5f}, {kx, cy + th * 0.5f}, A(Rgb(ACCENT)), th);
    if (hv > 0.f) dl->AddCircleFilled({kx, cy}, knobR + Dp(5.f) * hv, Rgb(ACCENT, 0.18f * hv));
    dl->AddCircleFilled({kx, cy + Dp(0.8f)}, knobR, IM_COL32(0, 0, 0, 50));
    dl->AddCircleFilled({kx, cy}, knobR * (active ? 1.08f : 1.f), A(Rgb(0xFFFFFF)));

    ImGui::SameLine(0, Dp(14.f));
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    const float ly = ImGui::GetCursorPosY() + (h - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPosY(ly);
    const float lw = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelW - lw);
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(*value == 0 && zeroLabel ? FAINT : TEXT));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopID();
    return changed;
}

bool Segmented(const char* id, int* value, const char* const* labels, int count, float width) {
    ImGui::PushID(id);
    const float h = Dp(32.f);
    if (width <= 0.f) width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + width, p.y + h}, Rgb(SURFACE_2), Dp(10.f));
    const float segW = width / count;
    const float t = Animate(ImGui::GetID("##sel"), float(*value), 16.f);
    const ImVec2 a = {p.x + Dp(3.f) + t * segW, p.y + Dp(3.f)};
    dl->AddRectFilled({a.x, a.y + Dp(1.f)}, {a.x + segW - Dp(6.f), p.y + h - Dp(2.f)}, Rgb(0x000000, 0.15f), Dp(8.f));
    dl->AddRectFilled(a, {a.x + segW - Dp(6.f), p.y + h - Dp(3.f)}, Rgb(SURFACE_3), Dp(8.f));
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        ImGui::SetCursorScreenPos({p.x + i * segW, p.y});
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", {segW, h}) && *value != i) { *value = i; changed = true; }
        const bool hovered = ImGui::IsItemHovered();
        const float hv = Animate(ImGui::GetItemID(), hovered ? 1.f : 0.f, 18.f);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::PopID();
        ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const ImU32 col = *value == i ? Rgb(TEXT) : LerpColor(Rgb(MUTED), Rgb(TEXT), hv * 0.7f);
        dl->AddText({p.x + i * segW + (segW - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f}, col, labels[i]);
        ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos({p.x, p.y + h});
    ImGui::Dummy({width, 0.f});
    ImGui::PopID();
    return changed;
}

void TextColored(unsigned color, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    ImGui::TextColoredV(Rgb4(color), fmt, args);
    va_end(args);
}

void TextSmall(unsigned color, const char* fmt, ...) {
    ImGui::PushFont(nullptr, SIZE_SMALL);
    va_list args; va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(color));
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
    ImGui::PopFont();
}

void TextCentered(ImFont* font, float size, unsigned color, const char* text) {
    ImGui::PushFont(font, size);
    const float w = ImGui::GetContentRegionAvail().x;
    const float tw = ImGui::CalcTextSize(text).x;
    if (tw <= w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - tw) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(color));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(color));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
}

void SectionTitle(const char* text, bool first) {
    if (!first) ImGui::Dummy({0, Dp(12.f)});   // groups breathe more than items inside them
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Dp(4.f));
    ImGui::PushFont(g_fonts.semibold, SIZE_H2 - 1.f);
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(FAINT));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy({0, Dp(0.f)});
}

void Spinner(float radius, unsigned color) {
    const float r = Dp(radius);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({r * 2, r * 2});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = float(ImGui::GetTime()) * 6.f;
    const ImVec2 c = {p.x + r, p.y + r};
    dl->AddCircle(c, r - Dp(1.5f), Rgb(color, 0.15f), 24, Dp(2.f));
    dl->PathClear();
    dl->PathArcTo(c, r - Dp(1.5f), t, t + 2.2f, 24);
    dl->PathStroke(Rgb(color), 0, Dp(2.f));
}

// ---------------------------------------------------------------------------
// Vector glyphs
// ---------------------------------------------------------------------------

void ControllerGlyph(ImDrawList* dl, ImVec2 c, float size, ControllerKind kind, unsigned color, bool filled) {
    const float s = size, th = std::max(1.2f, s * 0.055f);
    const ImU32 col = Rgb(color);
    const ImU32 inner = filled ? Rgb(SURFACE) : col;
    auto body = [&](ImVec2 a, ImVec2 b, float r) {
        if (filled) dl->AddRectFilled(a, b, col, r);
        else        dl->AddRect(a, b, col, r, 0, th);
    };
    auto dot = [&](float x, float y, float r) { dl->AddCircleFilled({c.x + x * s, c.y + y * s}, r * s, inner); };
    auto ring = [&](float x, float y, float r) {
        if (filled) dl->AddCircleFilled({c.x + x * s, c.y + y * s}, r * s, inner);
        else        dl->AddCircle({c.x + x * s, c.y + y * s}, r * s, col, 0, th);
    };
    switch (kind) {
        case ControllerKind::JoyConL:
        case ControllerKind::JoyConR: {
            const float w = 0.40f * s, h = 0.92f * s;
            body({c.x - w * 0.5f, c.y - h * 0.5f}, {c.x + w * 0.5f, c.y + h * 0.5f}, w * 0.5f);
            const float stickY = kind == ControllerKind::JoyConL ? -0.17f : 0.17f;
            const float btnY   = -stickY;
            ring(0.f, stickY, 0.085f);
            const float d = 0.075f, r = 0.032f;
            dot(0.f, btnY - d, r); dot(0.f, btnY + d, r); dot(-d, btnY, r); dot(d, btnY, r);
            break;
        }
        case ControllerKind::GameCube: {
            const float w = 0.92f * s, h = 0.58f * s;
            body({c.x - w * 0.5f, c.y - h * 0.5f}, {c.x + w * 0.5f, c.y + h * 0.5f}, h * 0.42f);
            ring(-0.22f, -0.04f, 0.095f);
            ring(0.24f, -0.02f, 0.085f);
            dot(0.10f, 0.10f, 0.032f);
            dot(0.08f, -0.12f, 0.030f);
            break;
        }
        default: {
            const float w = 0.94f * s, h = 0.56f * s;
            body({c.x - w * 0.5f, c.y - h * 0.5f}, {c.x + w * 0.5f, c.y + h * 0.5f}, h * 0.46f);
            ring(-0.22f, -0.06f, 0.08f);
            ring(0.10f, 0.09f, 0.07f);
            const float d = 0.06f, r = 0.026f, bx = 0.25f, by = -0.06f;
            dot(bx, by - d, r); dot(bx, by + d, r); dot(bx - d, by, r); dot(bx + d, by, r);
            break;
        }
    }
}

void AppLogo(ImDrawList* dl, ImVec2 o, float size) {
    // Same geometry as assets/icon.svg (viewBox 256x256).
    const float k = size / 256.f;
    auto P = [&](float x, float y) { return ImVec2{o.x + x * k, o.y + y * k}; };
    const ImU32 bg = Rgb(0x14161B), fg = Rgb(0xE8EAED), accent = Rgb(ACCENT);
    dl->AddRectFilled(P(0, 0), P(256, 256), bg, 56 * k);
    dl->AddRect(P(42, 58), P(194, 158), fg, 12 * k, 0, 12 * k);
    dl->AddLine(P(118, 170), P(118, 190), fg, 12 * k);
    dl->AddLine(P(92, 196), P(144, 196), fg, 12 * k);
    dl->AddRectFilled(P(158, 94), P(230, 226), bg, 36 * k);
    dl->AddRectFilled(P(168, 104), P(220, 216), accent, 26 * k);
    dl->AddCircleFilled(P(194, 138), 11 * k, bg);
    dl->AddCircleFilled(P(194, 182), 6 * k, bg);
}

void SignalBars(ImDrawList* dl, ImVec2 bl, float height, int16_t rssi, unsigned color) {
    const int level = rssi == 0 ? 0 : rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
    const float bw = height * 0.18f, gap = height * 0.1f;
    for (int i = 0; i < 4; ++i) {
        const float bh = height * (0.35f + 0.65f * (i / 3.f));
        const ImVec2 a = {bl.x + i * (bw + gap), bl.y - bh};
        dl->AddRectFilled(a, {a.x + bw, bl.y}, Rgb(i < level ? color : SURFACE_3), bw * 0.4f);
    }
}

void DrawIconCentered(ImDrawList* dl, ImVec2 center, float size, ImU32 color, const char* glyph) {
    // The most used navigation/action icons are drawn as vectors: exact
    // geometric centring (the font's X sat ~5 px high) and a stroke that
    // matches the rest of the UI.
    const ImVec2 c = center;
    const float k = Dp(size * 0.34f), th = Dp(std::max(1.4f, size * 0.105f));
    if (std::strcmp(glyph, icon::Close) == 0) {
        dl->AddLine({c.x - k, c.y - k}, {c.x + k, c.y + k}, color, th);
        dl->AddLine({c.x - k, c.y + k}, {c.x + k, c.y - k}, color, th);
        return;
    }
    if (std::strcmp(glyph, icon::Chevron) == 0) {
        const ImVec2 pts[3] = {{c.x - k * 0.45f, c.y - k}, {c.x + k * 0.55f, c.y}, {c.x - k * 0.45f, c.y + k}};
        dl->AddPolyline(pts, 3, color, ImDrawFlags_None, th);
        return;
    }
    if (std::strcmp(glyph, icon::Back) == 0) {
        const float kk = k * 1.3f, hd = k;
        dl->AddLine({c.x - kk, c.y}, {c.x + kk, c.y}, color, th);
        const ImVec2 head[3] = {{c.x - kk + hd, c.y - hd}, {c.x - kk, c.y}, {c.x - kk + hd, c.y + hd}};
        dl->AddPolyline(head, 3, color, ImDrawFlags_None, th);
        return;
    }
    ImFont* f = g_fonts.regular ? g_fonts.regular : ImGui::GetFont();
    const float px = size * ImGui::GetStyle().FontScaleDpi;
    unsigned int cp = 0;
    ImTextCharFromUtf8(&cp, glyph, nullptr);
    ImFontBaked* baked = f->GetFontBaked(px);
    const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback(static_cast<ImWchar>(cp)) : nullptr;
    if (!g) {   // fallback: text-box centring
        ImGui::PushFont(f, size);
        const ImVec2 ts = ImGui::CalcTextSize(glyph);
        ImGui::PopFont();
        dl->AddText(f, px, {center.x - ts.x * 0.5f, center.y - ts.y * 0.5f}, color, glyph);
        return;
    }
    const float gx = (g->X0 + g->X1) * 0.5f, gy = (g->Y0 + g->Y1) * 0.5f;
    dl->AddText(f, px, {std::round(c.x - gx), std::round(c.y - gy)}, color, glyph);
}

namespace {
constexpr float BATTERY_ICON_W = 20.f, BATTERY_NUB_W = 2.5f, BATTERY_TEXT_GAP = 6.f;
// pct < 0 while charging: the level is unknown over USB, so the label says so.
std::string BatteryLabel(int pct, bool charging) {
    if (pct < 0) return charging ? tr("Charging", "Carregando") : "";
    char b[16];
    std::snprintf(b, sizeof(b), "≈%d%%", pct);
    return b;
}
std::string SuffixLabel(const char* suffix) { return suffix && *suffix ? std::string(" · ") + suffix : std::string(); }
}

float BatteryInlineWidth(int pct, const char* suffix, bool charging) {
    if (pct < 0 && !charging) return 0.f;
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    float w = ImGui::CalcTextSize(BatteryLabel(pct, charging).c_str()).x;
    ImGui::PopFont();
    if (const std::string s = SuffixLabel(suffix); !s.empty()) {
        ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
        w += ImGui::CalcTextSize(s.c_str()).x;
        ImGui::PopFont();
    }
    return Dp(BATTERY_ICON_W) + Dp(BATTERY_NUB_W) + Dp(BATTERY_TEXT_GAP) + w;
}

float BadgeWidth(const char* text) {
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL - 1.f);
    const float w = ImGui::CalcTextSize(text).x + Dp(24.f);
    ImGui::PopFont();
    return w;
}

void Badge(const char* text, unsigned color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL - 1.f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float h = ts.y + Dp(6.f), w = ts.x + Dp(24.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, {p.x + w, p.y + h}, A(Rgb(color, 0.16f)), h * 0.5f);
    dl->AddRect(p, {p.x + w, p.y + h}, A(Rgb(color, 0.30f)), h * 0.5f, 0, 1.f);
    // Small status dot, then the label.
    const float r = Dp(3.f);
    dl->AddCircleFilled({p.x + Dp(10.f), p.y + h * 0.5f}, r, A(Rgb(color)));
    dl->AddText({p.x + Dp(16.f), p.y + (h - ts.y) * 0.5f}, A(Rgb(color)), text);
    ImGui::PopFont();
    ImGui::Dummy({w + Dp(4.f), h});
}

void KeyValueBadge(const char* label, const char* badge, unsigned color) {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float y = ImGui::GetCursorPosY();
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL - 1.f);
    const float bh = ImGui::GetTextLineHeight() + Dp(6.f);
    ImGui::PopFont();
    const float bw = BadgeWidth(badge) + Dp(4.f);
    const float rowH = std::max(bh, ImGui::GetTextLineHeight());
    ImGui::PushTextWrapPos(right - bw - Dp(12.f));
    ImGui::SetCursorPosY(y + (rowH - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(TEXT));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    const float textH = ImGui::GetItemRectSize().y;
    ImGui::SetCursorPos({right - bw, y + (std::max(rowH, textH) - bh) * 0.5f});
    Badge(badge, color);
    ImGui::SetCursorPosY(y + std::max(rowH, textH));
    ImGui::Dummy({0, 0});
}

unsigned BatteryColor(int pct) {
    return pct < 20 ? DANGER : pct < 50 ? WARNING : SUCCESS;
}

void BatteryInline(int pct, const char* suffix, bool charging, bool iconRight) {
    if (pct < 0 && !charging) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string label = BatteryLabel(pct, charging), extra = SuffixLabel(suffix);
    const unsigned col = charging ? SUCCESS : BatteryColor(pct);

    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    const float lineH = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
    const float extraW = extra.empty() ? 0.f : ImGui::CalcTextSize(extra.c_str()).x;
    ImGui::PopFont();

    const float bw = Dp(BATTERY_ICON_W), bh = Dp(10.f), nub = Dp(BATTERY_NUB_W), gapW = Dp(BATTERY_TEXT_GAP);
    const float textW = ts.x + extraW, iconW = bw + nub;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cy = p.y + lineH * 0.5f + Dp(1.f);
    const float iconX = iconRight ? p.x + textW + gapW : p.x;
    const float textX = iconRight ? p.x : p.x + iconW + gapW;

    // ---- icon: body, terminal cap, level (or charging bolt) ----
    const ImVec2 a = {iconX, cy - bh * 0.5f}, z = {iconX + bw, cy + bh * 0.5f};
    dl->AddRect(a, z, Rgb(col), Dp(3.f), 0, Dp(1.3f));
    // The cap touches the body and is rounded only on its outer side.
    dl->AddRectFilled({z.x - Dp(0.5f), cy - bh * 0.24f}, {z.x + nub, cy + bh * 0.24f}, Rgb(col), Dp(1.f),
                      ImDrawFlags_RoundCornersRight);
    if (charging) {
        // Full body with a lightning bolt cut out of it (phone style), drawn as
        // two triangles in the card colour.
        dl->AddRectFilled({a.x + Dp(2.f), a.y + Dp(2.f)}, {z.x - Dp(2.f), z.y - Dp(2.f)}, Rgb(col), Dp(1.5f));
        const float h = bh - Dp(1.f), w = h * 0.62f, x0 = (a.x + z.x) * 0.5f - w * 0.5f, y0 = cy - h * 0.5f;
        auto P = [&](float u, float t) { return ImVec2(x0 + u * w, y0 + t * h); };
        const ImU32 c = Rgb(SURFACE);
        dl->AddTriangleFilled(P(0.70f, 0.f), P(0.05f, 0.58f), P(0.55f, 0.58f), c);
        dl->AddTriangleFilled(P(0.45f, 0.42f), P(0.95f, 0.42f), P(0.30f, 1.f), c);
    } else {
        const float inner = (bw - Dp(4.f)) * std::clamp(pct / 100.f, 0.f, 1.f);
        if (inner > 0.5f) dl->AddRectFilled({a.x + Dp(2.f), a.y + Dp(2.f)}, {a.x + Dp(2.f) + inner, z.y - Dp(2.f)}, Rgb(col), Dp(1.5f));
    }

    // ---- text: level in the battery colour, extra info faint ----
    // Centred on the icon by the digits' ink (not the line box, which also
    // reserves room for descenders and would sit the text low).
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    float ty = p.y + (lineH - ts.y) * 0.5f + Dp(1.f);
    if (const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyph('8')) ty = cy - (g->Y0 + g->Y1) * 0.5f;
    dl->AddText({textX, ty}, Rgb(col), label.c_str());
    ImGui::PopFont();
    if (!extra.empty()) {
        ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
        dl->AddText({textX + ts.x, ty}, Rgb(FAINT), extra.c_str());
        ImGui::PopFont();
    }
    ImGui::Dummy({iconW + gapW + textW, lineH});
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------

namespace {
struct CardFrame { ImGuiID id; bool interactive; };
std::vector<CardFrame> g_cards;
}

void BeginCard(const char* id, float padding, bool interactive) {
    const ImGuiID gid = ImGui::GetID(id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const bool wasHovered = interactive && st->GetBool(SubId(gid, 7), false);
    const float hv = interactive ? Animate(SubId(gid, 8), wasHovered ? 1.f : 0.f, 16.f) : 0.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(LerpColor(Rgb(SURFACE), Rgb(SURFACE_2), hv * 0.7f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(LerpColor(Rgb(BORDER, 0.55f), Rgb(ACCENT, 0.45f), hv)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dp(14.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {Dp(padding), Dp(padding)});
    ImGui::BeginChild(id, {0, 0},
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    g_cards.push_back({gid, interactive});
}

bool EndCard() {
    const CardFrame f = g_cards.back();
    g_cards.pop_back();
    // Inside the card: was an inner widget under the mouse?
    const bool innerHovered = ImGui::IsAnyItemHovered();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (!f.interactive) return false;
    // After EndChild the child window is the last item; it reports hovered
    // while the mouse is anywhere over the card (inner widgets included).
    const bool hovered = ImGui::IsItemHovered();
    ImGui::GetStateStorage()->SetBool(SubId(f.id, 7), hovered && !innerHovered);
    if (hovered && !innerHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return hovered && !innerHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

} // namespace sw2::ui
