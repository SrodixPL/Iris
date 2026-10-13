#pragma once
#include <Windows.h>
#include <winhttp.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#pragma comment(lib,"winhttp.lib")

namespace iris::http {
struct Handle {
    HINTERNET value{};
    ~Handle(){if(value)WinHttpCloseHandle(value);}
    Handle(const Handle&)=delete;
    explicit Handle(HINTERNET handle):value(handle){if(!value)throw std::runtime_error("Cannot open artwork HTTPS connection.");}
};
// Synchronous transport belongs on a worker. Stream bounded data to disk;
// never accept HTTP error pages or disable certificate verification.
inline void download(const wchar_t* host,const wchar_t* path,const std::filesystem::path& file,
                     std::uint64_t expected_size,const std::atomic_bool& cancelled) {
    if(cancelled)throw std::runtime_error("Artwork download cancelled.");
    Handle session(WinHttpOpen(L"Iris-Artwork/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!WinHttpSetTimeouts(session.value,5000,5000,5000,5000))throw std::runtime_error("Cannot configure artwork download timeouts.");
    Handle connection(WinHttpConnect(session.value,host,INTERNET_DEFAULT_HTTPS_PORT,0));
    Handle request(WinHttpOpenRequest(connection.value,L"GET",path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)))throw std::runtime_error("Cannot configure artwork redirects.");
    if(!WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.value,nullptr))
        throw std::runtime_error("Artwork HTTPS request failed (Windows error "+std::to_string(GetLastError())+").");
    DWORD status{},bytes=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX)||status!=200)
        throw std::runtime_error("Artwork server returned HTTP "+std::to_string(status)+".");
    std::ofstream output(file,std::ios::binary|std::ios::trunc);
    if(!output)throw std::runtime_error("Cannot create artwork download file.");
    std::array<char,65536> buffer{};std::uint64_t total{};
    const auto started=GetTickCount64();
    for(;;){
        if(cancelled||GetTickCount64()-started>300000)throw std::runtime_error("Artwork download cancelled or timed out.");
        DWORD received{};
        if(!WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(buffer.size()),&received))throw std::runtime_error("Artwork download interrupted.");
        if(!received)break;
        if(total+received>expected_size)throw std::runtime_error("Artwork download exceeds the pinned size.");
        output.write(buffer.data(),received);if(!output)throw std::runtime_error("Cannot write artwork download.");
        total+=received;
    }
    output.flush();
    if(!output||total!=expected_size)throw std::runtime_error("Artwork download is incomplete.");
}
}
