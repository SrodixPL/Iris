#include "core/hooks/dx11.hpp"
#include "core/module.hpp"
#include "core/thread_state.hpp"
#include "core/render/chams_layer.hpp"
#include <d3d11.h>
#include "core/hooks/inline_hook.hpp"
#include "core/log.hpp"
#include "core/render/renderer.hpp"
#include "core/features/inventory/inventory.hpp"

#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace iris::overlay {
using Microsoft::WRL::ComPtr;
namespace {
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using Resize = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using SetTargets = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
using SetTargetsUav = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
using ClearDepth = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
using DrawIndexed = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DrawVertices = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using DrawIndexedInstances = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using DrawInstances = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using DrawIndirect = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
using DrawAuto = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using SdlWindowQuery = void*(__cdecl*)();
using SdlWindowBool = bool(__cdecl*)(void*, bool);
using SdlWindowGet = bool(__cdecl*)(void*);
using SdlCursor = bool(__cdecl*)();
using SdlReset = void(__cdecl*)();
using SdlFlush = void(__cdecl*)(std::uint32_t,std::uint32_t);
using SdlKeys = const bool*(__cdecl*)(int*);
using SdlMouse = std::uint32_t(__cdecl*)(float*,float*);
using SdlWarp = void(__cdecl*)(void*, float, float);
constexpr UINT cursor_message = WM_APP + 0x4e57;
struct Message { HWND window; UINT message; WPARAM wparam; LPARAM lparam; bool state_only{}; };
struct State {
    std::recursive_mutex render_mutex;
    std::mutex input_mutex;
    std::deque<Message> input;
    std::unordered_map<HWND, WNDPROC> previous_procedures;
    std::atomic<HWND> window{};
    std::atomic<bool> installed{}, stopped{}, visible{true}, mouse{}, keyboard{};
    std::atomic<bool> focus{true}, cursor_captured{}, requested_relative{}, requested_grab{};
    std::atomic<void*> sdl_window{};
    SdlWindowQuery sdl_mouse_focus{};
    SdlWindowBool sdl_relative{}, sdl_grab{};
    SdlWindowGet sdl_get_relative{}, sdl_get_grab{};
    SdlCursor sdl_show{}, sdl_hide{}, sdl_cursor_visible{};
    SdlWarp sdl_warp{};
    SdlReset sdl_reset{},sdl_pump{};
    SdlFlush sdl_flush{};
    std::atomic<float> blocked_mouse_x{},blocked_mouse_y{};
    SdlKeys sdl_keys{};
    SdlMouse sdl_motion{},sdl_buttons{};
    std::array<LPARAM,256> held_keys{};
    bool cursor_was_visible{}, inventory_escape_consumed{};
    std::atomic<WPARAM> escape_consumed{}, close_key_down{};
    std::unique_ptr<hooks::Hook> relative_hook, grab_hook, hide_cursor_hook, warp_hook,keys_hook,motion_hook,buttons_hook,pump_hook;
    std::atomic<std::uint64_t> frame_count{};
    bool initialized{}, error_reported{}, thread_warning{};
    bool hooks_live{};
    DWORD render_thread{};
    IDXGISwapChain* chain{}; // Non-owning: do not prevent the game's swap chain destruction.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11RenderTargetView> target;
    std::unique_ptr<hooks::Runtime> runtime;
    std::unique_ptr<hooks::Hook> present_hook, present1_hook, resize_hook;
    Present original_present{};
    Present1 original_present1{};
    Resize original_resize{};
    std::unique_ptr<hooks::Hook> targets_hook,targets_uav_hook;
    std::unique_ptr<hooks::Hook> clear_depth_hook;
    ClearDepth original_clear_depth{};
    std::array<std::unique_ptr<hooks::Hook>,7> draw_hooks,deferred_draw_hooks;
    DrawIndexed original_indexed{};DrawVertices original_vertices{};
    DrawIndexedInstances original_indexed_instances{};DrawInstances original_instances{};
    DrawIndirect original_indexed_indirect{},original_indirect{};DrawAuto original_auto{};
    DrawIndexed deferred_indexed{};DrawVertices deferred_vertices{};
    DrawIndexedInstances deferred_indexed_instances{};DrawInstances deferred_instances{};
    DrawIndirect deferred_indexed_indirect{},deferred_indirect{};DrawAuto deferred_auto{};
    SetTargets original_targets{};
    SetTargetsUav original_targets_uav{};
    std::mutex depth_mutex;
    std::atomic<ID3D11DeviceContext*> depth_context{};
    ComPtr<ID3D11Texture2D> scene_depth;
    ComPtr<ID3D11Texture2D> depth_snapshot;
    unsigned depth_clear_count{};
    bool depth_snapshot_ready{};
    std::atomic<UINT> depth_width{},depth_height{};
};
// Hooks and window procedures remain forwarding until process exit. Pinning
// the DLL keeps every callback/trampoline valid. No hook teardown in DllMain.
std::atomic<State*> active{};
std::mutex startup_mutex;
ThreadValue inside_present;
void STDMETHODCALLTYPE indexed_detour(ID3D11DeviceContext* context,UINT count,UINT start,INT base){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_indexed?s.deferred_indexed:s.original_indexed;draw(context,count,start,base);render::chams_layer::layer.capture(context,[&]{draw(context,count,start,base);});}
void STDMETHODCALLTYPE vertices_detour(ID3D11DeviceContext* context,UINT count,UINT start){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_vertices?s.deferred_vertices:s.original_vertices;draw(context,count,start);render::chams_layer::layer.capture(context,[&]{draw(context,count,start);});}
void STDMETHODCALLTYPE indexed_instances_detour(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,INT base,UINT first){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_indexed_instances?s.deferred_indexed_instances:s.original_indexed_instances;draw(context,count,instances,start,base,first);render::chams_layer::layer.capture(context,[&]{draw(context,count,instances,start,base,first);});}
void STDMETHODCALLTYPE instances_detour(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,UINT first){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_instances?s.deferred_instances:s.original_instances;draw(context,count,instances,start,first);render::chams_layer::layer.capture(context,[&]{draw(context,count,instances,start,first);});}
void STDMETHODCALLTYPE indexed_indirect_detour(ID3D11DeviceContext* context,ID3D11Buffer* buffer,UINT offset){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_indexed_indirect?s.deferred_indexed_indirect:s.original_indexed_indirect;draw(context,buffer,offset);render::chams_layer::layer.capture(context,[&]{draw(context,buffer,offset);});}
void STDMETHODCALLTYPE indirect_detour(ID3D11DeviceContext* context,ID3D11Buffer* buffer,UINT offset){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_indirect?s.deferred_indirect:s.original_indirect;draw(context,buffer,offset);render::chams_layer::layer.capture(context,[&]{draw(context,buffer,offset);});}
void STDMETHODCALLTYPE auto_detour(ID3D11DeviceContext* context){auto& s=*active.load();const auto draw=context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&s.deferred_auto?s.deferred_auto:s.original_auto;draw(context);render::chams_layer::layer.capture(context,[&]{draw(context);});}
void bind_draw_hooks(State& state){
    auto** table=*reinterpret_cast<void***>(state.context.Get());
    ComPtr<ID3D11DeviceContext> deferred;state.device->CreateDeferredContext(0,&deferred);
    auto** deferred_table=deferred?*reinterpret_cast<void***>(deferred.Get()):nullptr;
    const auto bind=[&](unsigned index,unsigned slot,void* callback,auto& original,auto& deferred_original){
        auto& hook=state.draw_hooks[index];hook=std::make_unique<hooks::Hook>(*state.runtime,table[slot],callback);original=hook->template original<std::remove_reference_t<decltype(original)>>();
        if(deferred_table){
            if(deferred_table[slot]==table[slot])deferred_original=original;
            else {auto& copy=state.deferred_draw_hooks[index];copy=std::make_unique<hooks::Hook>(*state.runtime,deferred_table[slot],callback);deferred_original=copy->template original<std::remove_reference_t<decltype(deferred_original)>>();}
        }
        hook->enable();if(state.deferred_draw_hooks[index])state.deferred_draw_hooks[index]->enable();
    };
    bind(0,12,reinterpret_cast<void*>(&indexed_detour),state.original_indexed,state.deferred_indexed);
    bind(1,13,reinterpret_cast<void*>(&vertices_detour),state.original_vertices,state.deferred_vertices);
    bind(2,20,reinterpret_cast<void*>(&indexed_instances_detour),state.original_indexed_instances,state.deferred_indexed_instances);
    bind(3,21,reinterpret_cast<void*>(&instances_detour),state.original_instances,state.deferred_instances);
    bind(4,39,reinterpret_cast<void*>(&indexed_indirect_detour),state.original_indexed_indirect,state.deferred_indexed_indirect);
    bind(5,40,reinterpret_cast<void*>(&indirect_detour),state.original_indirect,state.deferred_indirect);
    bind(6,38,reinterpret_cast<void*>(&auto_detour),state.original_auto,state.deferred_auto);
}
void remember_depth(State& state,ID3D11DeviceContext* context,ID3D11DepthStencilView* view) {
    if(!view || inside_present || !renderer::requires_scene_depth() || context!=state.depth_context.load())return;
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(desc.Width!=state.depth_width || desc.Height!=state.depth_height || desc.ArraySize!=1)return;
    // Keep the scene prepass depth; later viewmodel passes may use a separate
    // texture with the same dimensions.
    std::lock_guard lock(state.depth_mutex);if(!state.scene_depth)state.scene_depth=texture;
}
void STDMETHODCALLTYPE targets_detour(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets,ID3D11DepthStencilView* depth) {
    auto& state=*active.load();remember_depth(state,context,depth);state.original_targets(context,count,targets,depth);
}
void STDMETHODCALLTYPE targets_uav_detour(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets,ID3D11DepthStencilView* depth,UINT start,UINT uav_count,ID3D11UnorderedAccessView* const* uavs,const UINT* counts) {
    auto& state=*active.load();remember_depth(state,context,depth);state.original_targets_uav(context,count,targets,depth,start,uav_count,uavs,counts);
}
void STDMETHODCALLTYPE clear_depth_detour(ID3D11DeviceContext* context,ID3D11DepthStencilView* depth,UINT flags,FLOAT value,UINT8 stencil) {
    auto& state=*active.load();
    if((flags&D3D11_CLEAR_DEPTH) && depth && !inside_present && context==state.depth_context && renderer::requires_scene_depth()){
        remember_depth(state,context,depth);
        ComPtr<ID3D11Resource> resource;depth->GetResource(&resource);
        std::lock_guard depth_lock(state.depth_mutex);
        if(resource.Get()==state.scene_depth.Get()){
            ++state.depth_clear_count;
            // The first clear starts the scene. Preserve rendered geometry
            // before the engine's subsequent clear destroys it for Present.
            if(state.depth_clear_count>=2 && !state.depth_snapshot_ready){
                D3D11_TEXTURE2D_DESC desc{},cached{};state.scene_depth->GetDesc(&desc);
                if(state.depth_snapshot)state.depth_snapshot->GetDesc(&cached);
                if(!state.depth_snapshot || cached.Width!=desc.Width || cached.Height!=desc.Height || cached.Format!=desc.Format || cached.SampleDesc.Count!=desc.SampleDesc.Count){
                    state.depth_snapshot.Reset();desc.BindFlags=desc.CPUAccessFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
                    state.device->CreateTexture2D(&desc,nullptr,&state.depth_snapshot);
                }
                if(state.depth_snapshot){context->CopyResource(state.depth_snapshot.Get(),state.scene_depth.Get());state.depth_snapshot_ready=true;}
            }
        }
    }
    state.original_clear_depth(context,depth,flags,value,stencil);
}
bool input_blocked(const State& state){return state.visible.load()&&!state.stopped.load();}
const bool* __cdecl keys_detour(int* count){
    auto& state=*active.load();int size{};const auto* keys=state.sdl_keys(&size);if(count)*count=size;
    static const std::array<bool,512> released{};
    return input_blocked(state)&&size>=0&&size<=static_cast<int>(released.size())?released.data():keys;
}
std::uint32_t __cdecl motion_detour(float* x,float* y){
    auto& state=*active.load();const auto buttons=state.sdl_motion(x,y);
    if(input_blocked(state)){if(x)*x=0;if(y)*y=0;return 0;}return buttons;
}
std::uint32_t __cdecl buttons_detour(float* x,float* y){
    auto& state=*active.load();float actual_x{},actual_y{};const auto buttons=state.sdl_buttons(&actual_x,&actual_y);
    if(input_blocked(state)){if(x)*x=state.blocked_mouse_x;if(y)*y=state.blocked_mouse_y;return 0;}
    state.blocked_mouse_x=actual_x;state.blocked_mouse_y=actual_y;if(x)*x=actual_x;if(y)*y=actual_y;return buttons;
}
void flush_game_input(State& state){
    if(state.sdl_flush){state.sdl_flush(0x300,0x303);state.sdl_flush(0x400,0x403);}
}
void __cdecl pump_detour(){
    auto& state=*active.load();state.sdl_pump();
    // WndProc has already copied menu input. Do not let queued SDL motion from
    // focus recovery reach the game's independent event-based input path.
    if(input_blocked(state))flush_game_input(state);
}
void release_game_keys(State& state,HWND window,WNDPROC previous){
    if(state.sdl_reset)state.sdl_reset();
    flush_game_input(state);
    for(unsigned key=0;key<state.held_keys.size();++key)if(state.held_keys[key]){
        if(previous)CallWindowProcW(previous,window,(state.held_keys[key]&(LPARAM{1}<<29))?WM_SYSKEYUP:WM_KEYUP,key,state.held_keys[key]|(LPARAM{3}<<30));state.held_keys[key]=0;
    }
    if(previous){CallWindowProcW(previous,window,WM_LBUTTONUP,0,0);CallWindowProcW(previous,window,WM_RBUTTONUP,0,0);CallWindowProcW(previous,window,WM_MBUTTONUP,0,0);CallWindowProcW(previous,window,WM_XBUTTONUP,MAKEWPARAM(0,XBUTTON1),0);CallWindowProcW(previous,window,WM_XBUTTONUP,MAKEWPARAM(0,XBUTTON2),0);}
}
bool __cdecl relative_detour(void* window, bool enabled) {
    auto& state = *active.load();
    if (window == state.sdl_window.load() && state.cursor_captured) {
        state.requested_relative = enabled;
        return state.sdl_relative(window, false);
    }
    return state.sdl_relative(window, enabled);
}
bool __cdecl grab_detour(void* window, bool enabled) {
    auto& state = *active.load();
    if (window == state.sdl_window.load() && state.cursor_captured) {
        state.requested_grab = enabled;
        return state.sdl_grab(window, false);
    }
    return state.sdl_grab(window, enabled);
}
bool __cdecl hide_cursor_detour() {
    auto& state = *active.load();
    return state.cursor_captured ? state.sdl_show() : state.sdl_hide();
}
void __cdecl warp_detour(void* window, float x, float y) {
    auto& state = *active.load();
    if (state.visible && !state.stopped && window == state.sdl_window.load()) return;
    state.sdl_warp(window, x, y);
}
// Runs in WndProc, on SDL's window/main thread. Never change SDL mouse mode
// from Present; the render and window threads can differ.
void update_cursor(State& state, HWND window, bool lost_focus = false) {
    const bool capture = !lost_focus && state.visible && !state.stopped && GetForegroundWindow() == window;
    if (!state.sdl_mouse_focus) return;
    // Keep absolute mode throughout an open-menu focus cycle. Restoring
    // relative mode on blur lets SDL/game focus recovery recenter the cursor.
    if (lost_focus && state.visible && !state.stopped && state.cursor_captured) {
        if (auto* sdl = state.sdl_window.load()) {
            state.sdl_relative(sdl, false);
            state.sdl_grab(sdl, false);
        }
        return;
    }
    if (capture) {
        auto* sdl = state.sdl_mouse_focus();
        if(!sdl)sdl=state.sdl_window.load();
        if (!sdl) return;
        if (!state.cursor_captured) {
            if(state.sdl_reset)state.sdl_reset();
            state.sdl_window = sdl;
            state.requested_relative = state.sdl_get_relative(sdl);
            state.requested_grab = state.sdl_get_grab(sdl);
            state.cursor_was_visible = state.sdl_cursor_visible();
            state.cursor_captured = true;
            if(state.sdl_buttons){float x{},y{};state.sdl_buttons(&x,&y);state.blocked_mouse_x=x;state.blocked_mouse_y=y;}
            POINT pointer{}; GetCursorPos(&pointer);
            state.sdl_relative(sdl, false);
            state.sdl_grab(sdl, false);
            POINT after{}; GetCursorPos(&after);
            if (after.x != pointer.x || after.y != pointer.y) SetCursorPos(pointer.x, pointer.y);
            ReleaseCapture();
            ClipCursor(nullptr);

        }
        state.sdl_relative(sdl,false);state.sdl_grab(sdl,false);
        ReleaseCapture();ClipCursor(nullptr);flush_game_input(state);
        state.sdl_show();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    } else if (state.cursor_captured.exchange(false)) {
        auto* sdl = state.sdl_window.exchange(nullptr);
        if (sdl) {
            state.sdl_grab(sdl, state.requested_grab);
            state.sdl_relative(sdl, state.requested_relative);
            if (state.cursor_was_visible) state.sdl_show(); else state.sdl_hide();
        }
    }
}

bool mouse_message(UINT message) {
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_MOUSELEAVE ||
           message == WM_NCMOUSEMOVE || message == WM_NCMOUSELEAVE;
}
bool key_message(UINT message) { return message >= WM_KEYFIRST && message <= WM_KEYLAST; }
bool queued_message(UINT message) {
    return mouse_message(message) || key_message(message) || message == WM_SETFOCUS ||
           message == WM_KILLFOCUS || message == WM_INPUTLANGCHANGE;
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* state = active.load();
    if (!state) return DefWindowProcW(window, message, wparam, lparam);
    WNDPROC previous{};
    try {
        {
            std::lock_guard lock(state->input_mutex);
            const auto found = state->previous_procedures.find(window);
            if (found != state->previous_procedures.end()) previous = found->second;
        }
        if (message == cursor_message) { update_cursor(*state, window); return 0; }
        if (message == WM_KILLFOCUS) update_cursor(*state, window, true);
        if (message == WM_SETFOCUS) PostMessageW(window, cursor_message, 0, 0);
        if(message==WM_MOUSEACTIVATE && input_blocked(*state))return MA_ACTIVATEANDEAT;
        if (window == state->window.load() && !state->stopped.load()) {
            if(message==WM_KEYDOWN && wparam==static_cast<WPARAM>(renderer::close_key()))state->close_key_down=wparam;
            if(message==WM_KEYUP && wparam==state->close_key_down.load())state->close_key_down=0;
            const auto queue_key=[&] {
                std::lock_guard lock(state->input_mutex);if(state->input.size()>=2048)state->input.pop_front();
                state->input.push_back({window,message,wparam,lparam,true});
            };
            if (!renderer::binding_active() && message == WM_KEYDOWN && (wparam == static_cast<WPARAM>(renderer::toggle_key()) || wparam == VK_END)) {queue_key();return 0;}
            if (!renderer::binding_active() && message == WM_KEYUP && wparam == static_cast<WPARAM>(renderer::toggle_key())) {
                queue_key();state->visible = !state->visible.load();
                state->focus = state->visible.load();
                state->mouse = state->keyboard = state->visible.load();
                if(state->visible)release_game_keys(*state,window,previous);
                update_cursor(*state, window);
                overlay_log(state->visible ? "Menu shown (keybind)." : "Menu hidden (keybind).");
                return 0;
            }
            if (message == WM_KEYDOWN && wparam == VK_ESCAPE && state->visible &&
                renderer::inventory_back_active()) state->inventory_escape_consumed = true;
            const bool picker_escape = wparam == VK_ESCAPE &&
                (renderer::inventory_back_active() || state->inventory_escape_consumed);
            if (message == WM_KEYDOWN && state->escape_consumed && wparam == state->escape_consumed.load()) {queue_key();return 0;}
            if (message == WM_KEYDOWN && wparam == static_cast<WPARAM>(renderer::close_key()) && state->visible && !renderer::binding_active() && !renderer::popup_open() &&
                !picker_escape) {
                queue_key();state->escape_consumed = wparam;
                state->focus=false;
                state->visible = false;
                state->mouse = state->keyboard = false;
                update_cursor(*state, window);
                overlay_log("Menu hidden (Escape).");
                return 0;
            }
            if (message == WM_KEYUP && state->escape_consumed && wparam == state->escape_consumed.load()) { queue_key();state->escape_consumed = 0; return 0; }
            if (message == WM_KEYUP && wparam == VK_ESCAPE) state->inventory_escape_consumed = false;
            if (message == WM_SETCURSOR && state->visible && LOWORD(lparam) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return TRUE; }
            if (!renderer::binding_active() && message == WM_KEYUP && wparam == VK_END) {
                cosmetics::stop();
                state->stopped = true;
                state->visible = false;
                update_cursor(*state, window);
                overlay_log("Stop requested (End); renderer cleanup on next Present.");
                return 0;
            }
            if (queued_message(message)) {
                std::lock_guard lock(state->input_mutex);
                // Queue only scalar messages. WM_INPUT's handle is not retained.
                if (state->input.size() >= 2048) state->input.pop_front();
                state->input.push_back({window, message, wparam, lparam});
            }
        }
    } catch (...) { overlay_log("Input queue failed; forwarding window message."); }
    if (window == state->window.load() && !state->stopped && state->visible) {
        if (message == WM_INPUT) {
            // DefWindowProc performs the required foreground raw-input cleanup.
            return DefWindowProcW(window, message, wparam, lparam);
        }
        if (mouse_message(message) || key_message(message)) return 0;
    }
    if(wparam<state->held_keys.size()){
        if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN)state->held_keys[wparam]=lparam;
        if(message==WM_KEYUP||message==WM_SYSKEYUP)state->held_keys[wparam]=0;
    }
    const auto result = previous ? CallWindowProcW(previous, window, message, wparam, lparam)
                                 : DefWindowProcW(window, message, wparam, lparam);
    if(message==WM_SETFOCUS && input_blocked(*state)){release_game_keys(*state,window,previous);update_cursor(*state,window);}
    if (message == WM_NCDESTROY) {
        std::lock_guard lock(state->input_mutex);
        state->previous_procedures.erase(window);
        if (state->window.load() == window) state->window = nullptr;
    }
    return result;
}
bool attach_window(State& state, HWND window) {
    {
        std::lock_guard lock(state.input_mutex);
        if (state.previous_procedures.contains(window)) { state.window = window; return true; }
        const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (!previous) return false;
        state.previous_procedures.emplace(window, previous);
    }
    SetLastError(0);
    const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_proc));
    if (!previous && GetLastError() != 0) {
        std::lock_guard lock(state.input_mutex);
        state.previous_procedures.erase(window);
        overlay_log("Window input hook failed.");
        return false;
    }
    {
        std::lock_guard lock(state.input_mutex);
        state.previous_procedures[window] = reinterpret_cast<WNDPROC>(previous);
        state.input.clear();
        state.window = window;
    }
    PostMessageW(window, cursor_message, 0, 0);
    overlay_log("Window input forwarding installed; configured menu keys toggle/close, End stops drawing.");
    return true;
}
void release_renderer(State& state) {
    render::chams_layer::layer.reset();
    state.depth_context=nullptr;
    {std::lock_guard depth_lock(state.depth_mutex);state.scene_depth.Reset();state.depth_snapshot.Reset();state.depth_clear_count=0;state.depth_snapshot_ready=false;}
    if (state.initialized) {
        if (GetCurrentThreadId() != state.render_thread) return;
        if (!renderer::shutdown()) { overlay_log(renderer::last_error_message()); return; }
    }
    state.initialized = false;
    state.target.Reset();
    state.context.Reset();
    state.device.Reset();
    state.chain = nullptr;
    state.mouse = state.keyboard = false;
    state.render_thread = 0;
    state.thread_warning = false;
}
bool create_target(State& state, IDXGISwapChain* chain) {
    ComPtr<ID3D11Texture2D> buffer;
    return SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()))) &&
           SUCCEEDED(state.device->CreateRenderTargetView(buffer.Get(), nullptr, state.target.GetAddressOf()));
}
void draw(State& state, IDXGISwapChain* chain, UINT flags) {
    if (!state.installed || (flags & DXGI_PRESENT_TEST)) return;
    std::lock_guard lock(state.render_mutex);
    if (state.stopped) {
        if (state.initialized && GetCurrentThreadId() == state.render_thread) {
            release_renderer(state);
            overlay_log("Renderer stopped. Hooks remain forwarding; DLL stays loaded until process exit.");
        }
        return;
    }
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(chain->GetDesc(&description)) || !IsWindow(description.OutputWindow)) return;
    DWORD process{};
    GetWindowThreadProcessId(description.OutputWindow, &process);
    if (process != GetCurrentProcessId()) return;
    // Once selected, ignore other windows (video/tool swap chains).
    if (state.window.load() && state.window.load() != description.OutputWindow) return;
    if (state.initialized && GetCurrentThreadId() != state.render_thread) {
        if (!state.thread_warning) {
            overlay_log("Present arrived on a different thread; overlay drawing is suspended on that thread.");
            state.thread_warning = true;
        }
        return;
    }
    if (state.initialized && state.chain != chain) release_renderer(state);
    if (!state.initialized) {
        if (FAILED(chain->GetDevice(IID_PPV_ARGS(state.device.ReleaseAndGetAddressOf())))) return;
        state.device->GetImmediateContext(state.context.ReleaseAndGetAddressOf());
        if (!state.context || !create_target(state, chain)) return;
        if (!attach_window(state, description.OutputWindow)) { state.target.Reset(); return; }
        if (!renderer::initialize(description.OutputWindow, state.device.Get(), state.context.Get())) {
            if (!state.error_reported) { overlay_log(renderer::last_error_message()); state.error_reported = true; }
            state.target.Reset();
            return;
        }
        state.initialized = true;
        state.chain = chain;
        state.render_thread = GetCurrentThreadId();
        state.depth_context=state.context.Get();
        if(GetModuleHandleW(L"client.dll") && !state.targets_hook){
            auto** table=*reinterpret_cast<void***>(state.context.Get());
            state.targets_hook=std::make_unique<hooks::Hook>(*state.runtime,table[33],reinterpret_cast<void*>(&targets_detour));
            state.targets_uav_hook=std::make_unique<hooks::Hook>(*state.runtime,table[34],reinterpret_cast<void*>(&targets_uav_detour));
            state.clear_depth_hook=std::make_unique<hooks::Hook>(*state.runtime,table[53],reinterpret_cast<void*>(&clear_depth_detour));
            state.original_targets=state.targets_hook->original<SetTargets>();
            state.original_targets_uav=state.targets_uav_hook->original<SetTargetsUav>();
            state.original_clear_depth=state.clear_depth_hook->original<ClearDepth>();
            bind_draw_hooks(state);
            state.targets_hook->enable();state.targets_uav_hook->enable();
            state.clear_depth_hook->enable();
        }
        state.error_reported = false;
        overlay_log("ImGui initialized on the game's DX11 Present thread.");
    }
    if (!state.target && !create_target(state, chain)) return;
    std::deque<Message> messages;
    {
        std::lock_guard input_lock(state.input_mutex);
        messages.swap(state.input);
    }
    ComPtr<ID3D11Texture2D> scene_depth;
    {
        std::lock_guard depth_lock(state.depth_mutex);
        if(state.depth_snapshot_ready)scene_depth=state.depth_snapshot;
        else if(state.depth_clear_count==0)scene_depth=state.scene_depth;
        state.scene_depth.Reset();state.depth_clear_count=0;state.depth_snapshot_ready=false;
    }
    ComPtr<ID3D11Texture2D> back;
    if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&back)))){
        D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
        state.depth_width=desc.Width;state.depth_height=desc.Height;
        render::chams_layer::layer.configure(state.context.Get(),desc.Width,desc.Height);
    }
    renderer::environment_frame(chain,scene_depth.Get());
    for (const auto& message : messages) {
        if(message.state_only)renderer::key_state(message.window,message.message,message.wparam,message.lparam);
        else renderer::handle_message(message.window,message.message,message.wparam,message.lparam);
    }
    const bool visible=state.visible.load();
    if (!visible) state.mouse = state.keyboard = false;
    if (visible && state.focus.exchange(false)) renderer::focus_menu();
    // ImGui's DX11 backend restores its pipeline state. Preserve OM targets too.
    std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> saved;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> raw{};
    ComPtr<ID3D11DepthStencilView> depth;
    state.context->OMGetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), depth.GetAddressOf());
    for (std::size_t i = 0; i < raw.size(); ++i) saved[i].Attach(raw[i]);
    state.context->OMSetRenderTargets(1, state.target.GetAddressOf(), nullptr);
    const bool rendered = visible ? renderer::render() : renderer::hidden_frame();
    state.context->OMSetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), depth.Get());
    if (rendered) {
        if(!renderer::menu_open() && !state.focus && state.visible.exchange(false)) {
            // Keep a renderer-side close from leaking its key-up into the game.
            state.escape_consumed=state.close_key_down.load();
            if(state.close_key_down.load()!=state.escape_consumed.load())state.escape_consumed=0;
            state.mouse=state.keyboard=false;
            PostMessageW(state.window.load(),cursor_message,0,0);
        }
        state.mouse = renderer::captures_mouse();
        state.keyboard = renderer::captures_keyboard();
        if (state.frame_count.fetch_add(1) == 0) overlay_log("First ImGui frame rendered successfully.");
    } else if (!state.error_reported) { overlay_log(renderer::last_error_message()); state.error_reported = true; }
}
void device_result(State& state, HRESULT result) {
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        std::lock_guard lock(state.render_mutex);
        release_renderer(state);
        overlay_log("DX11 device lost; waiting for a replacement swap chain.");
    }
}
HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain* chain, UINT sync, UINT flags) {
    State& state = *active.load();
    if (inside_present) return state.original_present(chain, sync, flags);
    inside_present = true;
    try { draw(state, chain, flags); } catch (...) { overlay_log("Present overlay failed; forwarding game frame."); }
    const HRESULT result = state.original_present(chain, sync, flags);
    device_result(state, result);
    inside_present = false;
    return result;
}
HRESULT STDMETHODCALLTYPE present1_detour(IDXGISwapChain1* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) {
    State& state = *active.load();
    if (inside_present) return state.original_present1(chain, sync, flags, parameters);
    inside_present = true;
    try { draw(state, chain, flags); } catch (...) { overlay_log("Present1 overlay failed; forwarding game frame."); }
    const HRESULT result = state.original_present1(chain, sync, flags, parameters);
    device_result(state, result);
    inside_present = false;
    return result;
}
HRESULT STDMETHODCALLTYPE resize_detour(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    State& state = *active.load();
    std::lock_guard lock(state.render_mutex);
    {std::lock_guard depth_lock(state.depth_mutex);state.scene_depth.Reset();state.depth_snapshot.Reset();state.depth_clear_count=0;state.depth_snapshot_ready=false;}
    state.depth_width=state.depth_height=0;
    if (state.chain == chain) { state.target.Reset(); overlay_log("ResizeBuffers: released overlay back-buffer view."); }
    const auto result = state.original_resize(chain, count, width, height, format, flags);
    if (FAILED(result) && state.chain == chain) overlay_log("ResizeBuffers failed; render target will be retried on Present.");
    return result;
}
struct ProbeWindow {
    HINSTANCE instance{GetModuleHandleW(nullptr)};
    HWND window{};
    ProbeWindow() {
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = instance;
        cls.lpszClassName = L"IrisDX11Probe";
        if (!RegisterClassW(&cls)) throw std::runtime_error("Cannot register DX11 probe window");
        window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                                 nullptr, nullptr, instance, nullptr);
        if (!window) { UnregisterClassW(cls.lpszClassName, instance); throw std::runtime_error("Cannot create DX11 probe window"); }
    }
    ~ProbeWindow() { DestroyWindow(window); UnregisterClassW(L"IrisDX11Probe", instance); }
};
void discover(State& state) {
    ProbeWindow probe;
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2; description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; description.OutputWindow = probe.window;
    description.SampleDesc.Count = 1; description.Windowed = TRUE;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &description, chain.GetAddressOf(), device.GetAddressOf(), nullptr, context.GetAddressOf());
    if (FAILED(result)) {
        chain.Reset(); device.Reset(); context.Reset();
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &description, chain.GetAddressOf(), device.GetAddressOf(), nullptr, context.GetAddressOf());
    }
    if (FAILED(result)) throw std::runtime_error("DX11 probe device creation failed");
    auto** table = *reinterpret_cast<void***>(chain.Get());
    state.runtime = std::make_unique<hooks::Runtime>();
    if (const auto sdl = GetModuleHandleW(L"SDL3.dll")) {
        state.sdl_reset=reinterpret_cast<SdlReset>(GetProcAddress(sdl,"SDL_ResetKeyboard"));
        state.sdl_flush=reinterpret_cast<SdlFlush>(GetProcAddress(sdl,"SDL_FlushEvents"));
        const auto bind_input=[&](const char* name,void* callback,auto& original,auto& hook){if(auto* target=GetProcAddress(sdl,name)){hook=std::make_unique<hooks::Hook>(*state.runtime,target,callback);original=hook->template original<std::remove_reference_t<decltype(original)>>();}};
        if(state.sdl_flush)bind_input("SDL_PumpEvents",reinterpret_cast<void*>(&pump_detour),state.sdl_pump,state.pump_hook);
        bind_input("SDL_GetKeyboardState",reinterpret_cast<void*>(&keys_detour),state.sdl_keys,state.keys_hook);
        bind_input("SDL_GetRelativeMouseState",reinterpret_cast<void*>(&motion_detour),state.sdl_motion,state.motion_hook);
        bind_input("SDL_GetMouseState",reinterpret_cast<void*>(&buttons_detour),state.sdl_buttons,state.buttons_hook);
        state.sdl_mouse_focus = reinterpret_cast<SdlWindowQuery>(GetProcAddress(sdl, "SDL_GetMouseFocus"));
        state.sdl_get_relative = reinterpret_cast<SdlWindowGet>(GetProcAddress(sdl, "SDL_GetWindowRelativeMouseMode"));
        state.sdl_get_grab = reinterpret_cast<SdlWindowGet>(GetProcAddress(sdl, "SDL_GetWindowMouseGrab"));
        state.sdl_cursor_visible = reinterpret_cast<SdlCursor>(GetProcAddress(sdl, "SDL_CursorVisible"));
        state.sdl_show = reinterpret_cast<SdlCursor>(GetProcAddress(sdl, "SDL_ShowCursor"));
        state.sdl_warp = reinterpret_cast<SdlWarp>(GetProcAddress(sdl, "SDL_WarpMouseInWindow"));
        auto* relative = GetProcAddress(sdl, "SDL_SetWindowRelativeMouseMode");
        auto* grab = GetProcAddress(sdl, "SDL_SetWindowMouseGrab");
        auto* hide = GetProcAddress(sdl, "SDL_HideCursor");
        if (state.sdl_mouse_focus && state.sdl_get_relative && state.sdl_get_grab && state.sdl_cursor_visible && state.sdl_show && state.sdl_warp && relative && grab && hide) {
            state.warp_hook = std::make_unique<hooks::Hook>(*state.runtime, reinterpret_cast<void*>(state.sdl_warp), reinterpret_cast<void*>(&warp_detour));
            state.sdl_warp = state.warp_hook->original<SdlWarp>();
            state.relative_hook = std::make_unique<hooks::Hook>(*state.runtime, reinterpret_cast<void*>(relative), reinterpret_cast<void*>(&relative_detour));
            state.sdl_relative = state.relative_hook->original<SdlWindowBool>();
            state.grab_hook = std::make_unique<hooks::Hook>(*state.runtime, reinterpret_cast<void*>(grab), reinterpret_cast<void*>(&grab_detour));
            state.sdl_grab = state.grab_hook->original<SdlWindowBool>();
            state.hide_cursor_hook = std::make_unique<hooks::Hook>(*state.runtime, reinterpret_cast<void*>(hide), reinterpret_cast<void*>(&hide_cursor_detour));
            state.sdl_hide = state.hide_cursor_hook->original<SdlCursor>();
        } else { state.sdl_mouse_focus = nullptr; overlay_log("SDL cursor integration unavailable."); }
    }
    state.present_hook = std::make_unique<hooks::Hook>(*state.runtime, table[8], reinterpret_cast<void*>(&present_detour));
    state.original_present = state.present_hook->original<Present>();
    state.resize_hook = std::make_unique<hooks::Hook>(*state.runtime, table[13], reinterpret_cast<void*>(&resize_detour));
    state.original_resize = state.resize_hook->original<Resize>();
    ComPtr<IDXGISwapChain1> chain1;
    if (SUCCEEDED(chain.As(&chain1))) {
        auto** table1 = *reinterpret_cast<void***>(chain1.Get());
        // Some implementations share an entry; never hook the same address twice.
        if (table1[22] != table[8]) {
            state.present1_hook = std::make_unique<hooks::Hook>(*state.runtime, table1[22], reinterpret_cast<void*>(&present1_detour));
            state.original_present1 = state.present1_hook->original<Present1>();
        }
    }
    // All originals exist before any hook is enabled; probe is destroyed first.
}
}
DWORD start() noexcept {
    std::unique_lock lock(startup_mutex, std::defer_lock);
    try {
        lock.lock();
        State* state = active.load();
        if (!state) { state = new State; active.store(state); }
        if (state->installed) {
            state->stopped = false; state->visible = true;
            state->focus = true;
            if (state->window) PostMessageW(state->window, cursor_message, 0, 0);
            overlay_log("Overlay already installed; drawing enabled.");
            return 0;
        }
        overlay_log("Starting DX11 overlay bootstrap outside DllMain.");
        if (!state->runtime) discover(*state);
        if (!module::pin()) throw std::runtime_error("Cannot retain overlay callbacks");
        state->hooks_live = true; // Keep callbacks/trampolines valid on partial enable failure.
        if(state->pump_hook)state->pump_hook->enable();
        if(state->keys_hook)state->keys_hook->enable();
        if(state->motion_hook)state->motion_hook->enable();
        if(state->buttons_hook)state->buttons_hook->enable();
        if (state->warp_hook) state->warp_hook->enable();
        if (state->relative_hook) state->relative_hook->enable();
        if (state->grab_hook) state->grab_hook->enable();
        if (state->hide_cursor_hook) state->hide_cursor_hook->enable();
        state->resize_hook->enable();
        if (state->present1_hook) state->present1_hook->enable();
        state->present_hook->enable();
        state->installed = true;
        overlay_log("DX11 Present/Present1/ResizeBuffers hooks installed. Waiting for a game frame.");
        return 0;
    } catch (const std::exception& error) { overlay_log(error.what()); }
      catch (...) { overlay_log("Unknown DX11 bootstrap failure."); }
    // Failed creation is removable. Partially enabled callbacks stay pinned and
    // forward only; a later Start retries enabling without replacing trampolines.
    if (auto* state = active.load()) {
        if (!state->hooks_live) {
            state->present_hook.reset(); state->present1_hook.reset(); state->resize_hook.reset(); state->runtime.reset();
            state->warp_hook.reset(); state->relative_hook.reset(); state->grab_hook.reset(); state->hide_cursor_hook.reset();
            state->pump_hook.reset();state->keys_hook.reset();state->motion_hook.reset();state->buttons_hook.reset();
        }
    }
    return 1;
}
void request_stop() noexcept {
    cosmetics::stop();
    if (auto* state = active.load()) { state->stopped = true; state->visible = false; if (state->window) PostMessageW(state->window, cursor_message, 0, 0); }
    overlay_log("Stop requested; cleanup on next rendered frame.");
}
std::uint64_t frames() noexcept { auto* state = active.load(); return state ? state->frame_count.load() : 0; }
}
