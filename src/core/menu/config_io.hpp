#pragma once
#include "core/menu/menu_state.hpp"
#include "core/menu/file_worker.hpp"
#include <optional>
namespace iris::ui {
enum class ConfigAction {List,Save,Autosave,Load,Create,Rename,Duplicate,Delete,Import,Export,ListDeleted,Restore};
struct ConfigResult {
    ConfigAction action{};std::string name,error,clipboard;
    std::optional<MenuState> state;std::vector<ConfigEntry> entries;
    std::uint64_t hash{};
};
class ConfigIO {
    std::filesystem::path folder_;std::mutex mutex_;std::deque<ConfigResult> completed_;
    FileWorker worker_;
    void complete(ConfigResult result);
public:
    explicit ConfigIO(std::filesystem::path root);
    void request(ConfigAction action,std::string name={},MenuState state={},std::string argument={});
    std::optional<ConfigResult> poll();
};
std::string encode_config(const MenuState& state);
MenuState decode_config(const std::string& text);
bool valid_config_name(const std::string& name);
bool default_config(const std::string& name);
void persist_features(MenuRuntime& state);
void tick_configs(MenuRuntime& state);
void request_load(MenuRuntime& state,const std::string& name);
void save_config(MenuRuntime& state);
void config_overlays(MenuRuntime& state);
bool text_matches(const char* text,const char* query);
}
