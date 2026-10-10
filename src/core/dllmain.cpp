#include <Windows.h>
#include <cstdio>
#include "core/module.hpp"
#include "core/log.hpp"
#include "core/crash.hpp"
#include "core/hooks/dx11.hpp"
#include "core/startup_status.hpp"
#include "core/sdk/binding_config.hpp"
#include "core/sdk/client.hpp"
#include "core/features/inventory/inventory.hpp"

namespace {
DWORD WINAPI initialize(void*) noexcept {
    iris::initialize_logging();
#ifdef IRIS_DEBUG
    iris::overlay_log("Iris x64 Debug, built " __DATE__ " " __TIME__);
    char image[128]{};
    std::snprintf(image, sizeof(image), "Module base=%p; CRT initialized; bootstrap thread running.", iris::module::base());
    iris::overlay_log(image);
#else
    iris::overlay_log("Iris x64 Release, built " __DATE__ " " __TIME__);
#endif
    iris::crash::initialize();
    const bool game=iris::startup::is_game();
    if(game){
        try{
            for(std::size_t i=0;i<iris::sdk::binding_config::modules.size();++i){
                const auto module=GetModuleHandleW(iris::sdk::binding_config::modules[i]);
                if(!module)throw std::runtime_error("CS2 runtime was not detected. CS2 will close automatically.");
                if(iris::sdk::sha256_file(module)!=iris::sdk::binding_config::hashes[i])
                    throw std::runtime_error(iris::sdk::binding_config::updated_message);
            }
            const auto source=iris::sdk::binding_config::load(iris::module::folder()/L"bindings.json");
            iris::overlay_log(source==iris::sdk::binding_config::Source::built_in ?
                "Bindings: using the fingerprint-gated built-in registry." :
                "Bindings: using validated external bindings.json.");
            iris::cosmetics::start();
            const auto status=iris::cosmetics::status();
            if(!status.available)throw std::runtime_error("Iris runtime initialization failed: "+status.detail);
        }catch(const std::exception& error){
            iris::overlay_log(error.what());
            iris::startup::fail(error.what());
            return 1;
        }catch(...){
            iris::startup::fail("Iris runtime initialization failed. CS2 will close automatically.");
            return 1;
        }
    }
    const auto result = iris::overlay::start();
    if(game){
        if(result)iris::startup::fail("Iris rendering initialization failed. CS2 will close automatically.");
        else iris::startup::report(2,"Iris initialized successfully.");
    }
    iris::overlay_log(result == 0 ? "Initialization completed; waiting for rendering callbacks." : "Initialization failed; consult the preceding diagnostics.");
    return result;
}
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    iris::module::attach(instance);
    DisableThreadLibraryCalls(instance);
    const HANDLE thread = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
    if (!thread) return FALSE;
    CloseHandle(thread);
    return TRUE;
}
