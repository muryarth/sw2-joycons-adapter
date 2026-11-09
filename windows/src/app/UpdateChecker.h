#pragma once
// Checks GitHub for a newer release once per launch (background thread,
// short timeouts, never blocks the UI). Only the public releases API is
// contacted; the request carries a generic User-Agent and no user data.

#include <memory>
#include <mutex>
#include <string>

namespace sw2 {

struct UpdateInfo {
    enum class State { Idle, Checking, UpToDate, Available, Failed };
    State       state = State::Idle;
    std::string latest;   // e.g. "0.4.0"
    std::string url;      // release page
    std::string error;    // short reason when Failed
};

class UpdateChecker {
public:
    void Start();          // safe to call again ("check again")
    UpdateInfo Get() const;

    // a > b for "1.2.3"-style versions (a leading 'v' is ignored).
    static bool IsNewer(const std::string& a, const std::string& b);

private:
    struct Shared {
        std::mutex mutex;
        UpdateInfo info;
    };
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
};

} // namespace sw2
