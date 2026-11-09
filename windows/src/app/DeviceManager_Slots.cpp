// Virtual controllers ("slots"), rumble and player LEDs.

#include "app/DeviceManager.h"

#include <Windows.h>
#include <ViGEm/Client.h>

#include <algorithm>

namespace sw2 {

namespace {
constexpr int MAX_SLOTS = 8;
constexpr int DSU_SLOTS = 4;   // the DSU protocol has four controller slots
// Rumble thread pacing: fast only while something actually vibrates.
constexpr auto RUMBLE_ACTIVE_TICK = std::chrono::milliseconds(4);
constexpr auto RUMBLE_IDLE_TICK   = std::chrono::milliseconds(16);
constexpr auto RUMBLE_EMPTY_TICK  = std::chrono::milliseconds(100);
// Output listener pacing when get_output returns immediately.
constexpr auto OUTPUT_POLL        = std::chrono::milliseconds(8);
constexpr auto OUTPUT_ERROR_RETRY = std::chrono::milliseconds(50);
constexpr float TEST_RUMBLE_AMP   = 0.8f;

bool UsesDsuSlot(int player) { return player >= 1 && player <= DSU_SLOTS; }
} // namespace

// ===========================================================================
// Slot assignment
// ===========================================================================

int DeviceManager::NextPlayerLocked() const {
    for (int p = 1; p <= MAX_SLOTS; ++p) {
        const bool used = std::any_of(m_slots.begin(), m_slots.end(), [p](const auto& s) { return s->player == p; });
        if (!used) return p;
    }
    return 0;
}

uint8_t DeviceManager::LedPattern(int player) {
    // P1..P4 light up 1..4 LEDs like the console; P5..P8 use distinct patterns.
    static const uint8_t patterns[MAX_SLOTS + 1] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x09, 0x05, 0x0D, 0x06};
    return player >= 1 && player <= MAX_SLOTS ? patterns[player] : 0x01;
}

std::shared_ptr<DeviceManager::Slot> DeviceManager::AssignSlotLocked(const std::shared_ptr<Device>& d) {
    if (d->slot) return d->slot;   // reconnect: back to the very same virtual controller

    const bool isL = d->kind == ControllerKind::JoyConL, isR = d->kind == ControllerKind::JoyConR;
    if ((isL || isR) && m_settings.combineJoyCons) {
        // Join a slot whose other half is waiting for a partner.
        for (auto& s : m_slots) {
            if (s->solo) continue;
            std::lock_guard<std::mutex> sl(s->mutex);
            if (isL && !s->left && s->right) { s->left = d->address;  d->slot = s; return s; }
            if (isR && !s->right && s->left) { s->right = d->address; d->slot = s; return s; }
        }
    }
    return CreateSlotLocked(d);
}

std::shared_ptr<DeviceManager::Slot> DeviceManager::CreateSlotLocked(const std::shared_ptr<Device>& d) {
    const int player = NextPlayerLocked();
    if (!player) { Log("Virtual controller limit reached"); return nullptr; }

    auto s = std::make_shared<Slot>();
    s->player = player;
    s->target = vigem_target_ds4_alloc();
    if (!VIGEM_SUCCESS(vigem_target_add(m_vigem, s->target))) {
        Log("Failed to create the virtual controller (ViGEmBus)");
        vigem_target_free(s->target);
        return nullptr;
    }
    s->pad = VirtualPadManager::Create(m_vigem, s->target, "P" + std::to_string(player));
    s->pad->SetWatchdogEnabled(false);   // enabled once the stream is known to be continuous
    StartOutputListener(s);
    if (UsesDsuSlot(player)) {
        const uint8_t dsuSlot = uint8_t(player - 1);
        s->pad->SetDsuSink([this, dsuSlot](const DS4_REPORT_EX& r) {
            if (m_dsu.IsRunning()) m_dsu.UpdateController(dsuSlot, r);
        });
        if (m_dsu.IsRunning()) m_dsu.SetControllerConnected(dsuSlot);
    }
    {
        std::lock_guard<std::mutex> sl(s->mutex);
        if (d->kind == ControllerKind::JoyConL)      s->left = d->address;
        else if (d->kind == ControllerKind::JoyConR) s->right = d->address;
        else                                         s->solo = d->address;
    }
    m_slots.push_back(s);
    d->slot = s;
    return s;
}

void DeviceManager::RemoveFromSlotLocked(const std::shared_ptr<Device>& d) {
    const auto s = d->slot;
    d->slot.reset();
    if (!s) return;
    {
        std::lock_guard<std::mutex> sl(s->mutex);
        if (s->left == d->address)  { s->left = 0;  s->leftReport.clear(); }
        if (s->right == d->address) { s->right = 0; s->rightReport.clear(); }
        if (s->solo == d->address)  s->solo = 0;
    }
    if (s->pad) s->pad->ForceNeutral("controller removed");
    if (s->left || s->right || s->solo) {   // the other half stays
        RefreshLedsLocked(s);
        return;
    }
    // Last member gone: stop reports, then unplug the virtual controller.
    if (s->pad) s->pad->Close();
    UnplugSlot(s);
    if (UsesDsuSlot(s->player) && m_dsu.IsRunning()) m_dsu.SetControllerConnected(uint8_t(s->player - 1), false);
    m_slots.erase(std::remove(m_slots.begin(), m_slots.end(), s), m_slots.end());
}

bool DeviceManager::AnyOtherMemberConnectedLocked(const Slot& s, uint64_t except) const {
    for (uint64_t m : {s.left, s.right, s.solo}) {
        if (!m || m == except) continue;
        auto it = m_devices.find(m);
        if (it != m_devices.end() && it->second->state == DeviceState::Connected) return true;
    }
    return false;
}

void DeviceManager::UpdateWatchdogLocked(const std::shared_ptr<Slot>& s) {
    // Only safe when every member is known to stream continuously.
    bool any = false, allContinuous = true;
    for (uint64_t m : {s->left, s->right, s->solo}) {
        if (!m) continue;
        auto it = m_devices.find(m);
        if (it == m_devices.end()) continue;
        any = true;
        allContinuous &= it->second->streaming == Streaming::Continuous;
    }
    if (s->pad) s->pad->SetWatchdogEnabled(any && allContinuous);
}

void DeviceManager::RefreshLedsLocked(const std::shared_ptr<Slot>& s) {
    const uint8_t pattern = LedPattern(s->player);
    for (uint64_t m : {s->left, s->right, s->solo}) {
        if (!m) continue;
        auto it = m_devices.find(m);
        // A Joy-Con in mouse mode shows the mouse speed on its LEDs instead.
        if (it != m_devices.end() && it->second->link && it->second->mouseMode.load() == OpticalMouse::Off)
            it->second->link->SetPlayerLeds(pattern);
    }
}

// ===========================================================================
// Rumble: game -> virtual pad -> controllers
// ===========================================================================

void DeviceManager::StartOutputListener(const std::shared_ptr<Slot>& s) {
    // vigem_target_ds4_get_output() may wait until the game sends an output
    // report, so it runs on its own thread and NEVER under m_mutex.
    PVIGEM_CLIENT client = m_vigem;
    PVIGEM_TARGET target = s->target;
    auto listen = s->listen;
    std::weak_ptr<Slot> weak = s;
    ++m_listeners;
    std::thread([this, client, target, listen, weak] {
        while (listen->load()) {
            DS4_OUTPUT_DATA out{};
            const auto err = vigem_target_ds4_get_output(client, target, &out);
            if (!listen->load()) break;
            if (VIGEM_SUCCESS(err)) {
                if (auto slot = weak.lock()) slot->amp = std::max(out.LargeMotor, out.SmallMotor) / 255.f;
                std::this_thread::sleep_for(OUTPUT_POLL);
            } else {
                std::this_thread::sleep_for(OUTPUT_ERROR_RETRY);
            }
        }
        vigem_target_free(target);   // this thread owns the target once unplugged
        --m_listeners;
    }).detach();
}

void DeviceManager::UnplugSlot(const std::shared_ptr<Slot>& s) {
    if (!s->target) return;
    s->listen->store(false);
    if (m_vigem) vigem_target_remove(m_vigem, s->target);   // also aborts a pending get_output
    s->target = nullptr;                                     // freed by the listener thread
    s->amp = 0.f;
}

void DeviceManager::RumbleLoop() {
    while (m_running.load()) {
        std::vector<std::pair<std::shared_ptr<Switch2Link>, float>> work;
        bool anySlot = false, anyActive = false;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            const auto now = Clock::now();
            for (const auto& s : m_slots) {
                anySlot = true;
                const float amp = s->amp.load();   // no ViGEm calls under m_mutex
                for (uint64_t m : {s->left, s->right, s->solo}) {
                    if (!m) continue;
                    auto it = m_devices.find(m);
                    if (it == m_devices.end() || !it->second->link) continue;
                    const float a = now < it->second->testRumbleUntil ? std::max(amp, TEST_RUMBLE_AMP) : amp;
                    anyActive |= a > 0.f;
                    work.emplace_back(it->second->link, a);
                }
            }
        }
        // Links skip writes while one is in flight and keep retrying a "stop"
        // until it is delivered, so the idle tick still turns motors off.
        for (auto& [link, amp] : work) link->Rumble(amp);
        std::this_thread::sleep_for(!anySlot ? RUMBLE_EMPTY_TICK : anyActive ? RUMBLE_ACTIVE_TICK : RUMBLE_IDLE_TICK);
    }
}

} // namespace sw2
