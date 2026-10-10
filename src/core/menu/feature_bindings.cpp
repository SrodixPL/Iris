#include "core/menu/feature_bindings.hpp"
#include "core/menu/feature_tree.hpp"
namespace iris::ui {
void commit_feature_edit(MenuRuntime& state,const MenuState& before) {
  if(state_hash(before)==state_hash(state.values))return;
  state.apply_context="Feature";
  if(!submit_features(state))state.values=before;
  state.apply_context.clear();
}
void evaluate_bindings(MenuRuntime& state){feature_tree::evaluate(state);}
}
