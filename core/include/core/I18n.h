#pragma once
// Minimal localisation: every UI string is written inline as an
// English/Portuguese pair, e.g. tr("Connect", "Conectar"). English is the
// default. Logs stay in English (they are for diagnostics).

#include <atomic>

namespace sw2 {

enum class Lang { En = 0, Pt = 1 };

inline std::atomic<int>& LangStorage() { static std::atomic<int> v{0}; return v; }
inline void SetLang(Lang l) { LangStorage() = static_cast<int>(l); }
inline Lang CurrentLang() { return static_cast<Lang>(LangStorage().load()); }
inline const char* tr(const char* en, const char* pt) { return CurrentLang() == Lang::Pt ? pt : en; }

} // namespace sw2
