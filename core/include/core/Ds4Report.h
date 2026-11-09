#pragma once
// Platform-neutral DualShock 4 input report.
//
// The core decodes Switch 2 controllers into this report, the de facto
// lingua franca of PC gamepad tooling (Steam, DS4 drivers, DSU). Byte layout
// and field names match ViGEm's DS4_REPORT_EX (63 bytes), so the Windows app
// converts with a plain copy; other platforms map the fields to their own
// input APIs. No OS headers here.

#include <cstdint>

namespace sw2 {

namespace ds4 {
enum Button : uint16_t {
    Square        = 1 << 4,
    Cross         = 1 << 5,
    Circle        = 1 << 6,
    Triangle      = 1 << 7,
    ShoulderLeft  = 1 << 8,
    ShoulderRight = 1 << 9,
    TriggerLeft   = 1 << 10,
    TriggerRight  = 1 << 11,
    Share         = 1 << 12,
    Options       = 1 << 13,
    ThumbLeft     = 1 << 14,
    ThumbRight    = 1 << 15,
};
enum Special : uint8_t { SpecialPS = 1 << 0, SpecialTouchpad = 1 << 1 };
// D-pad occupies the low nibble of wButtons.
enum Dpad : uint8_t {
    DpadNorth = 0, DpadNorthEast = 1, DpadEast = 2, DpadSouthEast = 3,
    DpadSouth = 4, DpadSouthWest = 5, DpadWest = 6, DpadNorthWest = 7, DpadNone = 8,
};
} // namespace ds4

#pragma pack(push, 1)
struct Ds4Touch {
    uint8_t bPacketCounter;
    uint8_t bIsUpTrackingNum1;   // bit 7 set = finger up
    uint8_t bTouchData1[3];
    uint8_t bIsUpTrackingNum2;
    uint8_t bTouchData2[3];
};

struct Ds4Report {
    union {
        struct {
            uint8_t  bThumbLX, bThumbLY, bThumbRX, bThumbRY;
            uint16_t wButtons;
            uint8_t  bSpecial;
            uint8_t  bTriggerL, bTriggerR;
            uint16_t wTimestamp;
            uint8_t  bBatteryLvl;
            int16_t  wGyroX, wGyroY, wGyroZ;
            int16_t  wAccelX, wAccelY, wAccelZ;
            uint8_t  _bUnknown1[5];
            uint8_t  bBatteryLvlSpecial;
            uint8_t  _bUnknown2[2];
            uint8_t  bTouchPacketsN;
            Ds4Touch sCurrentTouch;
            Ds4Touch sPreviousTouch[2];
        } Report;
        uint8_t ReportBuffer[63];
    };
};
#pragma pack(pop)
static_assert(sizeof(Ds4Report) == 63, "Ds4Report must keep the DS4 USB report layout");

namespace ds4 {
inline void SetDpad(Ds4Report& r, Dpad d) {
    r.Report.wButtons = uint16_t((r.Report.wButtons & ~0xF) | d);
}
// Sticks centred, D-pad released, everything else zero.
inline Ds4Report Neutral() {
    Ds4Report r{};
    r.Report.bThumbLX = r.Report.bThumbLY = r.Report.bThumbRX = r.Report.bThumbRY = 0x80;
    SetDpad(r, DpadNone);
    return r;
}
} // namespace ds4

} // namespace sw2
