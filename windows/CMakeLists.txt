# Windows desktop app. Built from the repo root (see ../CMakeLists.txt).
enable_language(RC)

# ---- Dependencies -----------------------------------------------------------
# Downloaded on the first configure (needs internet once), pinned by URL and
# SHA-256 so every build uses exactly the same code. Cached in the build dir.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)   # extracted files get the extraction time
endif()

# Dear ImGui (UI). No CMake project of its own: the sources are compiled below.
FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.tar.gz
    URL_HASH SHA256=fecb33d33930e12ff53a34064e9d3a06c8f7c3e04408f14cd36c80e3faac863b)

# nlohmann/json (settings, update check).
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)

# ViGEmClient (virtual DS4 through the ViGEmBus driver), built from source.
# The project is retired; this is its final commit.
set(ViGEmClient_DLL OFF CACHE INTERNAL "")
FetchContent_Declare(vigem_client
    URL      https://github.com/nefarius/ViGEmClient/archive/b66d02d57e32cc8595369c53418b843e958649b4.tar.gz
    URL_HASH SHA256=c2bc824cc96ed18cf0620b734ae552474f107e7e680485e5c0cb63e675d9f72a)

FetchContent_MakeAvailable(imgui nlohmann_json vigem_client)
set(IMGUI_DIR ${imgui_SOURCE_DIR})

# ---- App ----------------------------------------------------------------------
add_executable(sw2-adapter WIN32
    src/main.cpp
    src/app.rc
    src/app/DemoData.cpp
    src/app/DeviceManager.cpp
    src/app/DeviceManager_Input.cpp
    src/app/DeviceManager_Slots.cpp
    src/app/OpticalMouse.cpp
    src/app/Settings.cpp
    src/app/UpdateChecker.cpp
    src/ble/BleScanner.cpp
    src/ble/Switch2Link.cpp
    src/output/DsuServer.cpp
    src/output/VirtualPad.cpp
    src/platform/UsbWatcher.cpp
    src/ui/AppUI.cpp
    src/ui/AppUI_Device.cpp
    src/ui/AppUI_Dialogs.cpp
    src/ui/AppUI_Home.cpp
    src/ui/AppUI_Settings.cpp
    src/ui/Layout.cpp
    src/ui/Sounds.cpp
    src/ui/Widgets.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(sw2-adapter PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(sw2-adapter PRIVATE
    sw2_core nlohmann_json::nlohmann_json ViGEmClient::ViGEmClient
    setupapi hid windowsapp ws2_32 d3d11 dxgi dwmapi winmm shell32 winhttp cfgmgr32
)

target_compile_definitions(sw2-adapter PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_options(sw2-adapter PRIVATE /W3 /permissive- /utf-8
    /wd4828)   # ViGEm/Client.h has Latin-1 author names in comments
set_target_properties(sw2-adapter PROPERTIES OUTPUT_NAME "sw2-adapter")
