#pragma once
#include <Windows.h>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11Texture2D;
namespace iris::renderer {
const char* last_error_message() noexcept;
bool initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context) noexcept;
bool render() noexcept;
LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
bool shutdown() noexcept;
void environment_frame(IDXGISwapChain* chain,ID3D11Texture2D* depth=nullptr) noexcept;
bool captures_mouse() noexcept;
bool captures_keyboard() noexcept;
bool requires_scene_depth() noexcept;
void clear_input() noexcept;
bool hidden_frame() noexcept;
void key_state(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept;
void focus_menu() noexcept;
int toggle_key() noexcept;
int close_key() noexcept;
bool menu_open() noexcept;
bool popup_open() noexcept;
bool binding_active() noexcept;
// Picker consumes Escape before the menu-close handler.
bool inventory_back_active() noexcept;
}
