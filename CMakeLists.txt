# SW2 Adapter — monorepo root.
#   core/     portable C++ (protocol decoding, DS4 report, i18n). No OS headers.
#   windows/  Windows desktop app (BLE via WinRT, ViGEmBus, ImGui/DX11) + MSI installer.
#   android/  Android app (planned; builds core/ through the NDK with its own Gradle/CMake).
cmake_minimum_required(VERSION 3.20)
project(sw2-joycons-adapter VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # single portable .exe
endif()

add_subdirectory(core)
if(WIN32)
    add_subdirectory(windows)
endif()
