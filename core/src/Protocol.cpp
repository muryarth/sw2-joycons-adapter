#include "core/Protocol.h"
#include "core/I18n.h"


#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace sw2 {

// ===========================================================================
// Advertisement
// ===========================================================================

ControllerKind KindFromProductId(uint16_t pid) {
    switch (pid) {
        case PID_JOYCON_L: return ControllerKind::JoyConL;
        case PID_JOYCON_R: return ControllerKind::JoyConR;
        case PID_PRO:      return ControllerKind::ProController;
        case PID_GAMECUBE: return ControllerKind::GameCube;
        default:           return ControllerKind::Unknown;
    }
}

const char* KindName(ControllerKind k) {
    switch (k) {
        case ControllerKind::JoyConL:       return "Joy-Con 2 (L)";
        case ControllerKind::JoyConR:       return "Joy-Con 2 (R)";
        case ControllerKind::ProController: return "Pro Controller 2";
        case ControllerKind::GameCube:      return "GameCube (NSO)";
        default:                            return "Switch 2 controller";
    }
}

bool ParseAdvertisement(const std::vector<uint8_t>& d, AdvertisementInfo& out) {
    // 01 00 03 7E 05 <PID lo> <PID hi> 00 01 00 <host addr x6> ...
    if (d.size() < 7) return false;
    if (d[0] != 0x01 || d[1] != 0x00 || d[2] != 0x03 || d[3] != 0x7E || d[4] != 0x05) return false;
    out.productId = static_cast<uint16_t>(d[5] | (d[6] << 8));
    if (out.productId < 0x2060) return false;
    out.kind = KindFromProductId(out.productId);

    // Pairing/sync advertisements carry an all-zero host address; "wake"
    // advertisements carry the console's address. If the payload is too short
    // to tell, fall back to the original app's behaviour (treat as sync).
    out.syncMode = true;
    if (d.size() >= 16) {
        bool allZero = true;
        for (size_t i = 10; i < 16; ++i) if (d[i] != 0) { allZero = false; break; }
        out.syncMode = allZero;
    }
    return true;
}

const char* ButtonMappingName(ButtonMapping m) {
    // Named after the Switch button that produces the same virtual output.
    switch (m) {
        case ButtonMapping::None:      return tr("None", "Nenhum");
        case ButtonMapping::L3:        return tr("Left stick press", "Clique do analógico esquerdo");
        case ButtonMapping::R3:        return tr("Right stick press", "Clique do analógico direito");
        case ButtonMapping::L1:        return "L";
        case ButtonMapping::R1:        return "R";
        case ButtonMapping::L2:        return "ZL";
        case ButtonMapping::R2:        return "ZR";
        case ButtonMapping::Cross:     return "B";
        case ButtonMapping::Circle:    return "A";
        case ButtonMapping::Square:    return "Y";
        case ButtonMapping::Triangle:  return "X";
        case ButtonMapping::Share:     return tr("Minus (−)", "Menos (−)");
        case ButtonMapping::Options:   return tr("Plus (+)", "Mais (+)");
        case ButtonMapping::DpadUp:    return tr("D-pad up", "Direcional cima");
        case ButtonMapping::DpadDown:  return tr("D-pad down", "Direcional baixo");
        case ButtonMapping::DpadLeft:  return tr("D-pad left", "Direcional esquerda");
        case ButtonMapping::DpadRight: return tr("D-pad right", "Direcional direita");
        default:                       return "?";
    }
}

// ===========================================================================
// Raw decoding
// ===========================================================================

namespace {

int16_t S16(const std::vector<uint8_t>& b, size_t o) {
    return static_cast<int16_t>((b[o + 1] << 8) | b[o]);
}

struct Motion { int16_t gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0; };

Motion DecodeMotion(const std::vector<uint8_t>& b) {
    Motion m;
    if (b.size() < MIN_REPORT_SIZE || b[0x29] != 0x01) return m;
    bool allZero = true;
    for (size_t i = 0x30; i <= 0x3B; ++i) if (b[i]) { allZero = false; break; }
    if (allZero) return m;
    m.ax = S16(b, 0x30); m.ay = S16(b, 0x32); m.az = S16(b, 0x34);
    m.gx = S16(b, 0x36); m.gy = S16(b, 0x38); m.gz = S16(b, 0x3A);
    return m;
}

void ApplyMotion(Ds4Report& r, const Motion& m) {
    r.Report.wAccelX = m.ax; r.Report.wAccelY = m.ay; r.Report.wAccelZ = m.az;
    r.Report.wGyroX  = m.gx; r.Report.wGyroY  = m.gy; r.Report.wGyroZ  = m.gz;
}

float CalibratedAxis(int raw, int center, int minV, int maxV) {
    if (minV >= maxV) { minV = 300; maxV = 3800; center = std::clamp(center, minV, maxV); }
    float v;
    if (raw >= center) { int range = maxV - center; v = range > 0 ? float(raw - center) / float(range) : 0.f; }
    else               { int range = center - minV; v = range > 0 ? -float(center - raw) / float(range) : 0.f; }
    return std::clamp(v, -1.f, 1.f);
}

// Returns stick in -1..1 with +y = up.
void DecodeStick(const std::vector<uint8_t>& b, bool leftStick, const StickCalibration& cal, float& x, float& y) {
    int rx, ry;
    ExtractRawStick(b, leftStick, rx, ry);
    x = CalibratedAxis(rx, cal.centerX, cal.minX, cal.maxX);
    y = CalibratedAxis(ry, cal.centerY, cal.minY, cal.maxY);
}

void FinishStick(float& x, float& y, const StickCalibration& cal, const StickTuning& tune) {
    // Radial processing (like commercial pads): deadzone and gain act on the
    // distance from the centre and the result is limited to a CIRCLE. Clamping
    // each axis separately produced a square gate (full X and Y on diagonals).
    // Uncalibrated sticks never reach the raw extremes, so they get the 1.7x
    // boost the original app used; a calibrated stick already spans -1..1.
    const float gain = cal.calibrated ? 1.0f : 1.7f;
    const float mag = std::sqrt(x * x + y * y);
    const float dz = std::clamp(tune.deadzone, 0.f, 0.9f);
    if (mag <= dz || mag < 1e-6f) { x = y = 0.f; return; }
    const float scaled = std::min(1.f, (mag - dz) / (1.f - dz) * gain);
    x = x / mag * scaled;
    y = y / mag * scaled;
}

uint8_t AxisToByte(float v) { return static_cast<uint8_t>(std::clamp(v * 127.f + 128.f, 0.f, 255.f)); }

void SetDpad(Ds4Report& r, bool up, bool down, bool left, bool right) {
    ds4::Dpad d = ds4::DpadNone;
    if      (up && left)    d = ds4::DpadNorthWest;
    else if (up && right)   d = ds4::DpadNorthEast;
    else if (down && left)  d = ds4::DpadSouthWest;
    else if (down && right) d = ds4::DpadSouthEast;
    else if (up)            d = ds4::DpadNorth;
    else if (down)          d = ds4::DpadSouth;
    else if (left)          d = ds4::DpadWest;
    else if (right)         d = ds4::DpadEast;
    ds4::SetDpad(r, d);
}

// --- Joy-Con 2 bit layout (JoyConButtonState) ---
// Left  (bytes 4..6): down 0x01, up 0x02, right 0x04, left 0x08, SR 0x10, SL 0x20,
//                     L 0x40, ZL 0x80, minus 0x100, stick 0x800, capture 0x2000
// Right (bytes 3..5): plus 0x02, stick 0x04, home 0x10, chat 0x40, Y 0x100,
//                     0x200, 0x400, A 0x800, SR 0x1000, SL 0x2000, R 0x4000, ZR 0x8000
// Note: joycon2cpp names 0x200 "B" and 0x400 "X", but its (hardware-tested)
// mappings are only geometrically consistent if 0x200 is X and 0x400 is B.
// The mappings below are kept exactly as in joycon2cpp.
constexpr uint32_t L_DOWN = 0x000001, L_UP = 0x000002, L_RIGHT = 0x000004, L_LEFT = 0x000008;
constexpr uint32_t L_SR = 0x000010, L_SL = 0x000020, L_L = 0x000040, L_ZL = 0x000080;
constexpr uint32_t L_MINUS = 0x000100, L_STICK = 0x000800, L_CAPTURE = 0x002000;
constexpr uint32_t R_PLUS = 0x000002, R_STICK = 0x000004, R_HOME = 0x000010;
constexpr uint32_t R_Y = 0x000100, R_BIT9 = 0x000200, R_BIT10 = 0x000400, R_A = 0x000800;
constexpr uint32_t R_SR = 0x001000, R_SL = 0x002000, R_R = 0x004000, R_ZR = 0x008000;

struct Half {
    uint16_t buttons = 0;     // DS4 buttons excluding the D-pad nibble
    bool up = false, down = false, left = false, right = false;
    uint8_t special = 0;
    bool l1 = false, r1 = false, l2 = false, r2 = false;
    bool stickPress = false;
    float sx = 0.f, sy = 0.f;
    Motion motion;
    bool valid = false;
};

Half DecodeHalf(const std::vector<uint8_t>& b, bool isLeft, const StickCalibration& cal,
                const StickTuning& tune, Orientation o, bool dpadAsDpad, bool remapSideways) {
    Half h;
    if (b.size() < MIN_REPORT_SIZE) return h;
    h.valid = true;
    const uint32_t s = JoyConButtonState(b, isLeft);
    const bool upright = o == Orientation::Upright;
    const bool remap = !upright && remapSideways;

    if (isLeft) {
        const bool pu = s & L_UP, pd = s & L_DOWN, pl = s & L_LEFT, pr = s & L_RIGHT;
        const bool up = remap ? pr : pu, down = remap ? pl : pd;
        const bool left = remap ? pu : pl, right = remap ? pd : pr;
        if (dpadAsDpad) { h.up = up; h.down = down; h.left = left; h.right = right; }
        else {
            if (down)  h.buttons |= ds4::Cross;
            if (right) h.buttons |= ds4::Circle;
            if (left)  h.buttons |= ds4::Square;
            if (up)    h.buttons |= ds4::Triangle;
        }
        if (s & L_MINUS)   h.buttons |= ds4::Share;
        if (s & L_CAPTURE) h.special |= ds4::SpecialTouchpad;
        h.stickPress = s & L_STICK;
        if (upright) { h.l1 = s & L_L; h.l2 = s & L_ZL; }
        else         { h.l1 = s & L_SL; h.r1 = s & L_SR; h.l2 = s & (L_L | L_ZL); }
    } else {
        if (remap) {
            if (s & R_A)     h.buttons |= ds4::Cross;
            if (s & R_BIT9)  h.buttons |= ds4::Circle;
            if (s & R_BIT10) h.buttons |= ds4::Square;
            if (s & R_Y)     h.buttons |= ds4::Triangle;
        } else {
            if (s & R_A)     h.buttons |= ds4::Circle;
            if (s & R_BIT9)  h.buttons |= ds4::Triangle;
            if (s & R_BIT10) h.buttons |= ds4::Cross;
            if (s & R_Y)     h.buttons |= ds4::Square;
        }
        if (s & R_PLUS) h.buttons |= ds4::Options;
        // Home -> PS (guide), like the Pro Controller. Capture (left) stays on
        // the touchpad click, so each Switch button has one clear meaning.
        if (s & R_HOME) h.special |= ds4::SpecialPS;
        h.stickPress = s & R_STICK;
        if (upright) { h.r1 = s & R_R; h.r2 = s & R_ZR; }
        else         { h.l1 = s & R_SL; h.r1 = s & R_SR; h.r2 = s & (R_R | R_ZR); }
    }

    float x, y;
    DecodeStick(b, isLeft, cal, x, y);
    if (!upright) {
        const float tx = x, ty = y;
        x = isLeft ? -ty : ty;
        y = isLeft ? tx : -tx;
    }
    FinishStick(x, y, cal, tune);
    h.sx = x; h.sy = y;
    h.motion = DecodeMotion(b);
    return h;
}

void ApplyShoulders(Ds4Report& r, bool l1, bool r1, bool l2, bool r2) {
    if (l1) r.Report.wButtons |= ds4::ShoulderLeft;
    if (r1) r.Report.wButtons |= ds4::ShoulderRight;
    if (l2) { r.Report.wButtons |= ds4::TriggerLeft;  r.Report.bTriggerL = 255; }
    if (r2) { r.Report.wButtons |= ds4::TriggerRight; r.Report.bTriggerR = 255; }
}

void ApplyMapping(Ds4Report& r, ButtonMapping m) {
    switch (m) {
        case ButtonMapping::L3:        r.Report.wButtons |= ds4::ThumbLeft; break;
        case ButtonMapping::R3:        r.Report.wButtons |= ds4::ThumbRight; break;
        case ButtonMapping::L1:        r.Report.wButtons |= ds4::ShoulderLeft; break;
        case ButtonMapping::R1:        r.Report.wButtons |= ds4::ShoulderRight; break;
        case ButtonMapping::L2:        r.Report.wButtons |= ds4::TriggerLeft;  r.Report.bTriggerL = 255; break;
        case ButtonMapping::R2:        r.Report.wButtons |= ds4::TriggerRight; r.Report.bTriggerR = 255; break;
        case ButtonMapping::Cross:     r.Report.wButtons |= ds4::Cross; break;
        case ButtonMapping::Circle:    r.Report.wButtons |= ds4::Circle; break;
        case ButtonMapping::Square:    r.Report.wButtons |= ds4::Square; break;
        case ButtonMapping::Triangle:  r.Report.wButtons |= ds4::Triangle; break;
        case ButtonMapping::Share:     r.Report.wButtons |= ds4::Share; break;
        case ButtonMapping::Options:   r.Report.wButtons |= ds4::Options; break;
        case ButtonMapping::DpadUp:    ds4::SetDpad(r, ds4::DpadNorth); break;
        case ButtonMapping::DpadDown:  ds4::SetDpad(r, ds4::DpadSouth); break;
        case ButtonMapping::DpadLeft:  ds4::SetDpad(r, ds4::DpadWest); break;
        case ButtonMapping::DpadRight: ds4::SetDpad(r, ds4::DpadEast); break;
        default: break;
    }
}

uint64_t ProState(const std::vector<uint8_t>& b) {
    uint64_t st = 0;
    for (int i = 3; i <= 8; ++i) st = (st << 8) | b[i];
    return st;
}

// Pro / GameCube bits (ProState) — kept exactly as in joycon2cpp.
// Face buttons, low byte of the right-hand group: Y=0x01, X=0x02, B=0x04, A=0x08
// (switch2_controller_research).
constexpr uint64_t P_A = 0x000800000000, P_B = 0x000400000000, P_X = 0x000200000000, P_Y = 0x000100000000;
constexpr uint64_t P_R = 0x004000000000, P_L = 0x000000400000;
constexpr uint64_t P_UP = 0x000000020000, P_RIGHT = 0x000000040000, P_DOWN = 0x000000010000, P_LEFT = 0x000000080000;
constexpr uint64_t P_HOME = 0x000010000000, P_MINUS = 0x000001000000, P_PLUS = 0x000002000000;
constexpr uint64_t P_RSTICK = 0x000004000000, P_LSTICK = 0x000008000000;
constexpr uint64_t P_ZL = 0x000000800000, P_ZR = 0x008000000000;
constexpr uint64_t P_GL = 0x000000000200, P_GR = 0x000000000100;

void ProSticks(Ds4Report& r, const std::vector<uint8_t>& b, const StickCalibration& calL,
               const StickCalibration& calR, const StickTuning& tune) {
    float lx, ly, rx, ry;
    DecodeStick(b, true, calL, lx, ly);  FinishStick(lx, ly, calL, tune);
    DecodeStick(b, false, calR, rx, ry); FinishStick(rx, ry, calR, tune);
    r.Report.bThumbLX = AxisToByte(lx); r.Report.bThumbLY = AxisToByte(-ly);
    r.Report.bThumbRX = AxisToByte(rx); r.Report.bThumbRY = AxisToByte(-ry);
}

} // namespace

void ExtractRawStick(const std::vector<uint8_t>& b, bool leftStick, int& x, int& y) {
    if (b.size() < 16) { x = y = 2048; return; }
    const uint8_t* d = leftStick ? &b[10] : &b[13];
    x = ((d[1] & 0x0F) << 8) | d[0];
    y = (d[2] << 4) | ((d[1] & 0xF0) >> 4);
}

uint32_t JoyConButtonState(const std::vector<uint8_t>& b, bool isLeft) {
    const size_t o = isLeft ? 4 : 3;
    if (b.size() < o + 3) return 0;
    return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2];
}

void StickValue(const std::vector<uint8_t>& b, bool leftStick, const StickCalibration& cal,
                const StickTuning& tune, float& x, float& y) {
    DecodeStick(b, leftStick, cal, x, y);
    FinishStick(x, y, cal, tune);
}

int BatteryMillivolts(const std::vector<uint8_t>& b) {
    if (b.size() < 0x21) return 0;
    const int mv = b[0x1F] | (b[0x20] << 8);
    return (mv >= 2000 && mv <= 4800) ? mv : 0;   // ignore garbage / absent field
}

int BatteryPercent(int mv) {
    if (mv <= 0) return -1;
    struct Pt { int mv; int pct; };
    static const Pt curve[] = {{2700, 0}, {3000, 25}, {3160, 50}, {3300, 75}, {3600, 100}};
    if (mv <= curve[0].mv) return 0;
    for (size_t i = 1; i < std::size(curve); ++i) {
        if (mv <= curve[i].mv) {
            const float t = float(mv - curve[i - 1].mv) / float(curve[i].mv - curve[i - 1].mv);
            return int(std::lround(curve[i - 1].pct + t * (curve[i].pct - curve[i - 1].pct)));
        }
    }
    return 100;
}

std::pair<int16_t, int16_t> OpticalSensor(const std::vector<uint8_t>& b) {
    if (b.size() < 0x18) return {0, 0};
    return {S16(b, 0x10), S16(b, 0x12)};
}

// ===========================================================================
// Reports
// ===========================================================================

Ds4Report NeutralReport() { return ds4::Neutral(); }

Ds4Report BuildSingleJoyConReport(const std::vector<uint8_t>& b, bool isLeft,
                                      const StickCalibration& cal, const StickTuning& tune,
                                      const JoyConMapping& map) {
    Ds4Report r = NeutralReport();
    const Half h = DecodeHalf(b, isLeft, cal, tune, map.orientation, isLeft ? map.dpadAsDpad : true, map.remapSideways);
    if (!h.valid) return r;
    r.Report.wButtons |= h.buttons;
    SetDpad(r, h.up, h.down, h.left, h.right);
    r.Report.bSpecial |= h.special;
    if (h.stickPress) r.Report.wButtons |= isLeft ? ds4::ThumbLeft : ds4::ThumbRight;
    ApplyShoulders(r, h.l1, h.r1, h.l2, h.r2);
    r.Report.bThumbLX = AxisToByte(h.sx);
    r.Report.bThumbLY = AxisToByte(-h.sy);
    ApplyMotion(r, h.motion);
    return r;
}

Ds4Report BuildDualJoyConReport(const std::vector<uint8_t>& left, const std::vector<uint8_t>& right,
                                    const StickCalibration& calL, const StickCalibration& calR,
                                    const StickTuning& tune, bool leftDpadAsDpad, GyroSource gyro) {
    Ds4Report r = NeutralReport();
    const Half L = DecodeHalf(left,  true,  calL, tune, Orientation::Upright, leftDpadAsDpad, false);
    const Half R = DecodeHalf(right, false, calR, tune, Orientation::Upright, true, false);

    r.Report.wButtons |= L.buttons | R.buttons;
    SetDpad(r, L.up, L.down, L.left, L.right);
    r.Report.bSpecial |= L.special | R.special;
    if (L.stickPress) r.Report.wButtons |= ds4::ThumbLeft;
    if (R.stickPress) r.Report.wButtons |= ds4::ThumbRight;
    ApplyShoulders(r, L.l1, R.r1, L.l2, R.r2);
    r.Report.bThumbLX = AxisToByte(L.sx); r.Report.bThumbLY = AxisToByte(-L.sy);
    r.Report.bThumbRX = AxisToByte(R.sx); r.Report.bThumbRY = AxisToByte(-R.sy);

    Motion m;
    auto avg = [](int16_t a, int16_t b) -> int16_t { if (!a) return b; if (!b) return a; return int16_t(a / 2 + b / 2); };
    switch (gyro) {
        case GyroSource::Left:  m = L.motion; break;
        case GyroSource::Right: m = R.motion; break;
        default:
            m.ax = avg(L.motion.ax, R.motion.ax); m.ay = avg(L.motion.ay, R.motion.ay); m.az = avg(L.motion.az, R.motion.az);
            m.gx = avg(L.motion.gx, R.motion.gx); m.gy = avg(L.motion.gy, R.motion.gy); m.gz = avg(L.motion.gz, R.motion.gz);
            break;
    }
    ApplyMotion(r, m);
    return r;
}

Ds4Report BuildProReport(const std::vector<uint8_t>& b, const StickCalibration& calL,
                             const StickCalibration& calR, const StickTuning& tune,
                             ButtonMapping gl, ButtonMapping gr) {
    Ds4Report r = NeutralReport();
    if (b.size() < MIN_REPORT_SIZE) return r;
    const uint64_t s = ProState(b);
    if (s & P_A)      r.Report.wButtons |= ds4::Circle;
    if (s & P_B)      r.Report.wButtons |= ds4::Cross;
    if (s & P_X)      r.Report.wButtons |= ds4::Triangle;
    if (s & P_Y)      r.Report.wButtons |= ds4::Square;
    if (s & P_L)      r.Report.wButtons |= ds4::ShoulderLeft;
    if (s & P_R)      r.Report.wButtons |= ds4::ShoulderRight;
    if (s & P_LSTICK) r.Report.wButtons |= ds4::ThumbLeft;
    if (s & P_RSTICK) r.Report.wButtons |= ds4::ThumbRight;
    if (s & P_MINUS)  r.Report.wButtons |= ds4::Share;   // upstream issue #37: was touchpad
    if (s & P_PLUS)   r.Report.wButtons |= ds4::Options;
    if (s & P_HOME)   r.Report.bSpecial |= ds4::SpecialPS;
    SetDpad(r, s & P_UP, s & P_DOWN, s & P_LEFT, s & P_RIGHT);
    ApplyShoulders(r, false, false, s & P_ZL, s & P_ZR);
    ProSticks(r, b, calL, calR, tune);
    if (s & P_GL) ApplyMapping(r, gl);
    if (s & P_GR) ApplyMapping(r, gr);
    ApplyMotion(r, DecodeMotion(b));
    return r;
}

Ds4Report BuildGameCubeReport(const std::vector<uint8_t>& b, const StickCalibration& calL,
                                  const StickCalibration& calR, const StickTuning& tune) {
    Ds4Report r = NeutralReport();
    if (b.size() < MIN_REPORT_SIZE) return r;
    const uint64_t s = ProState(b);
    if (s & P_A)      r.Report.wButtons |= ds4::Circle;
    if (s & P_B)      r.Report.wButtons |= ds4::Cross;
    if (s & P_X)      r.Report.wButtons |= ds4::Triangle;
    if (s & P_Y)      r.Report.wButtons |= ds4::Square;
    if (s & P_L)      r.Report.wButtons |= ds4::ShoulderLeft;
    if (s & P_R)      r.Report.wButtons |= ds4::ShoulderRight;
    if (s & P_ZL)     r.Report.wButtons |= ds4::TriggerLeft;
    if (s & P_ZR)     r.Report.wButtons |= ds4::TriggerRight;
    if (s & P_LSTICK) r.Report.wButtons |= ds4::ThumbLeft;
    if (s & P_RSTICK) r.Report.wButtons |= ds4::ThumbRight;
    if (s & P_MINUS)  r.Report.wButtons |= ds4::Share;
    if (s & P_PLUS)   r.Report.wButtons |= ds4::Options;
    if (s & P_HOME)   r.Report.bSpecial |= ds4::SpecialPS;
    SetDpad(r, s & P_UP, s & P_DOWN, s & P_LEFT, s & P_RIGHT);
    if (b.size() > 0x3D) { r.Report.bTriggerL = b[0x3C]; r.Report.bTriggerR = b[0x3D]; }
    ProSticks(r, b, calL, calR, tune);
    ApplyMotion(r, DecodeMotion(b));
    return r;
}

void ApplySwapABXY(Ds4Report& r) {
    uint16_t& w = r.Report.wButtons;
    const bool a = w & ds4::Circle, b = w & ds4::Cross;
    const bool x = w & ds4::Triangle, y = w & ds4::Square;
    w &= ~(ds4::Circle | ds4::Cross | ds4::Triangle | ds4::Square);
    if (b) w |= ds4::Circle;
    if (a) w |= ds4::Cross;
    if (y) w |= ds4::Triangle;
    if (x) w |= ds4::Square;
}

// ===========================================================================
// Commands
// ===========================================================================

bool IsCommandAllowed(const Packet& c) {
    if (c.size() < 8) return false;
    const uint8_t id = c[0], sub = c[3];
    switch (id) {
        case 0x02: return sub == 0x04;  // flash READ only (no write/erase)
        case 0x07:                      // init / unknown, sent by the console on connect
        case 0x09:                      // player LEDs
        case 0x0A:                      // vibration samples / config
        case 0x0C:                      // feature select (input streaming)
        case 0x10:                      // firmware info (read)
        case 0x11:                      // sent by the console during init
        case 0x16:                      // sent by the console during init
            return true;
        default:
            // 0x03 (pairing info / BT wake / cancel), 0x06 (shutdown / reboot),
            // 0x0D (firmware update), 0x15 (pairing) and anything unknown.
            return false;
    }
}

std::vector<Packet> InitSequence(ControllerKind k) {
    // Ported verbatim from joycon2cpp (SendJoyCon2OfficialInit & friends).
    if (k == ControllerKind::ProController) {
        return {
            {0x07,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x00,0x30,0x01,0x00},
            {0x16,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x0A,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x03,0x00,0x00,0x00},
            {0x09,0x91,0x01,0x07,0x00,0x08,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
            {0x0C,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x2F,0x00,0x00,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x80,0x30,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0xC0,0x30,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x40,0xC0,0x1F,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x10,0x7E,0x00,0x00,0x40,0x30,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x18,0x7E,0x00,0x00,0x00,0x31,0x01,0x00},
            {0x11,0x91,0x01,0x03,0x00,0x00,0x00,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x20,0x7E,0x00,0x00,0x60,0x30,0x01,0x00},
            {0x0A,0x91,0x01,0x08,0x00,0x14,0x00,0x00,
             0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x35,0x00,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
            {0x0C,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x2F,0x00,0x00,0x00},
        };
    }
    if (k == ControllerKind::GameCube) {
        return {
            {0x07,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x00,0x30,0x01,0x00},
            {0x10,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x16,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x0A,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x03,0x00,0x00,0x00},
            {0x09,0x91,0x01,0x07,0x00,0x08,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
            {0x0C,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x27,0x00,0x00,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x80,0x30,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0xC0,0x30,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x40,0xC0,0x1F,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x10,0x7E,0x00,0x00,0x40,0x30,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x18,0x7E,0x00,0x00,0x00,0x31,0x01,0x00},
            {0x11,0x91,0x01,0x03,0x00,0x00,0x00,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x02,0x7E,0x00,0x00,0x40,0x31,0x01,0x00},
            {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x20,0x7E,0x00,0x00,0x60,0x30,0x01,0x00},
            {0x0A,0x91,0x01,0x08,0x00,0x14,0x00,0x00,
             0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x35,0x00,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
            {0x0C,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x27,0x00,0x00,0x00},
        };
    }
    // Joy-Con 2 (L or R)
    return {
        {0x07,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x00,0x30,0x01,0x00},
        {0x10,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x16,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x0A,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x03,0x00,0x00,0x00},
        {0x09,0x91,0x01,0x07,0x00,0x08,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x37,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x80,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x40,0xC0,0x1F,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x10,0x7E,0x00,0x00,0x40,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x18,0x7E,0x00,0x00,0x00,0x31,0x01,0x00},
        {0x11,0x91,0x01,0x03,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x20,0x7E,0x00,0x00,0x60,0x30,0x01,0x00},
        {0x0A,0x91,0x01,0x08,0x00,0x14,0x00,0x00,
         0x01,0x59,0x09,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x35,0x00,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x11,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x37,0x00,0x00,0x00},
    };
}

int InitPrefixSize(ControllerKind k) {
    switch (k) {
        case ControllerKind::ProController: return 33;
        case ControllerKind::GameCube:      return 13;
        default:                            return 17;
    }
}

static Packet GenericCommand(uint8_t id, uint8_t sub, const std::vector<uint8_t>& data) {
    Packet p = {id, 0x91, 0x01, sub, 0x00, static_cast<uint8_t>(data.size()), 0x00, 0x00};
    p.insert(p.end(), data.begin(), data.end());
    return p;
}

Packet PlayerLedCommand(uint8_t pattern) {
    std::vector<uint8_t> d(8, 0); d[0] = pattern;
    return GenericCommand(0x09, 0x07, d);
}

Packet VibrationSampleCommand(uint8_t sampleId) {
    std::vector<uint8_t> d(4, 0); d[0] = sampleId;
    return GenericCommand(0x0A, 0x02, d);
}

static const uint8_t RUMBLE_STRONG[5] = {0x93, 0x35, 0x36, 0x1C, 0x0D};
static const uint8_t RUMBLE_WEAK[5]   = {0x4B, 0x7D, 0x80, 0x5A, 0x02};

Packet JoyConRumblePacket(float amp, uint8_t counter) {
    Packet p(42, 0x00);
    if (amp >= 0.01f) {
        p[1] = 0x50 | (counter & 0x0F);
        std::memcpy(p.data() + 2, amp > 0.5f ? RUMBLE_STRONG : RUMBLE_WEAK, 5);
    }
    return p;
}

Packet ProRumblePacket(float amp, uint8_t counter) {
    Packet p(58, 0x00);
    if (amp >= 0.01f) {
        const uint8_t* s = amp > 0.5f ? RUMBLE_STRONG : RUMBLE_WEAK;
        p[1] = 0x50 | (counter & 0x0F);  std::memcpy(p.data() + 2, s, 5);
        p[17] = 0x50 | (counter & 0x0F); std::memcpy(p.data() + 18, s, 5);
    }
    return p;
}

Packet GameCubeRumblePacket(float amp) {
    Packet p(5, 0x00);
    if (amp >= 0.01f) { p[1] = 0x01; p[2] = static_cast<uint8_t>(std::min(amp * 255.f, 255.f)); }
    return p;
}

} // namespace sw2
