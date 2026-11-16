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
bool  g_animating = false;

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

float ButtonFitWidth(const char* label, const char* glyph, float padding) {
    ImGui::PushFont(g_fonts.semibold, SIZE_BODY - 1.f);
    const float ts = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x;
    const float gs = glyph ? ImGui::CalcTextSize(glyph).x : 0.f;
    ImGui::PopFont();
    return ts + gs + (glyph && ts > 0 ? Dp(8.f) : 0.f) + Dp(padding);
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

    const float cx = p.x + (w - contentW) * 0.5f, cy = p.y + h * 0.5f;
    if (glyph) {
        // Icon centred by its ink. On primary buttons it is drawn twice, a
        // hair apart, for a bolder stroke (the icon font has one weight only).
        const bool strong = style == ButtonStyle::Primary;
        const float size = strong ? 14.f : 13.f;
        if (strong) DrawIconCentered(dl, {cx + gs.x * 0.5f + Dp(0.6f), cy}, size, fg, glyph);
        DrawIconCentered(dl, {cx + gs.x * 0.5f, cy}, size, fg, glyph);
    }
    if (ts.x > 0) {
        // Centre the label by the ink of its capitals (the line box also
        // reserves descender space, which made labels sit off the icon).
        float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
        if (const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyph('H')) ty = cy - (g->Y0 + g->Y1) * 0.5f;
        dl->AddText({cx + gs.x + gap, std::round(ty)}, fg, text, textEnd);
    }
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

void SectionTitle(const char* text, bool first, const char* waiting) {
    // Compact rhythm: a small gap above the title, none below it beyond the
    // regular item spacing, so the title clearly belongs to its card.
    if (first) ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Dp(4.f));
    else       ImGui::Dummy({0, Dp(2.f)});
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Dp(4.f));
    ImGui::PushFont(g_fonts.semibold, SIZE_H2 - 1.f);
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(MUTED));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    const ImVec2 tmin = ImGui::GetItemRectMin(), tmax = ImGui::GetItemRectMax();
    float cy = (tmin.y + tmax.y) * 0.5f;   // centre on the ink of the capitals
    if (const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyph('S')) cy = tmin.y + (g->Y0 + g->Y1) * 0.5f;
    ImGui::PopFont();
    if (waiting) {
        g_animating = true;
        // Right-aligned on the title line: three dots in a soft wave, then the
        // note. Reads as "in progress", calm enough to sit there for a while.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(g_fonts.semibold, SIZE_SMALL - 1.f);
        const ImVec2 ws = ImGui::CalcTextSize(waiting);
        float ty = cy - ws.y * 0.5f;
        if (const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyph('x')) ty = cy - (g->Y0 + g->Y1) * 0.5f;
        const float tx = right - Dp(4.f) - ws.x;
        dl->AddText({std::round(tx), std::round(ty)}, Rgb(ACCENT, 0.9f), waiting);
        ImGui::PopFont();
        const float r = Dp(2.2f), step = Dp(7.f), t = float(ImGui::GetTime());
        const float x0 = tx - Dp(8.f) - r - step * 2;
        for (int i = 0; i < 3; ++i) {
            const float ph = 0.5f + 0.5f * std::sin(t * 4.5f - i * 0.9f);
            dl->AddCircleFilled({x0 + i * step, cy - ph * Dp(1.5f)}, r, Rgb(ACCENT, 0.3f + 0.7f * ph));
        }
    }
}

bool TextEllipsis(ImFont* font, float size, unsigned color, const char* text, float maxWidth) {
    ImGui::PushFont(font, size);
    std::string shown = text;
    bool cut = false;
    if (maxWidth > 0.f && ImGui::CalcTextSize(text).x > maxWidth) {
        cut = true;
        const char* ell = "…";   // "..." (ellipsis)
        const float ellW = ImGui::CalcTextSize(ell).x;
        size_t n = std::strlen(text);
        while (n > 0) {
            do { --n; } while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80);   // whole UTF-8 characters
            if (ImGui::CalcTextSize(text, text + n).x + ellW <= maxWidth) break;
        }
        shown.assign(text, n);
        while (!shown.empty() && (shown.back() == ' ' || shown.back() == '.')) shown.pop_back();
        // Don't end on a dangling separator ("Connected ·…").
        if (shown.size() >= 2 && shown.compare(shown.size() - 2, 2, "\xC2\xB7") == 0) shown.resize(shown.size() - 2);
        while (!shown.empty() && shown.back() == ' ') shown.pop_back();
        shown += ell;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(color));
    ImGui::TextUnformatted(shown.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
    return cut;
}

unsigned KindColor(ControllerKind kind) {
    switch (kind) {
        case ControllerKind::JoyConL: return JOYCON_L;
        case ControllerKind::JoyConR: return JOYCON_R;
        default:                      return ACCENT;
    }
}

int SignalLevelFromRssi(int rssi) {
    return rssi == 0 ? 0 : rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
}

const char* SignalWord(int level) {
    switch (level) {
        case 4:  return tr("excellent", "excelente");
        case 3:  return tr("good", "bom");
        case 2:  return tr("fair", "razoável");
        case 1:  return tr("weak", "fraco");
        default: return tr("unknown", "desconhecido");
    }
}

void Spinner(float radius, unsigned color) {
    g_animating = true;   // keeps the frame rate up while it is on screen
    const float r = Dp(radius);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({r * 2, r * 2});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = float(ImGui::GetTime());
    const ImVec2 c = {p.x + r, p.y + r};
    const float rr = r - Dp(1.5f), th = Dp(2.2f);
    dl->AddCircle(c, rr, Rgb(color, 0.15f), 48, th);
    // Steady rotation plus an arc that gently grows and shrinks: smooth at
    // any frame rate, no visible stepping.
    const float head = t * 5.f;
    const float len = 1.4f + 1.0f * std::sin(t * 2.2f);
    dl->PathClear();
    dl->PathArcTo(c, rr, head, head + len, 40);
    dl->PathStroke(Rgb(color), 0, th);
    // Round the ends so the arc does not look cut.
    dl->AddCircleFilled({c.x + std::cos(head) * rr, c.y + std::sin(head) * rr}, th * 0.5f, Rgb(color), 12);
    dl->AddCircleFilled({c.x + std::cos(head + len) * rr, c.y + std::sin(head + len) * rr}, th * 0.5f, Rgb(color), 12);
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

void SignalBars(ImDrawList* dl, ImVec2 bl, float height, int level, unsigned color) {
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
        // The details chevron, mirrored: one caret style for "into" and "back".
        const ImVec2 pts[3] = {{c.x + k * 0.45f, c.y - k}, {c.x - k * 0.55f, c.y}, {c.x + k * 0.45f, c.y + k}};
        dl->AddPolyline(pts, 3, color, ImDrawFlags_None, th);
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
// Solid, rounded ("cozy") battery: a tinted track, a solid fill for the level
// and the percentage in bold white on top, readable on every fill colour.
constexpr float BATTERY_SIDE_PAD = 3.f, BATTERY_ICON_H = 14.f, BATTERY_NUB_W = 2.5f, BATTERY_TEXT_GAP = 7.f;
constexpr float BATTERY_PCT_SIZE = SIZE_SMALL - 2.f;   // as large as "100%" allows inside the body
// The label is only for charging (the level is unknown over USB, so the label says so).
std::string BatteryLabel(bool charging) { return charging ? tr("Charging", "Carregando") : ""; }
// While charging, the label ends in "…" that fills in one dot at a time
// (".", "..", "..."). Its width always reserves three dots so nothing jumps.
constexpr const char* CHARGING_DOTS = "...";
std::string LayoutLabel(bool charging) { return charging ? BatteryLabel(true) + CHARGING_DOTS : std::string(); }
std::string SuffixLabel(const char* suffix, bool afterLabel) {
    if (!suffix || !*suffix) return {};
    return afterLabel ? std::string(" · ") + suffix : std::string(suffix);
}
unsigned BatteryFill(int pct) { return pct < 20 ? BATTERY_LOW : pct < 50 ? BATTERY_MID : BATTERY_OK; }
// Body as wide as "100%" plus a little padding on each side: every level fits
// without the body being wider than it needs to be.
float BatteryBodyWidth() {
    ImGui::PushFont(g_fonts.bold ? g_fonts.bold : g_fonts.semibold, BATTERY_PCT_SIZE);
    const float w = ImGui::CalcTextSize("100%").x;
    ImGui::PopFont();
    return std::round(w + Dp(BATTERY_SIDE_PAD) * 2.f);
}
}

float BatteryInlineWidth(int pct, const char* suffix, bool charging) {
    if (pct < 0 && !charging) return 0.f;
    const std::string label = LayoutLabel(charging), extra = SuffixLabel(suffix, !label.empty());
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    float w = ImGui::CalcTextSize(label.c_str()).x;
    ImGui::PopFont();
    if (!extra.empty()) {
        ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
        w += ImGui::CalcTextSize(extra.c_str()).x;
        ImGui::PopFont();
    }
    return BatteryBodyWidth() + Dp(BATTERY_NUB_W) + (w > 0.f ? Dp(BATTERY_TEXT_GAP) + w : 0.f);
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

void Segments(std::initializer_list<Seg> segs) {
    ImGui::BeginGroup();
    bool firstSeg = true;
    for (const Seg& sg : segs) {
        if (!firstSeg) ImGui::SameLine(0, 0);
        firstSeg = false;
        ImGui::PushFont(sg.strong ? g_fonts.semibold : g_fonts.regular, SIZE_SMALL);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(sg.color));
        ImGui::TextUnformatted(sg.text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
}

void BatteryTooltip(int pct, const char* remaining, int millivolts, const char* note) {
    if (!ImGui::BeginTooltip()) return;
    const unsigned col = BatteryColor(pct), fill = BatteryFill(pct);
    const float w = Dp(note ? 250.f : 200.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // "90%" as the headline, the caption on its baseline.
    char num[8];
    std::snprintf(num, sizeof(num), "%d%%", std::clamp(pct, 0, 100));
    ImGui::PushFont(g_fonts.bold ? g_fonts.bold : g_fonts.semibold, SIZE_TITLE + 4.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 ns = ImGui::CalcTextSize(num);
    const float big = ImGui::GetFontSize();
    dl->AddText(p, Rgb(col), num);
    ImGui::PopFont();
    ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
    const float small = ImGui::GetFontSize();
    dl->AddText({p.x + ns.x + Dp(8.f), p.y + (big - small) * 0.80f}, Rgb(MUTED), tr("estimated battery", "bateria estimada"));
    ImGui::PopFont();
    ImGui::Dummy({w, ns.y});

    // Level bar.
    const ImVec2 b = ImGui::GetCursorScreenPos();
    const float bh = Dp(5.f);
    dl->AddRectFilled(b, {b.x + w, b.y + bh}, Rgb(fill, 0.25f), bh * 0.5f);
    dl->AddRectFilled(b, {b.x + std::max(w * std::clamp(pct, 0, 100) / 100.f, bh), b.y + bh}, Rgb(fill), bh * 0.5f);
    ImGui::Dummy({w, bh + Dp(4.f)});

    if (remaining && remaining[0]) Segments({{tr("about ", "cerca de "), FAINT}, {remaining, TEXT, true}, {tr(" left", " restantes"), FAINT}});
    TextSmall(FAINT, "%.2f V", millivolts / 1000.f);
    if (note) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        TextSmall(MUTED, "%s", note);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
}

void BatteryInline(int pct, const char* suffix, bool charging, bool iconRight) {
    if (pct < 0 && !charging) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string label = BatteryLabel(charging), extra = SuffixLabel(suffix, !label.empty());
    const unsigned col = charging ? SUCCESS : BatteryColor(pct);       // label text
    const unsigned fill = charging ? BATTERY_OK : BatteryFill(pct);    // icon

    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    const ImVec2 ts = ImGui::CalcTextSize(LayoutLabel(charging).c_str());   // reserves the three dots
    const float lineH = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::PushFont(g_fonts.regular, SIZE_SMALL);
    const float extraW = extra.empty() ? 0.f : ImGui::CalcTextSize(extra.c_str()).x;
    ImGui::PopFont();

    const float bw = BatteryBodyWidth(), bh = Dp(BATTERY_ICON_H), nub = Dp(BATTERY_NUB_W);
    const float textW = ts.x + extraW, iconW = bw + nub, gapW = textW > 0.f ? Dp(BATTERY_TEXT_GAP) : 0.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cy = std::round(p.y + lineH * 0.5f + Dp(1.f));
    const float iconX = std::round(iconRight ? p.x + textW + gapW : p.x);
    const float textX = iconRight ? p.x : p.x + iconW + gapW;

    // ---- icon: track, solid level, rounded cap ----
    const ImVec2 a = {iconX, cy - bh * 0.5f}, z = {iconX + bw, cy + bh * 0.5f};
    const float round = bh * 0.32f;
    const float level = charging ? 1.f : std::clamp(pct / 100.f, 0.f, 1.f);
    dl->AddRectFilled(a, z, Rgb(fill, 0.30f), round);
    if (level > 0.f) {   // the fill keeps the body's rounding on the left and a straight edge on the right
        dl->PushClipRect(a, {a.x + std::max(bw * level, Dp(3.f)), z.y}, true);
        dl->AddRectFilled(a, z, Rgb(fill), round);
        dl->PopClipRect();
    }
    dl->AddRectFilled({z.x + Dp(1.f), cy - bh * 0.20f}, {z.x + nub, cy + bh * 0.20f},
                      Rgb(fill, level >= 0.995f ? 1.f : 0.55f), Dp(1.5f), ImDrawFlags_RoundCornersRight);
    if (charging) {
        // White lightning bolt on the full body: one concave shape (two
        // overlapping triangles left anti-aliased seams inside it) on whole
        // pixels, so its edges stay crisp.
        // Sized to the body, with a waist wide enough to stay solid white.
        const float h = std::round(bh - Dp(4.f)), w = std::round(h * 0.78f);   // same margin above and below
        const float x0 = std::round((a.x + z.x - w) * 0.5f), y0 = a.y + std::round((bh - h) * 0.5f);
        auto P = [&](float u, float t) { return ImVec2(x0 + u * w, y0 + t * h); };
        // Clockwise on screen: the anti-aliased fringe is built for that winding
        // (the other way round it grows inwards and the edges come out jagged).
        // Blunt top (a needle point vanishes at this size), pointed bottom tip.
        const ImVec2 bolt[] = {P(0.48f, 0.00f), P(0.84f, 0.00f), P(0.62f, 0.38f), P(1.00f, 0.38f),
                               P(0.22f, 1.00f), P(0.40f, 0.62f), P(0.00f, 0.62f)};
        dl->AddConcavePolyFilled(bolt, 7, Rgb(0xFFFFFF));
    } else {
        // "90%" in bold white, with a soft shadow for contrast on light fills.
        char num[8];
        std::snprintf(num, sizeof(num), "%d%%", std::clamp(pct, 0, 100));
        ImGui::PushFont(g_fonts.bold ? g_fonts.bold : g_fonts.semibold, BATTERY_PCT_SIZE);
        const ImVec2 ns = ImGui::CalcTextSize(num);
        float ny = cy - ns.y * 0.5f;
        if (const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyph('8')) ny = cy - (g->Y0 + g->Y1) * 0.5f;
        const ImVec2 at = {std::round((a.x + z.x - ns.x) * 0.5f), std::round(ny)};
        dl->AddText({at.x, at.y + Dp(1.f)}, Rgb(0x000000, 0.28f), num);
        dl->AddText(at, Rgb(0xFFFFFF), num);
        ImGui::PopFont();
    }

    // ---- text: charging label in the battery colour, extra info faint ----
    // Centred on the icon by the digits' ink (not the line box, which also
    // reserves room for descenders and would sit the text low).
    ImGui::PushFont(g_fonts.semibold, SIZE_SMALL);
    float ty = p.y + (lineH - ts.y) * 0.5f + Dp(1.f);
    if (const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyph('8')) ty = cy - (g->Y0 + g->Y1) * 0.5f;
    if (!label.empty()) {
        std::string shown = label;
        if (charging) {
            g_animating = true;
            const int dots = int(ImGui::GetTime() * 2.5) % 4;   // 0..3, about 1.6 s per cycle
            shown.append(CHARGING_DOTS, dots);
        }
        dl->AddText({textX, ty}, Rgb(col), shown.c_str());
    }
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
struct CardFrame { ImGuiID id; bool interactive; float hover; };
constexpr float CARD_HOVER_SCALE = 0.015f;   // +1.5 % when hovered
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
    g_cards.push_back({gid, interactive, hv});
}

bool EndCard() {
    const CardFrame f = g_cards.back();
    g_cards.pop_back();
    // Inside the card: was an inner widget under the mouse?
    const bool innerHovered = ImGui::IsAnyItemHovered();
    // Hover: the whole card grows a little around its centre, eased by the
    // same value as its colours. Everything the card drew this frame is in its
    // own draw list, so scaling those vertices (and their clip rectangles)
    // scales the card as one piece.
    if (f.interactive) {
        const float hv = f.hover;
        if (hv > 0.002f) {
            if (hv < 0.998f) g_animating = true;
            const float e = hv * hv * (3.f - 2.f * hv);   // ease in-out
            const float k = 1.f + CARD_HOVER_SCALE * e;
            const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            const ImVec2 c = {wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f};
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (ImDrawVert& v : dl->VtxBuffer) {
                v.pos.x = c.x + (v.pos.x - c.x) * k;
                v.pos.y = c.y + (v.pos.y - c.y) * k;
            }
            for (ImDrawCmd& cmd : dl->CmdBuffer) {
                cmd.ClipRect.x = std::floor(c.x + (cmd.ClipRect.x - c.x) * k);
                cmd.ClipRect.y = std::floor(c.y + (cmd.ClipRect.y - c.y) * k);
                cmd.ClipRect.z = std::ceil(c.x + (cmd.ClipRect.z - c.x) * k);
                cmd.ClipRect.w = std::ceil(c.y + (cmd.ClipRect.w - c.y) * k);
            }
        }
    }
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
