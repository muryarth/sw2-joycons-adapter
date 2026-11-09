#pragma once
// Switch 2 controller protocol: advertisement parsing, input decoding into a
// DualShock 4 report, and the (allow-listed) host->controller commands.
//
// Button masks, report offsets and init sequences are ported from joycon2cpp
// (MIT, (c) 2025 Frano), which is known to work with real hardware.

#include "core/Ds4Report.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sw2 {

// ---- Controller identity -------------------------------------------------

enum class ControllerKind { Unknown, JoyConL, JoyConR, ProController, GameCube };

constexpr uint16_t NINTENDO_COMPANY_ID = 0x0553;
constexpr uint16_t PID_JOYCON_R   = 0x2066;
constexpr uint16_t PID_JOYCON_L   = 0x2067;
constexpr uint16_t PID_PRO        = 0x2069; // community RE, not confirmed on hardware
constexpr uint16_t PID_GAMECUBE   = 0x2073;

struct AdvertisementInfo {
    ControllerKind kind = ControllerKind::Unknown;
    uint16_t productId  = 0;
    // true  = pairing/sync advertisement (host address bytes are zero)
    // false = "wake" advertisement aimed at the console it is paired with.
    //         Never connect to those: the user wants to play on the Switch 2.
    bool syncMode = false;
};

// `data` is the manufacturer data payload (company id NOT included), as
// returned by WinRT BluetoothLEManufacturerData::Data().
bool ParseAdvertisement(const std::vector<uint8_t>& data, AdvertisementInfo& out);

ControllerKind KindFromProductId(uint16_t pid);
const char* KindName(ControllerKind k);
inline bool IsJoyCon(ControllerKind k) { return k == ControllerKind::JoyConL || k == ControllerKind::JoyConR; }

// ---- Calibration & options -----------------------------------------------

struct StickCalibration {
    bool calibrated = false;
    int centerX = 2048, centerY = 2048;
    int minX = 0, maxX = 4095, minY = 0, maxY = 4095;
};

enum class Orientation { Upright, Sideways };
enum class GyroSource  { Both, Left, Right };

struct StickTuning {
    float deadzone = 0.08f;   // 0..0.5
};

struct JoyConMapping {
    Orientation orientation = Orientation::Sideways;
    bool dpadAsDpad   = true;  // left Joy-Con: arrows -> D-pad (else face buttons)
    bool remapSideways = true; // sideways: rotate face buttons to match the grip
};

enum class ButtonMapping {
    None, L3, R3, L1, R1, L2, R2, Cross, Circle, Square, Triangle,
    Share, Options, DpadUp, DpadDown, DpadLeft, DpadRight, Count
};
const char* ButtonMappingName(ButtonMapping m);

// ---- Raw helpers -----------------------------------------------------------

constexpr size_t MIN_REPORT_SIZE = 0x3C;

void     ExtractRawStick(const std::vector<uint8_t>& buf, bool leftStick, int& x, int& y);
uint32_t JoyConButtonState(const std::vector<uint8_t>& buf, bool isLeft);
std::pair<int16_t, int16_t> OpticalSensor(const std::vector<uint8_t>& buf);
// Calibrated stick in -1..1 (+y = up), deadzone applied, no rotation.
void     StickValue(const std::vector<uint8_t>& buf, bool leftStick, const StickCalibration& cal,
                    const StickTuning& tune, float& x, float& y);

// Battery voltage carried in every input report (uint16 LE at 0x1F, in mV;
// documented by both joycon2android and switch2_controller_research).
// Returns 0 when the report has no plausible value.
int BatteryMillivolts(const std::vector<uint8_t>& buf);
// Estimated percentage. The reported voltage reads ~0.6 V below the cell, so
// Nintendo's Joy-Con thresholds are shifted down (community measurement:
// ~3.30 V shows 75% and ~3.60 V shows 100% on a Switch 2). Approximate.
int BatteryPercent(int millivolts);

// Right Joy-Con button bits (in JoyConButtonState(buf, false))
constexpr uint32_t JC_R_R     = 0x004000;
constexpr uint32_t JC_R_ZR    = 0x008000;
constexpr uint32_t JC_R_STICK = 0x000004;
constexpr uint32_t JC_R_CHAT  = 0x000040;

// ---- Report builders -------------------------------------------------------

Ds4Report NeutralReport();

Ds4Report BuildSingleJoyConReport(const std::vector<uint8_t>& buf, bool isLeft,
                                      const StickCalibration& cal, const StickTuning& tune,
                                      const JoyConMapping& map);

// Either buffer may be empty (that half is missing / disconnected): it then
// contributes a neutral state instead of garbage.
Ds4Report BuildDualJoyConReport(const std::vector<uint8_t>& left, const std::vector<uint8_t>& right,
                                    const StickCalibration& calL, const StickCalibration& calR,
                                    const StickTuning& tune, bool leftDpadAsDpad, GyroSource gyro);

Ds4Report BuildProReport(const std::vector<uint8_t>& buf, const StickCalibration& calL,
                             const StickCalibration& calR, const StickTuning& tune,
                             ButtonMapping gl, ButtonMapping gr);

Ds4Report BuildGameCubeReport(const std::vector<uint8_t>& buf, const StickCalibration& calL,
                                  const StickCalibration& calR, const StickTuning& tune);

void ApplySwapABXY(Ds4Report& r);

// ---- Host -> controller commands -------------------------------------------

using Packet = std::vector<uint8_t>;

// Only commands the official console / the original app send during normal
// operation are allowed. Pairing (0x15), clearing pairing (0x03/0x08),
// firmware (0x0D), shutdown/reboot (0x06) and flash writes are rejected.
bool IsCommandAllowed(const Packet& cmd);

std::vector<Packet> InitSequence(ControllerKind k);
int  InitPrefixSize(ControllerKind k);          // zero padding before each init command
Packet PlayerLedCommand(uint8_t pattern);
Packet VibrationSampleCommand(uint8_t sampleId); // 0x00 stops playback

// Rumble streaming packets (written to the vibration characteristic)
Packet JoyConRumblePacket(float amp, uint8_t counter);
Packet ProRumblePacket(float amp, uint8_t counter);
Packet GameCubeRumblePacket(float amp);

} // namespace sw2
