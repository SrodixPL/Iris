#include "core/menu/thumbnail_source.hpp"
#include "core/features/inventory/inventory.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace iris::ui {
namespace {
struct Source { int definition, paint; const wchar_t* url; const wchar_t* fallback; };
constexpr Source sources[]{
#include "core/sdk/generated/thumbnail_sources.inc"
};
struct Internet {
    HINTERNET value{};
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
bool fetch(const wchar_t* address, std::vector<unsigned char>& bytes, const std::atomic_bool& stopping) {
    if (!address[0] || stopping) return false;
    URL_COMPONENTS url{sizeof(url)};
    url.dwHostNameLength = url.dwUrlPathLength = url.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address, 0, 0, &url) || url.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const std::wstring host(url.lpszHostName, url.dwHostNameLength);
    std::wstring path(url.lpszUrlPath, url.dwUrlPathLength);
    if (url.dwExtraInfoLength) path.append(url.lpszExtraInfo, url.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"Iris thumbnails/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000)) return false;
    Internet connection{WinHttpConnect(session.value, host.c_str(), url.nPort, 0)};
    Internet request{connection.value ? WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (stopping || !request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) ||
        !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return false;
    DWORD status{}, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) || status != 200) return false;
    bytes.clear();
    std::array<unsigned char, 32768> buffer{};
    const auto deadline = GetTickCount64() + 15000;
    for (;;) {
        DWORD count{};
        if (stopping || GetTickCount64() >= deadline || !WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count)) return false;
        if (!count) break;
        if (bytes.size() + count > 4 * 1024 * 1024) return false;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
    }
    constexpr unsigned char signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    return bytes.size() >= sizeof(signature) && std::equal(std::begin(signature), std::end(signature), bytes.begin());
}
}
bool download_thumbnail(int definition, int paint, const std::filesystem::path& destination, const std::atomic_bool& stopping) {
    const auto key = std::pair{definition, paint};
    const auto found = std::lower_bound(std::begin(sources), std::end(sources), key,
        [](const Source& source, const auto& value) { return std::pair{source.definition, source.paint} < value; });
    const bool music=definition==-4;
    const auto* kit=music?cosmetics::find_music_kit(paint):nullptr;
    if (music&&!kit)return false;
    if (!music&&(found == std::end(sources) || found->definition != definition || found->paint != paint)) return false;
    std::vector<unsigned char> bytes;
    if(music) {
        const std::string path="https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/"+std::string(kit->image)+"_png.png";
        const std::wstring url(path.begin(),path.end());
        if(!fetch(url.c_str(),bytes,stopping))return false;
    }else if (!fetch(found->url, bytes, stopping) && !fetch(found->fallback, bytes, stopping)) return false;
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    auto temporary = destination;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.close();
        if (!output) { std::filesystem::remove(temporary, error); return false; }
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error); return false;
    }
    return true;
}
}
