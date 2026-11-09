#include "app/Settings.h"

#include <nlohmann/json.hpp>

#include <ShlObj.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

using nlohmann::json;
namespace fs = std::filesystem;

namespace sw2 {

DeviceProfile* AppSettings::Find(uint64_t address) {
    for (auto& d : known) if (d.address == address) return &d;
    return nullptr;
}
const DeviceProfile* AppSettings::Find(uint64_t address) const {
    for (auto& d : known) if (d.address == address) return &d;
    return nullptr;
}
void AppSettings::Forget(uint64_t address) {
    known.erase(std::remove_if(known.begin(), known.end(),
        [&](const DeviceProfile& d) { return d.address == address; }), known.end());
}

std::string FormatAddress(uint64_t a) {
    char s[24];
    std::snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X",
        unsigned((a >> 40) & 0xFF), unsigned((a >> 32) & 0xFF), unsigned((a >> 24) & 0xFF),
        unsigned((a >> 16) & 0xFF), unsigned((a >> 8) & 0xFF), unsigned(a & 0xFF));
    return s;
}

std::wstring SettingsDirectory() {
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        dir = std::wstring(appData) + L"\\sw2-joycons-adapter";
        CoTaskMemFree(appData);
    } else {
        dir = L".";
    }
    return dir;
}

static fs::path SettingsFile() { return fs::path(SettingsDirectory()) / L"settings.json"; }

// ---- JSON mapping ----------------------------------------------------------

static json CalToJson(const StickCalibration& c) {
    return {{"calibrated", c.calibrated}, {"centerX", c.centerX}, {"centerY", c.centerY},
            {"minX", c.minX}, {"maxX", c.maxX}, {"minY", c.minY}, {"maxY", c.maxY}};
}
static StickCalibration CalFromJson(const json& j) {
    StickCalibration c;
    c.calibrated = j.value("calibrated", false);
    c.centerX = j.value("centerX", 2048); c.centerY = j.value("centerY", 2048);
    c.minX = j.value("minX", 0); c.maxX = j.value("maxX", 4095);
    c.minY = j.value("minY", 0); c.maxY = j.value("maxY", 4095);
    return c;
}

template <typename E> static E EnumFrom(const json& j, const char* key, E def, int maxValue) {
    const int v = j.value(key, static_cast<int>(def));
    return (v >= 0 && v <= maxValue) ? static_cast<E>(v) : def;
}

AppSettings LoadSettings() {
    AppSettings s;
    std::ifstream f(SettingsFile());
    if (!f.is_open()) return s;
    try {
        json j = json::parse(f, nullptr, true, true);
        const int version = j.value("version", 1);
        s.language       = std::clamp(j.value("language", s.language), 0, 1);
        s.autoSearch     = j.value("autoSearch", s.autoSearch);
        s.combineJoyCons = j.value("combineJoyCons", s.combineJoyCons);
        s.swapABXY       = j.value("swapABXY", s.swapABXY);
        s.deadzone       = std::clamp(j.value("deadzone", s.deadzone), 0.0f, 0.5f);
        s.gyroSource     = EnumFrom(j, "gyroSource", s.gyroSource, 2);
        s.sounds         = j.value("sounds", s.sounds);
        s.volume         = std::clamp(j.value("volume", s.volume), 0.0f, 1.0f);
        s.updatePolicy   = EnumFrom(j, "updatePolicy", s.updatePolicy, 2);
        s.heartbeatMs    = std::clamp(j.value("heartbeatMs", s.heartbeatMs), 0, 50);
        s.watchdogMs     = std::clamp(j.value("watchdogMs", s.watchdogMs), 0, 2000);
        s.dsuServer      = j.value("dsuServer", s.dsuServer);

        for (const auto& d : j.value("knownControllers", json::array())) {
            DeviceProfile p;
            p.address = std::stoull(d.value("address", std::string("0")), nullptr, 16);
            p.kind    = EnumFrom(d, "kind", ControllerKind::Unknown, 4);
            p.name    = d.value("name", std::string(KindName(p.kind)));
            if (!p.address || p.kind == ControllerKind::Unknown) continue;
            const json in = d.value("input", json::object());
            p.input.calLeft  = CalFromJson(in.value("calLeft", json::object()));
            p.input.calRight = CalFromJson(in.value("calRight", json::object()));
            p.input.mapping.orientation   = EnumFrom(in, "orientation", p.input.mapping.orientation, 1);
            p.input.mapping.dpadAsDpad    = in.value("dpadAsDpad", p.input.mapping.dpadAsDpad);
            p.input.mapping.remapSideways = in.value("remapSideways", p.input.mapping.remapSideways);
            p.input.mouseEnabled = in.value("mouseEnabled", p.input.mouseEnabled);
            p.input.gl = EnumFrom(in, "gl", p.input.gl, int(ButtonMapping::Count) - 1);
            p.input.gr = EnumFrom(in, "gr", p.input.gr, int(ButtonMapping::Count) - 1);
            // v1 -> v2: GL/GR "None" (the old default) becomes the stick clicks.
            if (version < 2 && p.input.gl == ButtonMapping::None) p.input.gl = ButtonMapping::L3;
            if (version < 2 && p.input.gr == ButtonMapping::None) p.input.gr = ButtonMapping::R3;
            s.known.push_back(std::move(p));
        }
    } catch (...) {
        // Corrupt file: keep defaults rather than crash.
    }
    return s;
}

bool SaveSettings(const AppSettings& s) {
    json known = json::array();
    for (const auto& p : s.known) {
        char addr[24];
        std::snprintf(addr, sizeof(addr), "%llX", static_cast<unsigned long long>(p.address));
        known.push_back({
            {"address", addr}, {"kind", static_cast<int>(p.kind)}, {"name", p.name},
            {"input", {
                {"calLeft", CalToJson(p.input.calLeft)}, {"calRight", CalToJson(p.input.calRight)},
                {"orientation", static_cast<int>(p.input.mapping.orientation)},
                {"dpadAsDpad", p.input.mapping.dpadAsDpad},
                {"remapSideways", p.input.mapping.remapSideways},
                {"mouseEnabled", p.input.mouseEnabled},
                {"gl", static_cast<int>(p.input.gl)}, {"gr", static_cast<int>(p.input.gr)},
            }},
        });
    }
    json j = {
        {"version", 2},
        {"language", s.language},
        {"autoSearch", s.autoSearch}, {"combineJoyCons", s.combineJoyCons}, {"swapABXY", s.swapABXY},
        {"deadzone", s.deadzone}, {"gyroSource", static_cast<int>(s.gyroSource)},
        {"sounds", s.sounds}, {"volume", s.volume},
        {"updatePolicy", static_cast<int>(s.updatePolicy)},
        {"heartbeatMs", s.heartbeatMs}, {"watchdogMs", s.watchdogMs},
        {"dsuServer", s.dsuServer},
        {"knownControllers", known},
    };
    std::error_code ec;
    fs::create_directories(SettingsDirectory(), ec);
    // Write to a temp file first so a crash mid-write never corrupts settings.
    const fs::path file = SettingsFile();
    const fs::path tmp  = file.wstring() + L".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open()) return false;
        f << j.dump(2);
        if (!f.good()) return false;
    }
    fs::rename(tmp, file, ec);
    if (ec) { fs::remove(file, ec); fs::rename(tmp, file, ec); }
    return !ec;
}

} // namespace sw2
