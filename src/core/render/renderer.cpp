#include "core/features/player/esp/pipeline.hpp"
#include <d3d11.h>
#include "core/hooks/dx11.hpp"
#include "core/render/renderer.hpp"
#include "core/thread_state.hpp"
#include <array>
#include "core/features/world/render.hpp"
#include "core/features/world/esp.hpp"
#include "core/features/combat/nospread.hpp"
#include "core/features/combat/targeting.hpp"
#include "core/features/player/visuals.hpp"
#include "core/log.hpp"
#include "core/menu/menu.hpp"
#include "core/menu/theme.hpp"
#include "core/menu/settings_io.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/image_cache.hpp"
#include "core/menu/backdrop.hpp"
#include "core/menu/apply_queue.hpp"
#include "core/menu/feature_bindings.hpp"
#include <imgui_internal.h>
#include "core/menu/inventory_navigation.hpp"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <atomic>
#include <stdexcept>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
iris::ThreadState<std::array<char, 512>> errors;
char* last_error() noexcept {
    try { return errors.get().data(); }
    catch (...) { static char fallback[512]{}; return fallback; }
}
ImGuiContext* gui{};
HWND host_window{};
DWORD owner_thread{};
bool win32_ready{}, dx11_ready{};
std::atomic<bool> capture_mouse{}, capture_keyboard{};
std::atomic<bool> depth_requested{};
bool menu_visible{true};
std::atomic<bool> popup_open{},binding_active{},inventory_back_active{};
std::atomic<bool> published_visible{true};
bool inventory_escape_consumed{};
#ifdef INVENTORY_UI
std::atomic<int> toggle_key{VK_DELETE},close_key{VK_ESCAPE};
#else
std::atomic<int> toggle_key{VK_HOME},close_key{VK_ESCAPE};
#endif
iris::menu::State menu;
std::unique_ptr<iris::EnvironmentRender> environment_renderer;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> environment_context;
struct ContextScope {
    ImGuiContext* previous{ImGui::GetCurrentContext()};
    explicit ContextScope(ImGuiContext* context) { ImGui::SetCurrentContext(context); }
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
bool fail(const char* message) noexcept {
    std::snprintf(last_error(), 512, "%s", message);
    return false;
}
void cleanup() noexcept {
    iris::ui::backdrop::shutdown();
    environment_renderer.reset();environment_context.Reset();
    menu.apply_queue.reset();
    menu.images.reset();
    menu.config_io.reset();
    menu.settings_io.reset();
    binding_active=false;
    inventory_back_active=false;
    inventory_escape_consumed=false;
    iris::cosmetics::stop();
    if (dx11_ready) { ImGui_ImplDX11_Shutdown(); dx11_ready = false; }
    if (win32_ready) { ImGui_ImplWin32_Shutdown(); win32_ready = false; }
    if (gui) { ImGui::DestroyContext(gui); gui = nullptr; }
    host_window = nullptr;
    owner_thread = 0;
    capture_mouse = false;
    capture_keyboard = false;
    popup_open = false;
}
bool check_thread() noexcept {
    if (!gui) return fail("Renderer is not initialized");
    if (owner_thread != GetCurrentThreadId()) return fail("Call must run on the initializing thread");
    return true;
}
}

const char* iris::renderer::last_error_message() noexcept { return last_error(); }
bool iris::renderer::initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context) noexcept {
    iris::overlay_log("Renderer startup: validating window, device and immediate context.");
    last_error()[0] = '\0';
    if (gui) return fail("Renderer is already initialized");
    if (!IsWindow(window) || !device || !context) return fail("Valid HWND, device and immediate context are required");
    DWORD window_process{};
    GetWindowThreadProcessId(window, &window_process);
    if (window_process != GetCurrentProcessId()) return fail("Window must belong to this process");
    Microsoft::WRL::ComPtr<ID3D11Device> context_device;
    context->GetDevice(context_device.GetAddressOf());
    if (context_device.Get() != device || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return fail("Context must be the supplied device's immediate context");
    ContextScope scope(nullptr);
    try {
        IMGUI_CHECKVERSION();
        iris::overlay_log("Renderer startup: creating ImGui context.");
        gui = ImGui::CreateContext();
        if (!gui) throw std::runtime_error("ImGui context creation failed");
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        iris::overlay_log("Renderer startup: initializing menu and fonts.");
        iris::menu::initialize(::menu);
        ::menu.trace = iris::overlay_log;
#ifdef INVENTORY_UI
        ::menu.scale = std::max(.75f, GetDpiForWindow(window) / 96.f);
        iris::ui::theme::apply(::menu.settings.accent, ::menu.scale);
        ::toggle_key.store(::menu.menu_toggle_key);
        ::close_key.store(::menu.menu_close_key);
#endif
        iris::overlay_log("Renderer startup: starting artwork worker and environment renderer.");
        ::menu.images=std::make_shared<iris::ui::ImageCache>(device,true);
        iris::cosmetics::start();
        environment_context=context;
        environment_renderer=std::make_unique<iris::EnvironmentRender>(device);
        menu_visible = true;
        iris::overlay_log("Renderer startup: initializing Win32 backend.");
        win32_ready = ImGui_ImplWin32_Init(window);
        if (!win32_ready) throw std::runtime_error("Win32 backend initialization failed");
        iris::overlay_log("Renderer startup: initializing DX11 backend and device objects.");
        dx11_ready = ImGui_ImplDX11_Init(device, context);
        if (!dx11_ready || !ImGui_ImplDX11_CreateDeviceObjects())
            throw std::runtime_error("DX11 backend initialization failed");
        iris::overlay_log("Renderer startup: initializing backdrop.");
        iris::ui::backdrop::initialize(device,context,true,iris::overlay_log);
        host_window = window;
        owner_thread = GetCurrentThreadId();
        return true;
    } catch (const std::exception& error) { cleanup(); return fail(error.what()); }
      catch (...) { cleanup(); return fail("Unknown initialization error"); }
}
bool iris::renderer::render() noexcept {
    last_error()[0] = '\0';
    if (!check_thread()) return false;
    ContextScope scope(gui);
    try {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        iris::ui::tick_settings(::menu, GetDpiForWindow(host_window) / 96.f);
        iris::ui::tick_bindings(::menu);
        iris::ui::tick_configs(::menu);
        ImGui::NewFrame();
        iris::ui::evaluate_bindings(::menu);
        if (menu_visible) iris::menu::render(::menu);
        ::popup_open.store(menu_visible && ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel));
        ::binding_active.store(menu_visible && ::menu.key_capture);
        ::inventory_back_active.store(menu_visible && iris::ui::inventory_handles_escape(::menu));
#ifdef INVENTORY_UI
        ::toggle_key.store(::menu.menu_toggle_key);
        ::close_key.store(::menu.menu_close_key);
#endif
        if(!menu_visible && ::menu.images)::menu.images->pump();
        iris::visuals::esp::viewport(ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y);
        const auto lookup=[](void* owner,int definition){
            auto* cache=static_cast<iris::ui::ImageCache*>(owner);if(!cache)return iris::visuals::Icon{};
            const auto art=cache->art(definition,0);
            return iris::visuals::Icon{art.texture.GetTexID(),art.uv0.x,art.uv0.y,art.uv1.x,art.uv1.y,art.aspect};
        };
        bool fov_overlay=iris::visuals::draw(lookup,::menu.images.get());
        DWORD world_foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&world_foreground);
        if(!menu_visible && world_foreground==GetCurrentProcessId())iris::world::esp::draw(lookup,::menu.images.get());
        const auto aiming=iris::nospread::aim_settings.load();
        const auto legit=aiming?aiming->resolve(iris::nospread::legit_weapon_definition.load()):iris::combat::LegitSettings{};
        const bool legit_fov=legit.enabled && legit.visualize_fov;
        DWORD foreground_pid{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
        if(!menu_visible && !iris::nospread::stopped && foreground_pid==GetCurrentProcessId() && iris::nospread::player_alive && legit_fov) {
            const auto size=ImGui::GetIO().DisplaySize;
            const auto radius=iris::nospread::fov_radius(legit.effective_fov(iris::nospread::camera_fov.load()),iris::nospread::camera_fov.load(),size.y);
            if(radius>0 && radius<std::hypot(size.x,size.y)*.5f) {
                auto* draw=ImGui::GetBackgroundDrawList();
                const ImVec2 center{size.x*.5f,size.y*.5f};
                draw->AddCircle(center,radius,IM_COL32(0,0,0,180),128,3.f);
                draw->AddCircle(center,radius,ImGui::ColorConvertFloat4ToU32(iris::ui::theme::accent),128,1.f);
                fov_overlay=true;
            }
        }
        const auto& hud=::menu.values.cosmetics.hud;
        const auto hud_state=iris::nospread::hud_snapshot.load();
        if(!menu_visible && !iris::nospread::stopped && foreground_pid==GetCurrentProcessId() && iris::nospread::player_alive && hud_state && GetTickCount64()-hud_state->sampled<=150) {
            const auto size=ImGui::GetIO().DisplaySize;const ImVec2 center{size.x*.5f,size.y*.5f};auto* draw=ImGui::GetBackgroundDrawList();
            const auto scope_color=ImGui::ColorConvertFloat4ToU32({hud.scope_color[0],hud.scope_color[1],hud.scope_color[2],hud.scope_color[3]});
            if(hud.scope_lines&&hud_state->scoped){
                const float g=hud.scope_gap*.5f,t=hud.scope_thickness;
                const float horizontal=hud.scope_length==0?std::max(0.f,size.x*.5f-g):hud.scope_length;
                const float vertical=hud.scope_length==0?std::max(0.f,size.y*.5f-g):hud.scope_length;
                draw->AddLine({center.x-g-horizontal,center.y},{center.x-g,center.y},scope_color,t);draw->AddLine({center.x+g,center.y},{center.x+g+horizontal,center.y},scope_color,t);
                draw->AddLine({center.x,center.y-g-vertical},{center.x,center.y-g},scope_color,t);draw->AddLine({center.x,center.y+g},{center.x,center.y+g+vertical},scope_color,t);fov_overlay=true;
            }
            if(hud.crosshair_mode==1&&hud_state->scoped){const auto color=ImGui::ColorConvertFloat4ToU32({hud.crosshair_color[0],hud.crosshair_color[1],hud.crosshair_color[2],hud.crosshair_color[3]});constexpr float gap=4,length=8;draw->AddLine({center.x-gap-length,center.y},{center.x-gap,center.y},color,1.5f);draw->AddLine({center.x+gap,center.y},{center.x+gap+length,center.y},color,1.5f);draw->AddLine({center.x,center.y-gap-length},{center.x,center.y-gap},color,1.5f);draw->AddLine({center.x,center.y+gap},{center.x,center.y+gap+length},color,1.5f);fov_overlay=true;}
            if(hud.inaccuracy_overlay){const float fov=hud_state->fov;const float accuracy=hud_state->accuracy;if(fov>1&&accuracy>0){const float radius=accuracy/std::tan(fov*0.00872664626f)*size.y*.5f;if(std::isfinite(radius)&&radius>0&&radius<std::hypot(size.x,size.y)*.5f){draw->AddCircle(center,radius,IM_COL32(255,255,255,170),64,1.5f);fov_overlay=true;}}}
        }
        ImGui::Render();
        if(menu_visible || fov_overlay || ImGui::GetDrawData()->TotalVtxCount>0) {
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
            environment_context->OMGetRenderTargets(1,target.GetAddressOf(),nullptr);
            if(menu_visible)iris::ui::backdrop::render(target.Get(),ImGui::GetDrawData());
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        capture_mouse = menu_visible && ImGui::GetIO().WantCaptureMouse;
        capture_keyboard = menu_visible && ImGui::GetIO().WantCaptureKeyboard;
        published_visible=menu_visible;
        return true;
    } catch (const std::exception& error) { return fail(error.what()); }
      catch (...) { return fail("Unknown render error"); }
}
LRESULT iris::renderer::handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (!gui) return 0;
    if (!check_thread() || window != host_window) return 0;
    ContextScope scope(gui);
    if (!::binding_active && message == WM_KEYDOWN && wparam == static_cast<WPARAM>(::toggle_key.load())) { ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);return 1; }
    if (!::binding_active && message == WM_KEYUP && wparam == static_cast<WPARAM>(::toggle_key.load())) {
        ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);
        menu_visible = !menu_visible;
        ::menu.focus_menu = menu_visible;
        return 1;
    }
    // UI-only glue: deliver Escape to the picker, and consume its matching key-up.
    if (menu_visible && wparam == VK_ESCAPE &&
        (message == WM_KEYDOWN || message == WM_KEYUP) &&
        (::inventory_back_active.load() || inventory_escape_consumed)) {
        inventory_escape_consumed = message == WM_KEYDOWN;
        try { ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam); }
        catch (...) { fail("Inventory back input failed"); }
        return 1;
    }
    if (!::binding_active && menu_visible && !::popup_open.load() && message == WM_KEYDOWN && wparam == static_cast<WPARAM>(::close_key.load())) { ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);menu_visible = false;capture_mouse=capture_keyboard=false; return 1; }
    try { return ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam); }
    catch (...) { fail("Window message handler failed"); return 0; }
}
bool iris::renderer::shutdown() noexcept {
    last_error()[0] = '\0';
    if (!gui) return true;
    if (!check_thread()) return false;
    ContextScope scope(gui);
    // Preserve another application's context; never restore our destroyed one.
    if (scope.previous == gui) scope.previous = nullptr;
    cleanup();
    return true;
}
namespace iris::renderer {
void environment_frame(IDXGISwapChain* chain,ID3D11Texture2D* depth) noexcept {
    if(!gui || !environment_renderer || !::menu.gameplay_wired || owner_thread!=GetCurrentThreadId())return;
    ContextScope scope(gui);
    const auto& w=::menu.values.world;
    depth_requested=false;
    try {
        environment_renderer->draw(environment_context.Get(),chain,w,static_cast<float>(GetTickCount64()%10000000)/1000.f,depth);

    }
    catch(const std::exception& error){overlay_log(error.what());const bool enabled=::menu.values.world.enabled;::menu.values.world.enabled=false;if(!ui::submit_features(::menu))::menu.values.world.enabled=enabled;}
}

void focus_menu() noexcept {
    if (!gui || owner_thread != GetCurrentThreadId()) return;
    menu_visible = true;
    ::menu.focus_menu = true;
}
bool captures_mouse() noexcept { return capture_mouse; }
bool captures_keyboard() noexcept { return capture_keyboard; }
bool requires_scene_depth() noexcept { return depth_requested; }
int toggle_key() noexcept {return ::toggle_key.load();}
int close_key() noexcept {return ::close_key.load();}
bool menu_open() noexcept {return published_visible.load();}
bool popup_open() noexcept {return ::popup_open.load();}
bool binding_active() noexcept {return ::binding_active.load();}
bool inventory_back_active() noexcept {return ::inventory_back_active.load();}
// Host-consumed menu keys still feed feature hotkeys without toggling visibility twice.
void key_state(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if(!gui || owner_thread!=GetCurrentThreadId() || window!=host_window)return;
    ContextScope scope(gui);ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);
}
bool hidden_frame() noexcept {
    if(!gui || owner_thread!=GetCurrentThreadId())return false;
    if(menu_visible) {
        ContextScope scope(gui);ImGui::ClearActiveID();if(ImGui::GetCurrentContext()->OpenPopupStack.Size)ImGui::ClosePopupToLevel(0,true);
        ImGui::GetIO().WantTextInput=false;::menu.key_capture=false;
    }
    menu_visible=false;return iris::renderer::render();
}
void clear_input() noexcept {
    if (!gui || owner_thread != GetCurrentThreadId()) return;
    ContextScope scope(gui);
    ImGui::GetIO().ClearEventsQueue();
    ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse();
}
}
