#include "core/module.hpp"
#include <stdexcept>
#include <mutex>
#include <vector>
namespace iris::module {
namespace { HMODULE image{}; }
void attach(HMODULE instance) noexcept { image = instance; }
HMODULE base() noexcept { return image; }
bool owns(const void* address) noexcept {
    if (!image) return false;
    const auto* bytes = reinterpret_cast<const BYTE*>(image);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(bytes + dos->e_lfanew);
    const auto pointer = reinterpret_cast<ULONG_PTR>(address);
    const auto start = reinterpret_cast<ULONG_PTR>(image);
    return pointer >= start && pointer - start < nt->OptionalHeader.SizeOfImage;
}
bool pin() noexcept {
    HMODULE owner{};
    const auto address = reinterpret_cast<LPCWSTR>(&pin);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           address, &owner)) return true;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                             address, &owner) != FALSE;
}
void migrate_data(){
    static std::once_flag migrated;
    std::call_once(migrated,[]{
        const auto destination=data_folder();
        std::vector<std::filesystem::path> sources{folder()};
        wchar_t local[32768]{};const auto count=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        if(count && count<32768)sources.push_back(std::filesystem::path(local)/L"Iris");
        for(const auto& source:sources){
            if(source==destination)continue;
            for(const auto* name:{L"configs",L"inventory-configs",L"assets",L"logs"}){
                std::error_code ec;const auto root=source/name;
                if(!std::filesystem::is_directory(root,ec) || std::filesystem::is_symlink(root,ec))continue;
                for(const auto& item:std::filesystem::recursive_directory_iterator(root,std::filesystem::directory_options::skip_permission_denied)){
                    if(!item.is_regular_file() || item.is_symlink())continue;
                    const auto target=destination/name/item.path().lexically_relative(root);
                    std::filesystem::create_directories(target.parent_path());
                    std::filesystem::copy_file(item.path(),target,std::filesystem::copy_options::skip_existing);
                }
            }
            for(const auto* name:{L"menu-settings.json",L"bindings.json"}){
                std::error_code ec;if(std::filesystem::is_regular_file(source/name,ec)){
                    std::filesystem::create_directories(destination);std::filesystem::copy_file(source/name,destination/name,std::filesystem::copy_options::skip_existing);
                }
            }
        }
    });
}
std::filesystem::path data_folder_for(const std::filesystem::path& executable) {
    // CS2 mounts game/csgo as its resource root; precipitation assets also live here.
    const auto content=executable.parent_path().parent_path().parent_path()/L"csgo";
    if(executable.filename()==L"cs2.exe" && std::filesystem::is_directory(content))return content/L"iris";
    return executable.parent_path()/L"iris";
}
std::filesystem::path data_folder() {
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    if(!length || length>=std::size(path))throw std::runtime_error("Cannot resolve host executable directory");
    return data_folder_for(path);
}
std::filesystem::path folder() {
    wchar_t path[32768]{};
    HMODULE owner = image;
    if (!owner) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(&folder), &owner);
    if (owner) {
        const auto length = GetModuleFileNameW(owner, path, static_cast<DWORD>(std::size(path)));
        if (length && length < std::size(path)) return std::filesystem::path(path).parent_path();
    }
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) throw std::runtime_error("Cannot resolve Iris data directory");
    return std::filesystem::path(path) / L"Iris";
}
}
