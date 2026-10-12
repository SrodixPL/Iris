#pragma once
#include "core/menu/menu_state.hpp"
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
namespace iris::ui {
struct ApplyResult { MenuState before, requested, actual; std::string error, context; bool load{}; };
struct RuntimeSnapshot { cosmetics::Status status; cosmetics::EquippedLoadout roster; std::shared_ptr<const cosmetics::PreviewImage> preview; };
class ApplyQueue {
  struct Job { MenuState before, requested; std::string context; bool load{}; };
  MenuState requested_;
  std::mutex mutex_;
  static constexpr std::size_t capacity=64;
  std::array<std::optional<Job>,capacity> jobs_;
  std::atomic<std::size_t> written_{},read_{};
  std::shared_ptr<void> wake_;
  std::deque<ApplyResult> results_;
  RuntimeSnapshot snapshot_;
  std::atomic<bool> stopping_{};
  std::atomic<bool> refresh_{true};
  std::atomic<std::shared_ptr<const cosmetics::PreviewRequest>> preview_request_;
  std::atomic<std::uint64_t> preview_until_{};
  std::atomic<std::shared_ptr<const cosmetics::PreviewImage>> preview_image_;
  std::shared_ptr<void> preview_wake_;
  std::size_t pending_{};
  std::thread worker_;
  std::thread preview_worker_;
public:
  using Submit=void(*)(const cosmetics::Configuration&);
  using Status=cosmetics::Status(*)();
  using Roster=cosmetics::EquippedLoadout(*)();
  using Await=cosmetics::InventoryReceipt(*)(std::uint64_t,unsigned);
  explicit ApplyQueue(MenuState initial,Submit submit=cosmetics::submit,Status status=cosmetics::status,Roster roster=cosmetics::equipped_loadout,Await await=nullptr);
  ~ApplyQueue();
  bool submit(const MenuState& requested,const std::string& context={},bool load=false);
  std::optional<ApplyResult> poll();
  bool pending() const { return pending_!=0; }
  void refresh();
  void preview(const cosmetics::Selection& selection,const cosmetics::PreviewPose& pose={});
  std::optional<RuntimeSnapshot> snapshot();
};
void publish_apply(MenuState& displayed,const ApplyResult& result);
void tick_bindings(MenuRuntime& state);
void bind_inventory(MenuRuntime& state);
bool set_both_teams(MenuRuntime& state,int team,int slot,bool enabled);
bool apply_inventory(MenuRuntime& state,int team,int slot,const ItemDraft& value,bool both);
bool read_inventory(MenuRuntime& state,int team,int slot,ItemDraft& value);
}
