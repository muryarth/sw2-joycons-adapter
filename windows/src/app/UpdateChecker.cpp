#include "app/UpdateChecker.h"
#include "app/Version.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <winhttp.h>

#include <cstring>
#include <sstream>
#include <thread>
#include <vector>

namespace sw2 {

namespace {

std::vector<int> ParseVersion(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<int> parts;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, '.')) {
        int n = 0;
        for (char c : item) { if (c < '0' || c > '9') break; n = n * 10 + (c - '0'); }
        parts.push_back(n);
    }
    while (parts.size() < 3) parts.push_back(0);
    return parts;
}

// GET https://api.github.com/<path>. Returns HTTP status (0 = network error).
int HttpsGet(const std::wstring& host, const std::wstring& path, std::string& body) {
    int status = 0;
    const std::wstring agent = L"SW2-Adapter/" + std::wstring(APP_VERSION, APP_VERSION + strlen(APP_VERSION));
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return 0;
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 4000);
    HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (req) {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (WinHttpSendRequest(req, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            status = int(code);
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < 512 * 1024) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
                body.append(chunk.data(), read);
            }
        }
        WinHttpCloseHandle(req);
    }
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return status;
}

} // namespace

bool UpdateChecker::IsNewer(const std::string& a, const std::string& b) {
    return ParseVersion(a) > ParseVersion(b);
}

UpdateInfo UpdateChecker::Get() const {
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    return m_shared->info;
}

void UpdateChecker::Start() {
    {
        std::lock_guard<std::mutex> lk(m_shared->mutex);
        if (m_shared->info.state == UpdateInfo::State::Checking) return;
        m_shared->info = UpdateInfo{};
        m_shared->info.state = UpdateInfo::State::Checking;
    }
    // Detached with shared state: exiting the app never waits on the network.
    auto shared = m_shared;
    std::thread([shared] {
        UpdateInfo result;
        std::string body;
        const std::string path = std::string("/repos/") + GITHUB_OWNER + "/" + GITHUB_REPO + "/releases/latest";
        const int status = HttpsGet(L"api.github.com", std::wstring(path.begin(), path.end()), body);
        if (status == 200) {
            try {
                const auto j = nlohmann::json::parse(body);
                std::string tag = j.value("tag_name", std::string());
                if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
                result.latest = tag;
                result.url = j.value("html_url", std::string(RELEASES_URL));
                result.state = (!tag.empty() && IsNewer(tag, APP_VERSION)) ? UpdateInfo::State::Available
                                                                           : UpdateInfo::State::UpToDate;
            } catch (...) {
                result.state = UpdateInfo::State::Failed;
                result.error = "invalid response";
            }
        } else {
            result.state = UpdateInfo::State::Failed;
            result.error = status == 0   ? "offline"
                         : status == 404 ? "no public release yet"
                         : status == 403 ? "rate limited"
                                         : "HTTP " + std::to_string(status);
        }
        std::lock_guard<std::mutex> lk(shared->mutex);
        shared->info = result;
    }).detach();
}

} // namespace sw2
