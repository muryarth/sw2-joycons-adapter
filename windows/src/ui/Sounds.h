#pragma once
// Short synthesized UI sounds (no audio files). Each cue maps to one state
// change so it can be recognised without looking at the screen.

namespace sw2 {

enum class Sound {
    Found,         // soft single blip: a new controller is in sync mode
    Connected,     // rising two-note chime
    Reconnected,   // quick rising triple
    Lost,          // distinctive alert: a controller lost its connection mid-game
    Disconnected,  // single falling pop: user disconnected / forgot
    Error,         // low double knock
    Toggle,        // tiny tock: switches and mode changes
    BatteryLow,    // two soft falling notes
    Count
};

class Sounds {
public:
    void SetEnabled(bool on) { m_enabled = on; }
    void SetVolume(float v);
    void Play(Sound s);

private:
    bool  m_enabled = true;
    float m_volume  = 0.6f;
};

} // namespace sw2
