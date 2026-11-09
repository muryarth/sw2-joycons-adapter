#pragma once
// Converts the core's platform-neutral DS4 report into ViGEm's type.
// Both share the same 63-byte DS4 USB layout, so this is a checked copy.

#include "core/Ds4Report.h"

#include <Windows.h>
#include <ViGEm/Client.h>

#include <cstring>

namespace sw2 {

static_assert(sizeof(DS4_REPORT_EX) == sizeof(Ds4Report), "core report layout must match ViGEm's DS4_REPORT_EX");

inline DS4_REPORT_EX ToVigem(const Ds4Report& r) {
    DS4_REPORT_EX out;
    std::memcpy(&out, &r, sizeof(out));
    return out;
}

} // namespace sw2
