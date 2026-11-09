#pragma once
// Joy-Con 2 (R) optical sensor as a Windows mouse.
//
// The Chat button cycles Off -> Fast -> Normal -> Slow. While active:
// optical motion moves the cursor, R = left click, ZR = right click,
// stick press = middle click, stick up/down scrolls and pushing it fully
// left/right sends Back/Forward. Those inputs are removed from the report
// so the virtual gamepad does not see them.

#include "core/Protocol.h"

#include <cstdint>
#include <vector>

namespace sw2 {

class OpticalMouse {
public:
    enum Mode { Off = 0, Fast = 1, Normal = 2, Slow = 3, ModeCount = 4 };

    // Processes one input report of a right Joy-Con. Returns true when the
    // Chat button changed the mode (caller gives feedback: LEDs, sound).
    bool Process(std::vector<uint8_t>& report, const StickCalibration& stickCal);

    // Turns the feature off immediately, releasing any held mouse button.
    void Disable();

    Mode CurrentMode() const { return m_mode; }
    static uint8_t ModeLedPattern(Mode m);   // feedback on the controller LEDs

private:
    void ReleaseButtons();
    void ApplyMotion(int16_t opticalX, int16_t opticalY);
    void ApplyButtons(uint32_t buttons);
    void ApplyStick(float x, float y);
    static void HideFromGamepad(std::vector<uint8_t>& report);

    Mode    m_mode = Off;
    bool    m_chatWasDown = false;
    bool    m_hasLastPosition = false;
    int16_t m_lastX = 0, m_lastY = 0;
    float   m_scrollAccumulator = 0.f;
    bool    m_leftDown = false, m_rightDown = false, m_middleDown = false;
    bool    m_backSent = false, m_forwardSent = false;
};

} // namespace sw2
