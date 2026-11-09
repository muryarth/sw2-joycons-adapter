// Lifecycle, settings, events, scanning policy, connections and UI views.
// See DeviceManager.h for the session rules and the locking order.

#include "app/DeviceManager.h"
#include "app/DemoData.h"
#include "core/I18n.h"

#include <Windows.h>
#include <ViGEm/Client.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace sw2 {

namespace {
constexpr auto VISIBLE_FOR        = std::chrono::seconds(8);          // sync advertisement freshness
constexpr auto SAVE_DEBOUNCE      = std::chrono::milliseconds(400);   // coalesce settings writes
constexpr auto FIRST_DATA_TIMEOUT = std::chrono::seconds(3);          // init done -> first report
constexpr auto RECONNECT_GRACE    = std::chrono::seconds(2);          // after a drop, before retrying
constexpr auto LOSS_CONFIRM_DELAY = std::chrono::milliseconds(1500);  // wait for a USB cable before the 'lost' cue
constexpr auto USB_CONFIRM_TIMEOUT = std::chrono::seconds(3);          // cable plugged in, Bluetooth must drop by then
constexpr auto CHARGER_LEAD = std::chrono::milliseconds(1500);          // charger lifts the voltage before Windows reports the cable
constexpr auto BATTERY_SETTLE_AFTER_CHARGE = std::chrono::minutes(2);    // voltage relaxes after charging
constexpr auto CHARGE_ROLLBACK = std::chrono::milliseconds(300);         // battery current reacts with the charger
constexpr int  RETRY_STEP_SECONDS = 3;                                // backoff grows per failure...
constexpr int  RETRY_MAX_SECONDS  = 15;                               // ...up to this
constexpr size_t LOG_MAX_LINES    = 300;

std::string Timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char b[16];
    std::strftime(b, sizeof(b), "%H:%M:%S", &tm);
    return b;
}

int PolicyIntervalUs(UpdatePolicy p) {
    switch (p) {
        case UpdatePolicy::Hz120: return 8333;
        case UpdatePolicy::Hz60:  return 16667;
        default:                  return 0;
    }
}

std::wstring LogFile() { return SettingsDirectory() + L"\\log.txt"; }

bool IsVisible(TimePoint lastSeen, TimePoint now) {
    return lastSeen != TimePoint{} && now - lastSeen < VISIBLE_FOR;
}
} // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

DeviceManager::DeviceManager()
    : m_scanner([this](uint64_t a, const AdvertisementInfo& i, int16_t r) { OnAdvertisement(a, i, r); }),
      m_usb([this](uint16_t pid, bool arrived) { OnUsb(pid, arrived); }) {
    m_settings = LoadSettings();
    SetLang(m_settings.language == 1 ? Lang::Pt : Lang::En);
}

DeviceManager::~DeviceManager() { Shutdown(); }

bool DeviceManager::Init() {
    CreateDirectoryW(SettingsDirectory().c_str(), nullptr);
    if (FILE* f = _wfopen(LogFile().c_str(), L"wb")) std::fclose(f);   // fresh log per run
    Log("SW2 Adapter started");

    m_vigem = vigem_alloc();
    m_vigemOk = m_vigem && VIGEM_SUCCESS(vigem_connect(m_vigem));
    if (!m_vigemOk) Log("ViGEmBus not found: install the driver to create virtual controllers");

    VirtualPadManager::SetLogger([this](const std::string& s) { Log(s); });
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ApplyRuntimeLocked();
    }
    m_running = true;
    m_rumbleThread = std::thread([this] { RumbleLoop(); });
    if (!m_usb.Start()) Log("USB notifications unavailable (cable detection off)");
    return m_vigemOk;
}

void DeviceManager::Shutdown() {
    if (!m_running.exchange(false)) return;
    m_usb.Stop();
    m_scanner.Apply(ScanProfile::Off);
    if (m_rumbleThread.joinable()) m_rumbleThread.join();

    std::vector<std::shared_ptr<Switch2Link>> links;
    std::vector<std::shared_ptr<Slot>> slots;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& [address, d] : m_devices) {
            if (d->cancel) d->cancel->store(true);
            if (d->link) links.push_back(d->link);
            d->link.reset();
        }
        slots.swap(m_slots);
        FlushSaveLocked(/*force*/ true);
    }
    for (auto& l : links) { l->Rumble(0.f); l->Close(); }
    VirtualPadManager::Stop();
    for (auto& s : slots) UnplugSlot(s);
    m_dsu.Stop();
    // Give the output listeners a moment to leave their ViGEm call; if one is
    // still inside it, keep the client alive (the process is exiting anyway).
    for (int i = 0; i < 50 && m_listeners.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (m_vigem && m_listeners.load() == 0) { vigem_disconnect(m_vigem); vigem_free(m_vigem); }
    m_vigem = nullptr;
}

// ===========================================================================
// Settings
// ===========================================================================

AppSettings DeviceManager::Settings() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_settings;
}

void DeviceManager::UpdateSettings(const std::function<void(AppSettings&)>& fn) {
    std::lock_guard<std::mutex> lk(m_mutex);
    fn(m_settings);
    ApplyRuntimeLocked();
    MarkDirtyLocked();
}

void DeviceManager::UpdateProfile(uint64_t address, const std::function<void(DeviceProfile&)>& fn) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto* p = m_settings.Find(address)) { fn(*p); MarkDirtyLocked(); }
}

// Dragging a slider changes settings every frame: write the file once the
// changes settle instead of ~60 times per second.
void DeviceManager::MarkDirtyLocked() {
    m_saveDirty = true;
    m_saveDue = Clock::now() + SAVE_DEBOUNCE;
}

void DeviceManager::FlushSaveLocked(bool force) {
    if (!m_saveDirty || (!force && Clock::now() < m_saveDue)) return;
    m_saveDirty = false;
    if (!SaveSettings(m_settings)) Log("Failed to save settings");
}

void DeviceManager::ApplyRuntimeLocked() {
    SetLang(m_settings.language == 1 ? Lang::Pt : Lang::En);
    m_runtime.deadzone = m_settings.deadzone;
    m_runtime.swap     = m_settings.swapABXY;
    m_runtime.gyro     = m_settings.gyroSource;

    VirtualPadManager::Settings vp;
    vp.minIntervalUs = PolicyIntervalUs(m_settings.updatePolicy);
    vp.heartbeatMs   = m_settings.heartbeatMs;
    vp.watchdogMs    = m_settings.watchdogMs;
    vp.emulateTouch  = false;   // a permanently "touched" pad confuses Steam
    VirtualPadManager::SetSettings(vp);

    if (m_settings.dsuServer && !m_dsu.IsRunning()) {
        if (m_dsu.Start()) {
            Log("DSU server listening on port 26760");
            for (auto& s : m_slots) if (s->player <= 4) m_dsu.SetControllerConnected(uint8_t(s->player - 1));
        } else {
            Log("Could not start the DSU server (port 26760 in use?)");
        }
    } else if (!m_settings.dsuServer && m_dsu.IsRunning()) {
        m_dsu.Stop();
        Log("DSU server stopped");
    }
}

// ===========================================================================
// Events & log
// ===========================================================================

void DeviceManager::Emit(EventType t, const std::string& text, uint64_t address) {
    {
        std::lock_guard<std::mutex> lk(m_eventMutex);
        m_events.push_back({t, text, address});
    }
    Log(text);
}

void DeviceManager::Log(const std::string& line) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    const std::string entry = Timestamp() + "  " + line;
    m_log.push_back(entry);
    while (m_log.size() > LOG_MAX_LINES) m_log.pop_front();
    // Also on disk, so a session can be inspected after the fact.
    if (FILE* f = _wfopen(LogFile().c_str(), L"ab")) {
        std::fwrite(entry.data(), 1, entry.size(), f);
        std::fwrite("\r\n", 1, 2, f);
        std::fclose(f);
    }
}

std::vector<Event> DeviceManager::DrainEvents() {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

std::vector<std::string> DeviceManager::LogLines() const {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    return {m_log.begin(), m_log.end()};
}

std::string DeviceManager::NameLocked(uint64_t address, ControllerKind kind) const {
    if (auto* p = m_settings.Find(address); p && !p->name.empty()) return p->name;
    return KindName(kind);
}

// ===========================================================================
// Scanning policy
// ===========================================================================

void DeviceManager::Tick(bool focused) {
    ScanProfile profile = ScanProfile::Off;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_focused = focused;
        const auto now = Clock::now();
        FlushSaveLocked(/*force*/ false);
        ConfirmPendingLossesLocked(now);

        bool missing = false, anyConnected = false;
        for (auto it = m_devices.begin(); it != m_devices.end();) {
            Device& d = *it->second;
            if (d.inSession && d.state == DeviceState::WaitingSync) missing = true;
            if (d.state == DeviceState::Connected) anyConnected = true;

            const bool visible = IsVisible(d.lastSeen, now);
            const bool known = m_settings.Find(d.address) != nullptr;
            if (!visible) d.wasVisible = false;
            if (d.state == DeviceState::Available && !visible) {
                if (!known && !d.inSession) { it = m_devices.erase(it); continue; }   // stopped advertising
                if (known) d.state = DeviceState::Offline;
            }
            ++it;
        }

        const bool wantForeground = focused && (m_settings.autoSearch || now < m_manualSearchUntil);
        if (wantForeground || missing)
            profile = anyConnected ? ScanProfile::Coexistence : ScanProfile::Fast;
    }
    m_scanner.Apply(profile);
}

void DeviceManager::SearchNow(int seconds) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_manualSearchUntil = Clock::now() + std::chrono::seconds(seconds);
}

bool DeviceManager::Searching() const {
    return m_scanner.Current() != ScanProfile::Off;
}

void DeviceManager::OnAdvertisement(uint64_t address, const AdvertisementInfo& info, int16_t rssi) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_running) return;
    // A controller calling its Switch 2 (button press while asleep) must never
    // be taken over: the user wants to play on the console.
    if (!info.syncMode) {
        if (m_wakeLogged.insert(address).second)
            Log(std::string(KindName(info.kind)) + " " + FormatAddress(address) +
                " is calling its console (not sync mode) · ignored");
        return;
    }
    const auto now = Clock::now();

    auto& entry = m_devices[address];
    if (!entry) {
        entry = std::make_shared<Device>();
        entry->address = address;
        entry->kind = info.kind;
        entry->state = DeviceState::Available;
    }
    const std::shared_ptr<Device> d = entry;
    if (d->kind == ControllerKind::Unknown) d->kind = info.kind;
    d->lastSeen = now;
    d->rssi = rssi;

    if (d->state == DeviceState::Connected || d->state == DeviceState::Connecting) return;

    const bool known = m_settings.Find(address) != nullptr;
    const bool mayRetry = now >= d->retryAfter;
    if (d->inSession && d->state == DeviceState::WaitingSync) {
        if (mayRetry) StartConnectLocked(d);           // works in the background too
        return;
    }
    if (known && m_focused && mayRetry) {
        StartConnectLocked(d);                          // known: connect on sync
        return;
    }
    if (d->state == DeviceState::Offline) d->state = DeviceState::Available;
    if (!d->wasVisible) {
        d->wasVisible = true;
        if (!known && m_focused)
            Emit(EventType::Found, std::string(KindName(d->kind)) + tr(" found", " encontrado"), address);
    }
}

// ===========================================================================
// Connect / disconnect
// ===========================================================================

void DeviceManager::Connect(uint64_t address) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_devices.find(address);
    if (it == m_devices.end()) return;
    const auto& d = it->second;
    if (d->state == DeviceState::Connected || d->state == DeviceState::Connecting) return;
    d->retryAfter = {};
    StartConnectLocked(d);
}

void DeviceManager::StartConnectLocked(const std::shared_ptr<Device>& d) {
    if (d->kind == ControllerKind::Unknown) {
        d->error = tr("This controller model is not supported yet", "Modelo de controle ainda não suportado");
        return;
    }
    if (!m_vigemOk) {
        d->error = tr("ViGEmBus is not installed", "ViGEmBus não instalado");
        return;
    }
    d->state = DeviceState::Connecting;
    d->error.clear();
    d->cancel = std::make_shared<std::atomic<bool>>(false);
    std::thread(&DeviceManager::ConnectWorker, this, d, d->cancel).detach();
}

void DeviceManager::ConnectWorker(std::shared_ptr<Device> d, std::shared_ptr<std::atomic<bool>> cancel) {
    const auto log = [this](const std::string& s) { Log(s); };
    Log(std::string("Connecting ") + KindName(d->kind) + " " + FormatAddress(d->address));

    std::string err;
    auto link = Switch2Link::Open(d->address, d->kind, err, cancel.get(), log);
    if (!link) { OnConnectFailed(d, *cancel, err); return; }
    if (cancel->load() || !m_running) { link->Close(); return; }

    Log("  sending init sequence");
    link->SendInit();

    std::weak_ptr<Device> weak = d;
    const bool subscribed = link->Start(
        [this, weak](const std::vector<uint8_t>& r) { if (auto dd = weak.lock()) OnInput(dd, r); },
        [this, weak](bool connected) { if (auto dd = weak.lock()) OnLinkStatus(dd, connected); },
        log);
    if (!subscribed) {
        link->Close();
        OnConnectFailed(d, *cancel, tr("could not subscribe to the controller data", "não foi possível assinar os dados do controle"));
        return;
    }

    // Success is judged by data actually arriving, not by the CCCD status.
    const auto t0 = Clock::now();
    while (link->PacketCount() == 0 && Clock::now() - t0 < FIRST_DATA_TIMEOUT && !cancel->load())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (link->PacketCount() == 0) {
        link->Close();
        OnConnectFailed(d, *cancel, tr("the controller sent no data after init", "o controle não enviou dados após a inicialização"));
        return;
    }
    Log("  receiving data (" + std::to_string(int(std::chrono::duration<float, std::milli>(Clock::now() - t0).count())) + " ms)");
    link->KeepAlive();

    if (!OnConnected(d, link, *cancel)) link->Close();   // outside the lock: Close() may block briefly
}

void DeviceManager::OnConnectFailed(const std::shared_ptr<Device>& d, const std::atomic<bool>& cancel, const std::string& why) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (cancel.load()) return;
    d->failures++;
    // Back off: rapid repeated connects make these controllers go silent.
    d->retryAfter = Clock::now() + std::chrono::seconds(std::min(RETRY_STEP_SECONDS * d->failures, RETRY_MAX_SECONDS));
    d->error = why;
    d->state = d->inSession ? DeviceState::WaitingSync
             : IsVisible(d->lastSeen, Clock::now()) ? DeviceState::Available : DeviceState::Offline;
    Emit(EventType::Failed, std::string(tr("Could not connect ", "Falha ao conectar ")) + NameLocked(d->address, d->kind) + ": " + why, d->address);
}

bool DeviceManager::OnConnected(const std::shared_ptr<Device>& d, const std::shared_ptr<Switch2Link>& link,
                                const std::atomic<bool>& cancel) {
    std::lock_guard<std::mutex> lk(m_mutex);
    // Checked under the same lock DisconnectAndForget uses, so a controller
    // forgotten while connecting can never end up with a virtual pad.
    if (cancel.load() || !m_running) return false;
    const bool reconnect = d->inSession;
    d->link = link;
    d->inSession = true;
    d->state = DeviceState::Connected;
    d->failures = 0;
    d->error.clear();
    d->lastPacket = {};
    d->intervalMs = d->jitterMs = 0.f;
    d->streaming = Streaming::Measuring;
    d->batteryMv = 0.f;
    d->batteryHistory.clear();
    d->batteryHoldUntil = {};
    d->drainRefTime = {};
    d->drainRefPct = -1;
    d->drainPctPerHour = 0.f;
    d->onUsb = false;              // back on Bluetooth
    d->usbPendingSince = {};
    d->chargingByCurrent = false;
    d->chargingShown = false;
    d->currentStreak = 0;
    d->chargingOnBle = false;
    // A cable was already in this model (nothing opened it over USB): with a
    // single controller of the model in the session, it is this one charging.
    if (int& loose = m_usbUnmatched[d->kind]; loose > 0 && SessionCountLocked(d->kind) == 1) {
        --loose;
        d->chargingOnBle = true;
        Log("  cable already in · charging while playing");
        RefreshChargingLocked(*d, Clock::now());
    }
    d->lostPendingSince = {};
    d->mouse.Disable();
    d->mouseMode = 0;

    if (!m_settings.Find(d->address)) {
        DeviceProfile p;
        p.address = d->address;
        p.kind = d->kind;
        p.name = KindName(d->kind);
        m_settings.known.push_back(p);
        MarkDirtyLocked();
    }
    const auto slot = AssignSlotLocked(d);
    if (slot) { UpdateWatchdogLocked(slot); RefreshLedsLocked(slot); }
    const std::string where = slot ? " · P" + std::to_string(slot->player) : "";
    Emit(reconnect ? EventType::Reconnected : EventType::Connected,
         NameLocked(d->address, d->kind) + (reconnect ? tr(" reconnected", " reconectado") : tr(" connected", " conectado")) + where,
         d->address);
    return true;
}

void DeviceManager::OnLinkStatus(const std::shared_ptr<Device>& d, bool connected) {
    Log(std::string("BLE link status ") + FormatAddress(d->address) + ": " + (connected ? "connected" : "disconnected"));
    if (connected) return;
    std::shared_ptr<Switch2Link> link;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (d->state != DeviceState::Connected) return;
        DumpRecentLocked(*d, "link dropped");
        link = std::move(d->link);
        d->state = DeviceState::WaitingSync;   // only session devices are ever Connected
        d->retryAfter = Clock::now() + RECONNECT_GRACE;
        if (const auto s = d->slot) {
            {
                // Forget the half that dropped so a combined pair can't keep its held buttons.
                std::lock_guard<std::mutex> sl(s->mutex);
                if (s->left == d->address)  s->leftReport.clear();
                if (s->right == d->address) s->rightReport.clear();
            }
            if (!AnyOtherMemberConnectedLocked(*s, d->address) && s->pad) s->pad->ForceNeutral("controller disconnected");
        }
        // Inputs are released right away; the "lost" cue waits a moment in
        // case the drop was caused by plugging a USB cable (see OnUsb).
        if (d->usbPendingSince != TimePoint{} || d->chargingOnBle) ConfirmUsbLocked(d);   // the cable was in this controller
        else if (!d->onUsb) d->lostPendingSince = Clock::now();
    }
    if (link) std::thread([link] { link->Close(); }).detach();
}

void DeviceManager::ConfirmPendingLossesLocked(TimePoint now) {
    for (auto& [address, d] : m_devices) {
        if (d->usbPendingSince != TimePoint{} && now - d->usbPendingSince >= USB_CONFIRM_TIMEOUT) {
            d->usbPendingSince = {};
            if (d->state != DeviceState::Connected) {
                ConfirmUsbLocked(d);
            } else if (SessionCountLocked(d->kind) == 1) {
                // Bluetooth kept: nothing opened the controller over USB, so it
                // charges while still playing through the app.
                d->chargingOnBle = true;
                RefreshChargingLocked(*d, now);
            } else {
                // Several controllers of this model: the cable can't be attributed.
                ++m_usbUnmatched[d->kind];
                Log(NameLocked(address, d->kind) + ": Bluetooth still up, cable not attributed");
            }
        }
        if (d->lostPendingSince == TimePoint{} || now - d->lostPendingSince < LOSS_CONFIRM_DELAY) continue;
        d->lostPendingSince = {};
        if (d->onUsb || d->state != DeviceState::WaitingSync) continue;   // cable, or already back
        Emit(EventType::Lost, NameLocked(address, d->kind) +
                                  tr(" disconnected · press sync to bring it back", " desconectou · aperte sync para voltar"),
             address);
    }
}

void DeviceManager::DumpRecentLocked(Device& d, const char* why) {
    if (d.recent.empty()) return;
    const auto end = d.recent.back().first;
    Log(std::string("  last reports before ") + why + " (" + std::to_string(d.recent.size()) + ", newest last):");
    std::vector<uint8_t> prev;
    for (const auto& [t, r] : d.recent) {
        // Buttons/sticks (bytes 0..15) and the battery fields; a line is printed
        // only when the buttons, sticks or battery current changed.
        const size_t n = std::min<size_t>(r.size(), 16);
        std::vector<uint8_t> head(r.begin() + std::min<size_t>(4, n), r.begin() + n);   // skip the packet counter
        if (r.size() >= 0x23) { head.push_back(r[0x21]); head.push_back(r[0x22]); }
        if (head == prev) continue;
        prev = head;
        std::string hex;
        char b[4];
        for (size_t i = 0; i < n; ++i) { std::snprintf(b, sizeof(b), "%02X ", r[i]); hex += b; }
        if (r.size() >= 0x23) {   // battery voltage (mV) and current (raw, signed) next to it
            char bat[48];
            std::snprintf(bat, sizeof(bat), "| mV=%d I=%d", r[0x1F] | (r[0x20] << 8), int(int16_t(r[0x21] | (r[0x22] << 8))));
            hex += bat;
        }
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - t).count();
        Log("    -" + std::to_string(ms) + " ms len=" + std::to_string(r.size()) + " : " + hex);
    }
    d.recent.clear();
}

// A cable plugged into a controller outside this session carries no address:
// show it only when exactly one known, offline controller of that model exists.
bool DeviceManager::UsbOnlyLocked(const DeviceProfile& p) const {
    const auto it = m_usbUnmatched.find(p.kind);
    if (it == m_usbUnmatched.end() || it->second <= 0) return false;
    int candidates = 0;
    for (const auto& q : m_settings.known) {
        if (q.kind != p.kind) continue;
        const auto d = m_devices.find(q.address);
        if (d == m_devices.end() || (!d->second->inSession && d->second->state == DeviceState::Offline)) ++candidates;
    }
    return candidates == 1;
}

// Plugging the cable makes a Switch 2 controller drop Bluetooth and switch to
// USB by itself (the USB connection appears to Windows as a separate generic
// gamepad). The USB device carries no Bluetooth address, so it is matched to
// the session controller of the same model that is connected or just dropped.
void DeviceManager::OnUsb(uint16_t productId, bool arrived) {
    const ControllerKind kind = KindFromProductId(productId);
    if (kind == ControllerKind::Unknown) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_running) return;

    auto score = [&](const Device& d) {
        if (d.kind != kind || !d.inSession) return 0;
        if (arrived) {
            if (d.onUsb || d.chargingOnBle || d.usbPendingSince != TimePoint{}) return 0;   // already has a cable
            if (d.chargingByCurrent && d.state == DeviceState::Connected) return 4;   // its battery says it is charging
            if (d.lostPendingSince != TimePoint{}) return 3;   // dropped a moment ago: the cable did it
            if (d.state == DeviceState::Connected) return 2;   // drop not reported yet
            if (d.state == DeviceState::WaitingSync) return 1;
            return 0;
        }
        return d.onUsb || d.chargingOnBle || d.usbPendingSince != TimePoint{} ? 1 : 0;
    };
    std::shared_ptr<Device> best;
    int bestScore = 0;
    for (auto& [address, d] : m_devices)
        if (const int s = score(*d); s > bestScore) { best = d; bestScore = s; }

    if (!best) {
        // Not a controller of this session (e.g. charging while the app opens).
        // Remember it so a single known controller of that model can show it.
        int& n = m_usbUnmatched[kind];
        n = arrived ? n + 1 : (n > 0 ? n - 1 : 0);
        Log(std::string(KindName(kind)) + (arrived ? " plugged in via USB (not connected over Bluetooth)" : " unplugged from USB"));
        for (const auto& p : m_settings.known) {
            if (p.kind != kind || !UsbOnlyLocked(p)) continue;
            if (arrived)
                Emit(EventType::UsbConnected, NameLocked(p.address, p.kind) + tr(" plugged in · charging", " conectado por cabo · carregando"), p.address);
            break;
        }
        return;
    }
    if (arrived) {
        if (best->state == DeviceState::Connected) {
            if (best->chargingByCurrent) {   // already charging by its battery current: this is its cable
                best->chargingOnBle = true;
                Log(NameLocked(best->address, best->kind) + ": cable matches the controller that is charging");
                return;
            }
            // Not confirmed yet: the USB device only names the model. When a USB
            // host driver (Steam) opens the controller it leaves Bluetooth within
            // a second; otherwise it just charges and keeps playing over
            // Bluetooth. Inputs keep flowing meanwhile (the reports stay clean);
            // see OnLinkStatus and ConfirmPendingLossesLocked for the outcomes.
            DumpRecentLocked(*best, "cable plugged in");
            best->usbPendingSince = Clock::now();
            RollBackBatteryLocked(*best, best->usbPendingSince - CHARGER_LEAD);
            Log("  cable seen, watching the Bluetooth link");
            return;
        }
        ConfirmUsbLocked(best);   // Bluetooth already gone: it is this controller
    } else {
        const bool stillPlaying = best->state == DeviceState::Connected;
        best->onUsb = false;
        best->usbPendingSince = {};
        best->chargingOnBle = false;
        if (stillPlaying) {
            DumpRecentLocked(*best, "cable removed");
            Log(NameLocked(best->address, best->kind) + ": cable removed, still on Bluetooth");
            RefreshChargingLocked(*best, Clock::now());   // stays on while the battery current says so
            return;
        }
        Emit(EventType::UsbRemoved, NameLocked(best->address, best->kind) +
                                        tr(" cable removed · press sync to play over Bluetooth",
                                           " cabo removido · aperte sync para jogar pelo Bluetooth"),
             best->address);
    }
}

// The controller is on USB for sure. Over USB it is a regular gamepad of its
// own (Steam opens it); keeping its virtual DS4 plugged in next to it makes
// Steam flip between configurations and type arrow keys/Tab. So the virtual
// controller is released (inputs neutral first) and recreated on the next
// Bluetooth connection.
// Charging has two sources: the battery current in the controller's own
// reports (instant, per controller) and a cable seen by Windows while
// Bluetooth stays up (fallback). This keeps the shown state and the battery
// estimate in step with either.
void DeviceManager::RefreshChargingLocked(Device& d, TimePoint now) {
    const bool charging = d.chargingByCurrent || d.chargingOnBle;
    if (charging == d.chargingShown) return;
    d.chargingShown = charging;
    const std::string name = NameLocked(d.address, d.kind);
    if (charging) {
        // The charger lifts the voltage at once: go back to the level before it.
        RollBackBatteryLocked(d, now - CHARGE_ROLLBACK);
        Log(name + ": charging" + (d.chargingByCurrent ? " (battery current)" : " (cable, Bluetooth kept)"));
        Emit(EventType::UsbConnected, name + tr(" charging · still playing over Bluetooth", " carregando · continua jogando pelo Bluetooth"),
             d.address);
    } else {
        // Right after charging the voltage reads high for a while: keep the
        // pre-charge level and measure again once it settled. Without one (it
        // connected already charging) measure right away; the smoothing follows
        // the voltage as it settles. The drain estimate restarts either way.
        if (d.batteryMv > 0.f) d.batteryHoldUntil = now + BATTERY_SETTLE_AFTER_CHARGE;
        d.batteryHistory.clear();
        d.drainRefTime = {};
        d.drainRefPct = -1;
        d.drainPctPerHour = 0.f;
        Log(name + ": charging stopped");
    }
}

// Restore the level measured before the charger touched it.
void DeviceManager::RollBackBatteryLocked(Device& d, TimePoint before) {
    for (auto it = d.batteryHistory.rbegin(); it != d.batteryHistory.rend(); ++it)
        if (it->first <= before) { d.batteryMv = it->second; break; }
    d.batteryHistory.clear();
}

int DeviceManager::SessionCountLocked(ControllerKind kind) const {
    int n = 0;
    for (const auto& [address, d] : m_devices) n += d->inSession && d->kind == kind;
    return n;
}

void DeviceManager::ConfirmUsbLocked(const std::shared_ptr<Device>& d) {
    d->onUsb = true;
    d->chargingOnBle = false;
    d->chargingByCurrent = false;
    d->chargingShown = false;
    d->usbPendingSince = {};
    d->lostPendingSince = {};
    if (d->slot) {
        Log("  releasing virtual controller P" + std::to_string(d->slot->player) + " while on USB");
        RemoveFromSlotLocked(d);
    }
    Emit(EventType::UsbConnected, NameLocked(d->address, d->kind) +
                                      tr(" plugged in · charging. The controller paused Bluetooth",
                                         " conectado por cabo · carregando. O controle pausou o Bluetooth"),
         d->address);
}

void DeviceManager::DisconnectAndForget(uint64_t address) {
    std::shared_ptr<Switch2Link> link;
    std::string name;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (auto it = m_devices.find(address); it != m_devices.end()) {
            const auto d = it->second;
            name = NameLocked(address, d->kind);
            if (d->cancel) d->cancel->store(true);
            link = std::move(d->link);
            d->inSession = false;
            d->state = DeviceState::Offline;
            RemoveFromSlotLocked(d);
            m_devices.erase(it);
        } else if (const auto* p = m_settings.Find(address)) {
            name = p->name;
        }
        m_settings.Forget(address);
        MarkDirtyLocked();
    }
    if (link) std::thread([link] { link->Rumble(0.f); link->Close(); }).detach();
    Emit(EventType::Disconnected,
         (name.empty() ? std::string(tr("Controller", "Controle")) : name) + tr(" disconnected and forgotten", " desconectado e esquecido"),
         address);
}

void DeviceManager::Identify(uint64_t address) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_devices.find(address); it != m_devices.end() && it->second->link) it->second->link->Identify();
}

void DeviceManager::TestRumble(uint64_t address) {
    constexpr auto TEST_RUMBLE = std::chrono::milliseconds(450);
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_devices.find(address); it != m_devices.end()) it->second->testRumbleUntil = Clock::now() + TEST_RUMBLE;
}

// ===========================================================================
// Views for the UI
// ===========================================================================

std::vector<DeviceView> DeviceManager::Snapshot() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    const auto now = Clock::now();
    std::vector<DeviceView> out;
    out.reserve(m_devices.size() + m_settings.known.size());

    auto add = [&](const Device* d, const DeviceProfile* p) {
        DeviceView v;
        v.address = d ? d->address : p->address;
        v.kind    = d ? d->kind : p->kind;
        v.name    = p && !p->name.empty() ? p->name : KindName(v.kind);
        v.known   = p != nullptr;
        v.state   = d ? d->state : DeviceState::Offline;
        if (d) {
            v.inSession  = d->inSession;
            v.visible    = IsVisible(d->lastSeen, now);
            v.rssi       = d->rssi;
            v.intervalMs = d->intervalMs;
            v.jitterMs   = d->jitterMs;
            v.error      = d->error;
            v.mouseMode  = d->mouseMode.load();
            v.streaming  = d->streaming;
            v.onUsb      = d->onUsb;
            v.charging   = d->chargingShown;
            if (d->state == DeviceState::Connected && d->batteryMv > 0.f) {
                v.batteryMv  = int(d->batteryMv);
                v.batteryPct = BatteryPercent(v.batteryMv);
                v.batteryMinutesLeft = MinutesLeft(*d, v.batteryPct, v.batteryMeasured);
            }
            if (d->slot) {
                v.player   = d->slot->player;
                v.combined = d->slot->left && d->slot->right;
            }
        }
        if (!v.onUsb && p && v.state == DeviceState::Offline) v.onUsb = UsbOnlyLocked(*p);
        out.push_back(std::move(v));
    };
    for (const auto& [address, d] : m_devices) add(d.get(), m_settings.Find(address));
    for (const auto& p : m_settings.known) if (!m_devices.count(p.address)) add(nullptr, &p);
    if (m_demo) demo::AppendViews(out);
    return out;
}

bool DeviceManager::LiveInputFor(uint64_t address, LiveInput& out) const {
    if (m_demo && demo::IsDemoAddress(address)) { demo::FillLiveInput(out); return true; }

    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_devices.find(address);
    if (it == m_devices.end() || it->second->state != DeviceState::Connected) return false;
    const Device& d = *it->second;
    out = LiveInput{};
    out.hasLeft  = d.kind != ControllerKind::JoyConR;
    out.hasRight = d.kind != ControllerKind::JoyConL;
    if (out.hasLeft)  ExtractRawStick(d.lastReport, true, out.rawLX, out.rawLY);
    if (out.hasRight) ExtractRawStick(d.lastReport, false, out.rawRX, out.rawRY);
    if (d.slot) {
        std::lock_guard<std::mutex> sl(d.slot->mutex);
        out.report = d.slot->last;
        out.valid  = d.slot->hasLast;
    }
    return true;
}

} // namespace sw2
