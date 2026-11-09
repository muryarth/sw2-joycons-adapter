#include "ui/Sounds.h"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace sw2 {

namespace {

constexpr int RATE = 44100;

// One soft tone. `freqTo` lets the pitch glide (a "pop" drops slightly).
struct Tone {
    float start;     // seconds
    float freq;      // Hz
    float freqTo;    // Hz at the end (== freq for a steady note)
    float dur;       // seconds until it has faded
    float gain;
};

// Original, minimal cues in the spirit of a modern console UI: short,
// rounded, quiet. Sine voices with a whisper of 3rd harmonic, 3 ms attack,
// exponential decay, a gentle low-pass and a faint echo for air.
std::vector<uint8_t> Render(const std::vector<Tone>& tones, float volume) {
    float total = 0.f;
    for (auto& t : tones) total = std::max(total, t.start + t.dur);
    constexpr float echoDelay = 0.085f, echoGain = 0.18f;
    const int samples = int((total + echoDelay + 0.06f) * RATE);
    std::vector<float> dry(samples, 0.f);
    const float twoPi = 6.2831853f;

    for (const auto& t : tones) {
        const int s0 = int(t.start * RATE), len = int(t.dur * RATE);
        double phase = 0.0;
        for (int i = 0; i < len && s0 + i < samples; ++i) {
            const float x = float(i) / len;                       // 0..1 through the note
            const float f = t.freq + (t.freqTo - t.freq) * (1.f - (1.f - x) * (1.f - x));  // ease-out glide
            phase += twoPi * f / RATE;
            const float tSec = float(i) / RATE;
            const float attack = std::min(1.f, tSec / 0.003f);
            const float decay = std::exp(-x * 5.5f);
            const float v = float(std::sin(phase) + 0.06 * std::sin(phase * 3.0));
            dry[s0 + i] += v * attack * decay * t.gain;
        }
    }

    // Faint echo + one-pole low-pass (~4.5 kHz) to keep everything round.
    std::vector<float> wet(samples, 0.f);
    const int d = int(echoDelay * RATE);
    const float a = std::exp(-twoPi * 4500.f / RATE);
    float lp = 0.f;
    for (int i = 0; i < samples; ++i) {
        float s = dry[i] + (i >= d ? dry[i - d] * echoGain : 0.f);
        lp = (1.f - a) * s + a * lp;
        wet[i] = lp;
    }
    // Short fade-out so nothing ever clicks at the end.
    const int fade = std::min(samples, int(0.02f * RATE));
    for (int i = 0; i < fade; ++i) wet[samples - 1 - i] *= float(i) / fade;

    const uint32_t dataBytes = uint32_t(samples * 2);
    std::vector<uint8_t> wav(44 + dataBytes);
    auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&wav[o], &v, 4); };
    auto put16 = [&](size_t o, uint16_t v) { std::memcpy(&wav[o], &v, 2); };
    std::memcpy(&wav[0], "RIFF", 4); put32(4, 36 + dataBytes); std::memcpy(&wav[8], "WAVE", 4);
    std::memcpy(&wav[12], "fmt ", 4); put32(16, 16); put16(20, 1); put16(22, 1);
    put32(24, RATE); put32(28, RATE * 2); put16(32, 2); put16(34, 16);
    std::memcpy(&wav[36], "data", 4); put32(40, dataBytes);
    const float master = 0.20f * volume;   // quiet by design
    for (int i = 0; i < samples; ++i) {
        const int16_t s = int16_t(std::clamp(wet[i] * master, -1.f, 1.f) * 32767.f);
        std::memcpy(&wav[44 + i * 2], &s, 2);
    }
    return wav;
}

std::vector<Tone> Score(Sound s) {
    // Pentatonic palette (C/D/E/G/A) so every cue feels related.
    switch (s) {
        case Sound::Toggle:       // tiny soft "tock"
            return {{0.00f, 1480.f, 1180.f, 0.045f, 0.45f}};
        case Sound::Found:        // gentle rising pop
            return {{0.00f, 880.f, 1046.5f, 0.11f, 0.55f}};
        case Sound::Connected:    // two light notes, upward
            return {{0.00f, 1046.5f, 1046.5f, 0.12f, 0.55f},
                    {0.075f, 1568.f, 1568.f, 0.20f, 0.50f}};
        case Sound::Reconnected:  // quick three-note sparkle
            return {{0.00f, 1046.5f, 1046.5f, 0.09f, 0.45f},
                    {0.055f, 1318.5f, 1318.5f, 0.09f, 0.45f},
                    {0.110f, 1568.f, 1568.f, 0.18f, 0.45f}};
        case Sound::Lost:         // signature: two quick pings, then a falling glide
            // The only cue with a repeated note, so it is recognisable even
            // mid-game; slightly more present than the others.
            return {{0.00f, 1318.5f, 1318.5f, 0.085f, 0.75f},
                    {0.12f, 1318.5f, 1318.5f, 0.085f, 0.75f},
                    {0.26f, 988.f, 659.f, 0.34f, 0.70f}};
        case Sound::BatteryLow:   // two soft notes, downward
            return {{0.00f, 1046.5f, 1046.5f, 0.12f, 0.45f},
                    {0.09f, 784.f, 784.f, 0.22f, 0.40f}};
        case Sound::Disconnected: // single falling pop
            return {{0.00f, 784.f, 622.f, 0.16f, 0.50f}};
        case Sound::Error:        // low, muted double knock
            return {{0.00f, 392.f, 349.f, 0.10f, 0.55f},
                    {0.13f, 392.f, 330.f, 0.14f, 0.50f}};
    }
    return {};
}

std::mutex g_mutex;
// PlaySound(SND_MEMORY | SND_ASYNC) reads the buffer while playing, so the
// rendered sounds must stay alive: one cached buffer per cue.
std::array<std::vector<uint8_t>, static_cast<size_t>(Sound::Count)> g_cache;
float g_cacheVolume = -1.f;

} // namespace

void Sounds::SetVolume(float v) { m_volume = std::clamp(v, 0.f, 1.f); }

void Sounds::Play(Sound s) {
    if (!m_enabled || m_volume <= 0.001f) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (std::abs(g_cacheVolume - m_volume) > 0.001f) {
        // Volume changed: stop playback before replacing the buffers it may use.
        PlaySoundW(nullptr, nullptr, 0);
        for (auto& b : g_cache) b.clear();
        g_cacheVolume = m_volume;
    }
    auto& buf = g_cache[static_cast<size_t>(s)];
    if (buf.empty()) buf = Render(Score(s), m_volume);
    PlaySoundW(reinterpret_cast<LPCWSTR>(buf.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace sw2
