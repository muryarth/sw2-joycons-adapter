#include "app/OpticalMouse.h"

#include <Windows.h>

#include <cmath>

namespace sw2 {

namespace {
constexpr float SPEED[OpticalMouse::ModeCount] = {0.f, 1.0f, 0.6f, 0.3f};
constexpr float SCROLL_DEADZONE     = 0.12f;   // stick travel before scrolling starts
constexpr float SCROLL_SPEED        = 40.f;    // wheel units per report at full tilt
constexpr int   WHEEL_DELTA_UNITS   = 120;     // one wheel notch (Windows WHEEL_DELTA)
constexpr float SIDE_BUTTON_TRIGGER = 0.85f;   // stick push for Back / Forward

void SendMouse(DWORD flags, DWORD data = 0, LONG dx = 0, LONG dy = 0) {
    INPUT ip{};
    ip.type = INPUT_MOUSE;
    ip.mi.dwFlags = flags;
    ip.mi.mouseData = data;
    ip.mi.dx = dx;
    ip.mi.dy = dy;
    SendInput(1, &ip, sizeof(ip));
}

void Click(DWORD xbutton) {
    SendMouse(MOUSEEVENTF_XDOWN, xbutton);
    SendMouse(MOUSEEVENTF_XUP, xbutton);
}
} // namespace

uint8_t OpticalMouse::ModeLedPattern(Mode m) {
    static const uint8_t leds[ModeCount] = {0x00, 0x08, 0x04, 0x02};
    return leds[m];
}

bool OpticalMouse::Process(std::vector<uint8_t>& report, const StickCalibration& stickCal) {
    const uint32_t buttons = JoyConButtonState(report, /*isLeft*/ false);
    const bool chatDown = (buttons & JC_R_CHAT) != 0;
    bool modeChanged = false;
    if (chatDown && !m_chatWasDown) {
        m_mode = static_cast<Mode>((m_mode + 1) % ModeCount);
        ReleaseButtons();
        m_hasLastPosition = false;
        modeChanged = true;
    }
    m_chatWasDown = chatDown;

    if (m_mode == Off) return modeChanged;

    const auto [ox, oy] = OpticalSensor(report);
    ApplyMotion(ox, oy);
    ApplyButtons(buttons);
    float sx, sy;
    StickValue(report, /*leftStick*/ false, stickCal, StickTuning{}, sx, sy);
    ApplyStick(sx, sy);
    HideFromGamepad(report);
    return modeChanged;
}

void OpticalMouse::Disable() {
    ReleaseButtons();
    m_mode = Off;
    m_hasLastPosition = false;
}

void OpticalMouse::ReleaseButtons() {
    if (m_leftDown)   { SendMouse(MOUSEEVENTF_LEFTUP);   m_leftDown = false; }
    if (m_rightDown)  { SendMouse(MOUSEEVENTF_RIGHTUP);  m_rightDown = false; }
    if (m_middleDown) { SendMouse(MOUSEEVENTF_MIDDLEUP); m_middleDown = false; }
    m_scrollAccumulator = 0.f;
}

void OpticalMouse::ApplyMotion(int16_t x, int16_t y) {
    if (!m_hasLastPosition) {
        m_lastX = x; m_lastY = y;
        m_hasLastPosition = true;
        return;
    }
    const int16_t dx = int16_t(x - m_lastX), dy = int16_t(y - m_lastY);   // wraps like the sensor counter
    m_lastX = x; m_lastY = y;
    if (dx || dy) SendMouse(MOUSEEVENTF_MOVE, 0, LONG(dx * SPEED[m_mode]), LONG(dy * SPEED[m_mode]));
}

void OpticalMouse::ApplyButtons(uint32_t buttons) {
    auto edge = [](bool down, bool& state, DWORD pressFlag, DWORD releaseFlag) {
        if (down == state) return;
        SendMouse(down ? pressFlag : releaseFlag);
        state = down;
    };
    edge((buttons & JC_R_R) != 0,     m_leftDown,   MOUSEEVENTF_LEFTDOWN,   MOUSEEVENTF_LEFTUP);
    edge((buttons & JC_R_ZR) != 0,    m_rightDown,  MOUSEEVENTF_RIGHTDOWN,  MOUSEEVENTF_RIGHTUP);
    edge((buttons & JC_R_STICK) != 0, m_middleDown, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);
}

void OpticalMouse::ApplyStick(float x, float y) {
    if (std::abs(y) > SCROLL_DEADZONE) {
        m_scrollAccumulator += y * SCROLL_SPEED;
        if (std::abs(m_scrollAccumulator) >= WHEEL_DELTA_UNITS) {
            const int notches = int(m_scrollAccumulator / WHEEL_DELTA_UNITS);
            m_scrollAccumulator -= float(notches * WHEEL_DELTA_UNITS);
            SendMouse(MOUSEEVENTF_WHEEL, DWORD(notches * WHEEL_DELTA_UNITS));
        }
    } else {
        m_scrollAccumulator = 0.f;
    }
    // Back / Forward fire once per push.
    if (x < -SIDE_BUTTON_TRIGGER) { if (!m_backSent) { Click(XBUTTON1); m_backSent = true; } }
    else m_backSent = false;
    if (x > SIDE_BUTTON_TRIGGER)  { if (!m_forwardSent) { Click(XBUTTON2); m_forwardSent = true; } }
    else m_forwardSent = false;
}

void OpticalMouse::HideFromGamepad(std::vector<uint8_t>& report) {
    if (report.size() < 16) return;
    report[4] &= uint8_t(~0xC0);                              // R, ZR
    report[5] &= uint8_t(~0x04);                              // stick press
    report[13] = 0x00; report[14] = 0x08; report[15] = 0x80;  // stick centred (2048, 2048)
}

} // namespace sw2
