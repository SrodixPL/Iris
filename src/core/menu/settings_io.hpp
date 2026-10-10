#pragma once
#include "core/menu/menu_state.hpp"
#include "core/menu/file_worker.hpp"
#include <optional>
namespace iris::ui {
struct SettingsResult {bool loaded{};MenuSettings settings;std::string error;};
class SettingsIO {
    std::filesystem::path path_;std::mutex mutex_;std::deque<SettingsResult> completed_;
    FileWorker worker_; // Must be destroyed first, draining jobs before the result members.
    void complete(SettingsResult result);
public:
    explicit SettingsIO(std::filesystem::path folder);
    void load(MenuSettings defaults);
    void save(MenuSettings settings);
    std::optional<SettingsResult> poll();
};
std::string encode_settings(const MenuSettings& settings);
MenuSettings decode_settings(const std::string& text,MenuSettings defaults={});
void tick_settings(MenuRuntime& state,float auto_scale);
void save_settings(MenuRuntime& state);
int virtual_key(ImGuiKey key);
ImGuiKey imgui_key(int key);
}
