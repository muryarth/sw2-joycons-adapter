#pragma once
// Fake controllers for `--demo` (UI review without hardware).
// Kept out of DeviceManager so production logic stays free of test data.

#include "app/DeviceManager.h"

#include <vector>

namespace sw2::demo {

bool IsDemoAddress(uint64_t address);
void AppendViews(std::vector<DeviceView>& out);
void FillLiveInput(LiveInput& out);   // animated sticks and buttons

} // namespace sw2::demo
