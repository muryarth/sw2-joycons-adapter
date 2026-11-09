#include "ble/Switch2Link.h"
#include "core/I18n.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Foundation;

namespace sw2 {

namespace {
// GATT characteristic UUIDs (from joycon2cpp).
const guid UUID_INPUT        {L"ab7de9be-89fe-49ad-828f-118f09df7fd2"};
const guid UUID_WRITE        {L"649d4ac9-8eb7-4e6c-af44-1ea54fe5f005"};
const guid UUID_RESP_BASIC   {L"c765a961-d9d8-4d36-a20a-5315b111836a"};
const guid UUID_RESP_L       {L"63a3810f-aec7-474b-9010-3d52403cb996"};
const guid UUID_RESP_R       {L"640ca58e-0e88-410c-a7f3-426faf2b690b"};
const guid UUID_RESP_PRO     {L"506d9f7d-4278-4e95-a549-326ba77657e0"};
const guid UUID_RESP_GC      {L"46f6ad29-cdaf-4569-a2fe-339020b94604"};
const guid UUID_COMMAND_L    {L"ce49a830-dced-48ae-931e-c8cf88aadbea"};
const guid UUID_COMMAND_R    {L"65a724b3-f1e7-4a61-8078-a342376b27ff"};
const guid UUID_COMMAND_PRO  {L"3dacbc7e-6955-40b5-8eaf-6f9809e8b379"};
const guid UUID_COMMAND_GC   {L"af95885e-44b3-4a24-9cf0-483cc129469a"};
const guid UUID_VIBRATION_L  {L"289326cb-a471-485d-a8f4-240c14f18241"};
const guid UUID_VIBRATION_R  {L"fa19b0fb-cd1f-46a7-84a1-bbb09e00c149"};
const guid UUID_VIBRATION_PRO{L"cc483f51-9258-427d-a939-630c31f72b05"};
const guid UUID_VIBRATION_GC {L"3f8fb670-ab25-45bf-b540-38c72834d064"};

bool Cancelled(const std::atomic<bool>* c) { return c && c->load(); }

const char* StatusName(GattCommunicationStatus s) {
    switch (s) {
        case GattCommunicationStatus::Success:       return "Success";
        case GattCommunicationStatus::Unreachable:   return "Unreachable";
        case GattCommunicationStatus::ProtocolError: return "ProtocolError";
        case GattCommunicationStatus::AccessDenied:  return "AccessDenied";
        default:                                     return "?";
    }
}

std::string Hr(const winrt::hresult_error& e) {
    char b[160];
    std::snprintf(b, sizeof(b), "0x%08X %s", unsigned(e.code()), winrt::to_string(e.message()).c_str());
    return b;
}
} // namespace

std::shared_ptr<Switch2Link> Switch2Link::Open(uint64_t address, ControllerKind kind, std::string& error,
                                               const std::atomic<bool>* cancel, const LogFn& log) {
    auto L = [&](const std::string& s) { if (log) log(s); };
    std::shared_ptr<Switch2Link> link(new Switch2Link());
    link->m_address = address;
    link->m_kind = kind;

    try {
        link->m_device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
    } catch (const winrt::hresult_error& e) {
        L("  open device: exception " + Hr(e));
    } catch (...) {}
    if (!link->m_device) { error = tr("Could not open the Bluetooth device", "Não foi possível abrir o dispositivo Bluetooth"); return nullptr; }
    L("  device opened");

    GattDeviceServicesResult services{nullptr};
    for (int attempt = 1; attempt <= 6 && !Cancelled(cancel); ++attempt) {
        try {
            services = link->m_device.GetGattServicesAsync(BluetoothCacheMode::Uncached).get();
            if (services.Status() == GattCommunicationStatus::Success) break;
            L(std::string("  GATT services: ") + StatusName(services.Status()) + " (attempt " + std::to_string(attempt) + ")");
        } catch (const winrt::hresult_error& e) {
            L("  GATT services: exception " + Hr(e));
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    if (Cancelled(cancel)) { error = tr("Cancelled", "Cancelado"); link->Close(); return nullptr; }
    if (!services || services.Status() != GattCommunicationStatus::Success) {
        error = tr("Could not read the controller services", "Falha ao ler os serviços do controle");
        link->Close();
        return nullptr;
    }

    const bool isL = kind == ControllerKind::JoyConL, isR = kind == ControllerKind::JoyConR;
    const bool isPro = kind == ControllerKind::ProController;
    const guid wantCommand   = isL ? UUID_COMMAND_L : isR ? UUID_COMMAND_R : isPro ? UUID_COMMAND_PRO : UUID_COMMAND_GC;
    const guid wantVibration = isL ? UUID_VIBRATION_L : isR ? UUID_VIBRATION_R : isPro ? UUID_VIBRATION_PRO : UUID_VIBRATION_GC;
    const guid wantRespExt   = isL ? UUID_RESP_L : isR ? UUID_RESP_R : isPro ? UUID_RESP_PRO : UUID_RESP_GC;

    int total = 0;
    for (auto svc : services.Services()) {
        GattCharacteristicsResult chars{nullptr};
        try { chars = svc.GetCharacteristicsAsync().get(); } catch (...) { continue; }
        if (chars.Status() != GattCommunicationStatus::Success) continue;
        for (auto ch : chars.Characteristics()) {
            ++total;
            const guid u = ch.Uuid();
            if (u == UUID_INPUT)      link->m_input = ch;
            if (u == UUID_WRITE)      link->m_write = ch;
            if (u == wantCommand)     link->m_command = ch;
            if (u == wantVibration)   link->m_vibration = ch;
            if (u == UUID_RESP_BASIC) link->m_respBasic = ch;
            if (u == wantRespExt)     link->m_respExt = ch;
        }
    }
    char found[200];
    std::snprintf(found, sizeof(found), "  %d characteristics · input:%s write:%s command:%s vibration:%s resp:%s/%s",
                  total, link->m_input ? "ok" : "—", link->m_write ? "ok" : "—", link->m_command ? "ok" : "—",
                  link->m_vibration ? "ok" : "—", link->m_respBasic ? "ok" : "—", link->m_respExt ? "ok" : "—");
    L(found);
    if (!link->m_input || !link->m_write) {
        error = tr("Controller characteristics not found", "Características do controle não encontradas");
        link->Close();
        return nullptr;
    }

    // joycon2cpp subscribes to the command-response channels before init.
    link->EnableNotify(link->m_respBasic, link->m_respBasicToken, "basic response", log);
    link->EnableNotify(link->m_respExt, link->m_respExtToken, "extended response", log);

    try {
        link->m_device.RequestPreferredConnectionParameters(
            BluetoothLEPreferredConnectionParameters::ThroughputOptimized());
    } catch (...) {
        L("  preferred connection parameters: not supported (ignored)");
    }
    return link;
}

Switch2Link::~Switch2Link() { Close(); }

void Switch2Link::EnableNotify(const GattCharacteristic& ch, winrt::event_token& token, const char* name, const LogFn& log) {
    if (!ch) return;
    try {
        // A handler must be attached for Windows to keep delivering notifications.
        token = ch.ValueChanged([](GattCharacteristic const&, GattValueChangedEventArgs const&) {});
        const auto st = ch.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        if (log && st != GattCommunicationStatus::Success)
            log(std::string("  notify ") + name + ": " + StatusName(st));
    } catch (const winrt::hresult_error& e) {
        if (log) log(std::string("  notify ") + name + ": exception " + Hr(e));
    } catch (...) {}
}

bool Switch2Link::WriteRaw(const GattCharacteristic& member, const std::vector<uint8_t>& bytes, bool waitResult) {
    GattCharacteristic ch{nullptr};
    {
        // Close() clears the members under the same lock.
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_closed.load() || !member) return false;
        ch = member;
    }
    try {
        DataWriter w;
        w.WriteBytes(bytes);
        auto op = ch.WriteValueAsync(w.DetachBuffer(), GattWriteOption::WriteWithoutResponse);
        if (waitResult) return op.get() == GattCommunicationStatus::Success;
        return true;
    } catch (...) {
        return false;
    }
}

bool Switch2Link::SendGenericCommand(const Packet& cmd) {
    if (!IsCommandAllowed(cmd)) return false;   // hard safety net
    return WriteRaw(m_write, cmd, true);
}

void Switch2Link::SendInit() {
    if (!m_command) return;
    const int prefix = InitPrefixSize(m_kind);
    for (const auto& cmd : InitSequence(m_kind)) {
        if (m_closed.load()) return;
        if (!IsCommandAllowed(cmd)) continue;   // never send anything off the allow-list
        std::vector<uint8_t> pkt(prefix, 0x00);
        pkt.insert(pkt.end(), cmd.begin(), cmd.end());
        WriteRaw(m_command, pkt, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
}

bool Switch2Link::Start(InputHandler onInput, StatusHandler onStatus, const LogFn& log) {
    std::weak_ptr<Switch2Link> weak = weak_from_this();
    try {
        m_inputToken = m_input.ValueChanged(
            [weak, onInput](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
                auto self = weak.lock();
                if (!self || self->m_closed.load()) return;
                auto rdr = DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> buf(rdr.UnconsumedBufferLength());
                rdr.ReadBytes(buf);
                self->m_packets.fetch_add(1);
                onInput(buf);
            });
        m_statusToken = m_device.ConnectionStatusChanged(
            [weak, onStatus](BluetoothLEDevice const& dev, IInspectable const&) {
                auto self = weak.lock();
                if (!self || self->m_closed.load()) return;
                onStatus(dev.ConnectionStatus() == BluetoothConnectionStatus::Connected);
            });
    } catch (const winrt::hresult_error& e) {
        if (log) log("  subscribe input: exception " + Hr(e));
        return false;
    } catch (...) {
        return false;
    }
    try {
        const auto st = m_input.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        if (log) log(std::string("  input notify: ") + StatusName(st));
    } catch (const winrt::hresult_error& e) {
        if (log) log("  input notify: exception " + Hr(e));
    } catch (...) {}
    return true;   // the caller decides by whether packets actually arrive
}

void Switch2Link::KeepAlive() {
    try {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_closed.load() || !m_device) return;
        if (!m_session) m_session = GattSession::FromDeviceIdAsync(m_device.BluetoothDeviceId()).get();
        if (m_session) m_session.MaintainConnection(true);
    } catch (...) {}
}

void Switch2Link::SetPlayerLeds(uint8_t pattern) {
    auto self = shared_from_this();
    std::thread([self, pattern] { self->SendGenericCommand(PlayerLedCommand(pattern)); }).detach();
}

void Switch2Link::Identify() {
    auto self = shared_from_this();
    std::thread([self] {
        // Sample 0x04 is the short "chime" the original app used as feedback.
        self->SendGenericCommand(VibrationSampleCommand(0x04));
    }).detach();
}

bool Switch2Link::Rumble(float amp) {
    const bool active = amp >= 0.01f;
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_closed.load()) return false;
    if (!active && !m_rumbleActive) return true;   // already silent

    GattCharacteristic ch{nullptr};
    Packet pkt;
    switch (m_kind) {
        case ControllerKind::ProController: ch = m_command;   pkt = ProRumblePacket(amp, m_rumbleCounter); break;
        case ControllerKind::GameCube:      ch = m_vibration; pkt = GameCubeRumblePacket(amp); break;
        default:                            ch = m_vibration; pkt = JoyConRumblePacket(amp, m_rumbleCounter); break;
    }
    if (!ch) return true;
    if (m_rumbleBusy->exchange(true)) return false;  // previous write still in flight
    try {
        DataWriter w;
        w.WriteBytes(pkt);
        auto busy = m_rumbleBusy;
        ch.WriteValueAsync(w.DetachBuffer(), GattWriteOption::WriteWithoutResponse)
            .Completed([busy](auto const&, AsyncStatus) { busy->store(false); });
    } catch (...) {
        m_rumbleBusy->store(false);
        return false;
    }
    if (active) m_rumbleCounter = (m_rumbleCounter + 1) & 0x0F;
    m_rumbleActive = active;   // a "stop" is only considered delivered once written
    return true;
}

void Switch2Link::Close() {
    if (m_closed.exchange(true)) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    try { if (m_input && m_inputToken) m_input.ValueChanged(m_inputToken); } catch (...) {}
    try { if (m_respBasic && m_respBasicToken) m_respBasic.ValueChanged(m_respBasicToken); } catch (...) {}
    try { if (m_respExt && m_respExtToken) m_respExt.ValueChanged(m_respExtToken); } catch (...) {}
    try { if (m_device && m_statusToken) m_device.ConnectionStatusChanged(m_statusToken); } catch (...) {}
    try { if (m_session) { m_session.MaintainConnection(false); m_session.Close(); } } catch (...) {}
    try { if (m_device) m_device.Close(); } catch (...) {}
    m_session = nullptr;
    m_input = m_write = m_command = m_vibration = m_respBasic = m_respExt = nullptr;
    m_device = nullptr;
}

} // namespace sw2
