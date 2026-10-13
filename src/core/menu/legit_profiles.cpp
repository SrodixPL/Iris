#include "core/menu/legit_profiles.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/hotkeys.hpp"
#include "core/menu/feature_bindings.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/menu/feature_catalog.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/settings_io.hpp"
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>
#include <imgui_internal.h>
namespace iris::ui::legit_profiles {
namespace {
struct Path {const char* key;const char* path;};
constexpr Path toggle_paths[]{
    {"through_walls","Combat/Legit/Aim assist/Through walls"},{"smoke_check","Combat/Legit/Aim assist/Smoke check"},{"air_check","Combat/Legit/Aim assist/Air check"},{"trigger_smoke","Combat/Legit/Triggerbot/Smoke check"},
    {"enabled","Combat/Legit/Aim assist/Enable"},{"on_fire","Combat/Legit/Aim assist/Only while firing"},
    {"target_lock","Combat/Legit/Aim assist/Keep current target"},{"visualize_fov","Combat/Legit/Aim assist/Draw FOV"},
    {"recoil_control","Combat/Legit/RCS"},{"standalone_recoil","Combat/Legit/RCS/Standalone"},
    {"trigger","Combat/Legit/Triggerbot/Enable"},{"scoped_only","Combat/Legit/Triggerbot/Require sniper scope"},
    {"pause_flashed","Combat/Legit/Aim assist/Pause while flashed"},
    {"confirmation_delay","Combat/Legit/Triggerbot/Shot confirmation"},
    {"only_body","Combat/Legit/Aim assist/Selection/Only body aim"},
    {"prefer_head","Combat/Legit/Aim assist/Selection/Prefer head"},
    {"prefer_body","Combat/Legit/Aim assist/Selection/Prefer body"},
    {"trigger_flash","Combat/Legit/Triggerbot/Flash check"},
    {"trigger_air","Combat/Legit/Triggerbot/Air check"}};
constexpr Path number_paths[]{
    {"assist_strength","Combat/Legit/Aim assist/Assist strength"},{"reaction_max","Combat/Legit/Aim assist/Maximum delay"},{"trigger_reaction_max","Combat/Legit/Triggerbot/Maximum delay"},{"trigger_hit_chance","Combat/Legit/Triggerbot/Hit chance"},
    {"fov","Combat/Legit/Aim assist/Aim/Radius"},{"smoothing","Combat/Legit/Aim assist/Aim/Smoothing"},
    {"max_speed","Combat/Legit/Aim assist/Aim/Turn speed"},{"reaction_time","Combat/Legit/Aim assist/Target acquisition delay"},
    {"recoil_strength","Combat/Legit/RCS/Overall amount"},{"shot_delay","Combat/Legit/Triggerbot/Shot delay"},
    {"sniper_delay","Combat/Legit/Triggerbot/Sniper confirmation delay"},
    {"recoil_horizontal","Combat/Legit/RCS/Horizontal (X)"},{"recoil_vertical","Combat/Legit/RCS/Vertical (Y)"},
    {"trigger_interval","Combat/Legit/Triggerbot/Delay between shots"},
    {"trigger_reaction","Combat/Legit/Triggerbot/Reaction time"},
    {"trigger_minimum_damage","Combat/Legit/Triggerbot/Minimum damage"},
    {"aim_time","Combat/Legit/Aim assist/Aim time"},
    {"smooth_pitch","Combat/Legit/Aim assist/Aim/Smooth pitch"},
    {"smooth_yaw","Combat/Legit/Aim assist/Aim/Smooth yaw"}};
constexpr const char* hitbox_path="Combat/Legit/Aim assist/Accuracy/Hit groups: all / selectable groups";
constexpr const char* trigger_hitbox_path="Combat/Legit/Triggerbot/Hit groups";
constexpr const char* multipoint_path="Combat/Legit/Aim assist/Aim/Multipoint";
constexpr const char* aim_key_path="Combat/Legit/Aim assist/Aim key";
constexpr Path choice_paths[]{
    {"aim_mode","Combat/Legit/Aim assist/Mode"},{"activation","Combat/Legit/Aim assist/Activation"},
    {"trigger_visibility","Combat/Legit/Triggerbot/Visible mode"},
    {"radius_type","Combat/Legit/Aim assist/Aim/Radius type"},
    {"smoothing_type","Combat/Legit/Aim assist/Aim/Smooth type"},
    {"target_selection","Combat/Legit/Aim assist/Selection/Target selection"}};
struct ScopeSelection {MenuRuntime* owner{};std::uint32_t profile{};int category{-1},weapon{};} scope;
combat::LegitSettings* selected_settings(combat::Settings& model){
    auto* set=model.selected_set();if(!set)return nullptr;
    if(scope.weapon)for(auto& item:set->weapons)if(item.definition==scope.weapon&&item.value.enabled)return &item.value.settings;
    if(scope.category>=0&&set->categories[scope.category].enabled)return &set->categories[scope.category].settings;
    scope.category=-1;scope.weapon=0;return &set->global;
}
struct Removal {MenuRuntime* owner{};std::uint32_t profile{};int category{-1},weapon{};std::string label;Bounds anchor{};} removal;
struct HotkeyContext {
    MenuRuntime* owner{};std::uint32_t profile{};int frame{-1};bool loading{};
} hotkey_context;
bool matches(MenuRuntime& state,const char* path,const char* label) {
    return !*state.search||text_matches(path,state.search)||text_matches(label,state.search);
}
void reveal(MenuRuntime& state,const char* path) {
    if(state.search_target!=path)return;
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    if(state.search_reveal_pending&&!ImGui::GetCurrentWindow()->SkipItems){ImGui::SetScrollHereY(.5f);state.search_reveal_pending=false;}
    auto tint=theme::accent;tint.w*=.4f;
    ImGui::GetWindowDrawList()->AddRect(min,max,ImGui::GetColorU32(tint),theme::control_radius*state.scale,0,state.scale);
}
ImGuiID binding(const char* path,int category=-1,int weapon=0) {return hotkeys::binding(binding_path(path,category,weapon));}
ImGuiID selected_binding(const char* path) {return binding(path,scope.category,scope.weapon);}
template<class F> void each_scope(combat::LegitProfileSet& set,F&& visit) {
    visit(set.global,-1,0);
    for(int category=0;category<static_cast<int>(set.categories.size());++category)
        if(set.categories[category].enabled)visit(set.categories[category].settings,category,0);
    for(auto& item:set.weapons)if(item.value.enabled)visit(item.value.settings,-1,item.definition);
}
template<class F> void scope_bindings(int category,int weapon,F&& visit) {
    for(const auto& field:combat::legit_toggles)visit(binding(toggle_path(field.key),category,weapon));
    for(const auto& field:combat::legit_numbers)visit(binding(number_path(field.key),category,weapon));
    for(const auto& field:combat::legit_choices)visit(binding(choice_path(field.key),category,weapon));
    for(const auto* path:{hitbox_path,trigger_hitbox_path,multipoint_path,
         "Combat/Legit/Aim assist/Conditions","Combat/Legit/Triggerbot/Conditions",
         "Combat/Legit/Aim assist/Checks","Combat/Legit/Triggerbot/Checks"})visit(binding(path,category,weapon));
}
template<class F> void each_binding(F&& visit) {
    scope_bindings(-1,0,visit);
    for(int category=0;category<std::size(combat::legit_category_names);++category)scope_bindings(category,0,visit);
    for(const auto& weapon:combat::legit_weapons)scope_bindings(-1,weapon.definition,visit);
}
void reset_scope_hotkeys(int category,int weapon) {
    scope_bindings(category,weapon,[](ImGuiID id){
        auto& option=hotkeys::option(id);option.was_active=false;option.baseline_selection.clear();
        if(option.frame!=ImGui::GetFrameCount())option.frame=-1;
        for(auto& key:option.entries){key.active=false;key.toggled_value=false;}
    });
}
const char* control_label(const char* path,const char* fallback){for(const auto& node:feature_tree::nodes)if(std::string_view(node.path)==path)return node.label;return fallback;}
bool exposed(const char* path){return feature_switches::wired(path);}
bool percent_field(const char* key) {return std::string_view(key)=="recoil_horizontal"||std::string_view(key)=="recoil_vertical";}
bool synchronize_hotkeys(MenuRuntime& state) {
    auto& model=state.values.cosmetics.combat_settings;
    const int observed_frame=hotkeys::option(binding(number_path("fov"))).frame;
    const bool same_owner=hotkey_context.owner==&state;
    // restore() replaces the Option map with unevaluated entries. A load also
    // replaces settings before queue acceptance, including same-ID profiles.
    const bool reloaded=same_owner&&((hotkey_context.frame>=0&&observed_frame<hotkey_context.frame)||(state.startup_loading&&!hotkey_context.loading));
    const bool switched=same_owner&&hotkey_context.profile&&hotkey_context.profile!=model.selected_profile;
    bool changed=false;
    if(switched&&!reloaded){
        const auto previous=std::find_if(model.profile_sets.begin(),model.profile_sets.end(),[](const auto& set){return set.id==hotkey_context.profile;});
        if(previous!=model.profile_sets.end()){
            each_scope(*previous,[&](combat::LegitSettings& values,int category,int weapon){
                const auto binding=[&](const char* path){return legit_profiles::binding(path,category,weapon);};
                for(const auto& field:combat::legit_toggles){if(!exposed(toggle_path(field.key)))continue;
                    auto& option=hotkeys::option(binding(toggle_path(field.key)));
                    const auto held=std::any_of(option.entries.begin(),option.entries.end(),[](const auto& key){return key.mode!=hotkeys::Mode::Toggle&&key.active;});
                    if(!held)continue;
                    const bool value=option.baseline;changed|=values.*field.member!=value;values.*field.member=value;
                }
                for(const auto& field:combat::legit_numbers){if(!exposed(number_path(field.key)))continue;
                    auto& option=hotkeys::option(binding(number_path(field.key)));if(!option.was_active)continue;
                    const float multiplier=percent_field(field.key)?100.f:1.f;
                    const float value=std::clamp(static_cast<float>(option.baseline_number)/multiplier,field.low,field.high);
                    changed|=values.*field.member!=value;values.*field.member=value;
                }
                for(const auto& field:combat::legit_choices){if(!exposed(choice_path(field.key)))continue;auto& option=hotkeys::option(binding(choice_path(field.key)));if(option.was_active){const int value=std::clamp(static_cast<int>(option.baseline_number),0,static_cast<int>(field.names.size())-1);changed|=values.*field.member!=value;values.*field.member=value;}}
                for(const auto& item:{std::pair{hitbox_path,&values.hitboxes},std::pair{trigger_hitbox_path,&values.trigger_hitboxes},std::pair{multipoint_path,&values.multipoint}}){
                    auto& option=hotkeys::option(binding(item.first));
                    if(option.was_active&&option.baseline_selection.size()==item.second->size())for(std::size_t i=0;i<item.second->size();++i){changed|=(*item.second)[i]!=option.baseline_selection[i];(*item.second)[i]=option.baseline_selection[i];}
                }
                for(int trigger=0;trigger<2;++trigger){
                    const auto id=binding(trigger?"Combat/Legit/Triggerbot/Conditions":"Combat/Legit/Aim assist/Conditions");
                    auto& option=hotkeys::option(id);if(!option.was_active||option.baseline_selection.size()!=(trigger?3:2))continue;
                    const auto restore=[&](bool& target,bool baseline){changed|=target!=baseline;target=baseline;};
                    if(trigger){restore(values.trigger_flash,option.baseline_selection[0]);restore(values.trigger_air,option.baseline_selection[1]);restore(values.scoped_only,option.baseline_selection[2]);}
                    else{restore(values.on_fire,option.baseline_selection[0]);restore(values.pause_flashed,option.baseline_selection[1]);}
                }
                for(int trigger=0;trigger<2;++trigger){
                    auto& option=hotkeys::option(binding(trigger?"Combat/Legit/Triggerbot/Checks":"Combat/Legit/Aim assist/Checks"));
                    if(!option.was_active||option.baseline_selection.size()!=(trigger?4:3))continue;
                    const auto restore=[&](bool& target,bool baseline){changed|=target!=baseline;target=baseline;};
                    if(trigger){restore(values.trigger_flash,option.baseline_selection[0]);restore(values.trigger_smoke,option.baseline_selection[1]);restore(values.trigger_air,option.baseline_selection[2]);restore(values.scoped_only,option.baseline_selection[3]);}
                    else{restore(values.pause_flashed,option.baseline_selection[0]);restore(values.smoke_check,option.baseline_selection[1]);restore(values.air_check,option.baseline_selection[2]);}
                }
            });
        }
    }
    if(!same_owner||switched||reloaded){
        const bool transfer_hold=switched&&!reloaded&&model.master_enabled&&!widgets::capturing_hotkey()&&!ImGui::GetIO().WantTextInput;
        const auto clear=[&](ImGuiID id){
            auto& option=hotkeys::option(id);option.was_active=false;option.baseline=false;option.baseline_number=0;option.baseline_selection.clear();
            // Keep an already consumed frame so one Toggle press cannot affect
            // both the old and new sets during a same-frame selection change.
            if(option.frame!=ImGui::GetFrameCount())option.frame=-1;
            for(auto& key:option.entries){key.active=transfer_hold&&key.mode!=hotkeys::Mode::Toggle&&key.key!=ImGuiKey_None&&ImGui::IsKeyDown(key.key);key.toggled_value=false;}
        };
        each_binding(clear);
    }
    hotkey_context.owner=&state;hotkey_context.profile=model.selected_profile;hotkey_context.loading=state.startup_loading;
    hotkey_context.frame=hotkeys::option(binding(number_path("fov"))).frame;
    return changed;
}
void remember_hotkey_frame(){hotkey_context.frame=hotkeys::option(binding(number_path("fov"))).frame;}
bool toggle(MenuRuntime& state,combat::LegitSettings& settings,const char* key,bool disabled) {
    for(const auto& field:combat::legit_toggles)if(std::string_view(field.key)==key){
        const auto* path=toggle_path(key);const auto* label=std::string_view(key)=="recoil_control"?"Enabled":control_label(path,field.label);if(!exposed(path)||!matches(state,path,label))return false;
        ImGui::PushID(path);const bool changed=widgets::toggle("value",label,settings.*field.member,disabled,nullptr,true,true,selected_binding(path));reveal(state,path);ImGui::PopID();return changed;
    }return false;
}
bool number(MenuRuntime& state,combat::LegitSettings& settings,const char* key,bool disabled) {
    for(const auto& field:combat::legit_numbers)if(std::string_view(field.key)==key){
        const auto* path=number_path(key);const auto* label=control_label(path,field.label);if(!exposed(path)||!matches(state,path,label))return false;
        const float multiplier=percent_field(key)?100.f:1.f;float value=(settings.*field.member)*multiplier;
        ImGui::PushID(path);const bool changed=widgets::slider("value",label,value,field.low*multiplier,field.high*multiplier,0,percent_field(key)?"%.0f%%":field.format,disabled,true,true,false,nullptr,selected_binding(path));if(changed)settings.*field.member=value/multiplier;reveal(state,path);ImGui::PopID();return changed;
    }return false;
}
void group_gap(MenuRuntime& state){ImGui::Dummy({0,theme::group_gap*state.scale});}
bool choice(MenuRuntime& state,combat::LegitSettings& settings,const char* key,bool disabled) {
    for(const auto& field:combat::legit_choices)if(std::string_view(field.key)==key){const auto* path=choice_path(key);if(!matches(state,path,field.label))return false;
        std::vector<widgets::Option> options;for(const auto* name:field.names)options.push_back({name});
        ImGui::PushID(path);const bool changed=widgets::combo("value",field.label,settings.*field.member,options,disabled,nullptr,true,true,selected_binding(path));reveal(state,path);ImGui::PopID();return changed;
    }return false;
}
void main_header(MenuRuntime& state,combat::Settings& model,bool& changed) {
    if(widgets::begin_group("legit_profiles","Main")){
        bool master=model.master_enabled;
        constexpr const char* master_path="Combat/Legit/General/Master switch";
        if(widgets::toggle("master","Master switch",master,false,nullptr,true,true,binding(master_path))){model.master_enabled=master;feature_switches::set_toggle(state.values,master_path,master);changed=true;}reveal(state,master_path);
    }widgets::end_group();group_gap(state);
}

}
std::string binding_path(const char* path,int category,int weapon) {
    std::string result="Preview/";result+=path;
    if(weapon)result+="/Weapon/"+std::to_string(weapon);
    else if(category>=0)result+="/Category/"+std::to_string(category);
    return result;
}
bool known_scoped_binding(ImGuiID id) {
    // Build once from the same implemented fields and stable scope identities.
    static const auto ids=[] {
        std::vector<ImGuiID> result;
        const auto add=[&](const char* path){
            if(!exposed(path))return;
            const auto hash=[&](int category,int weapon){const auto name=binding_path(path,category,weapon);result.push_back(ImHashStr(name.c_str(),0,ImHashStr("option bindings")));};
            for(int category=0;category<std::size(combat::legit_category_names);++category)hash(category,0);
            for(const auto& weapon:combat::legit_weapons)hash(-1,weapon.definition);
        };
        for(const auto& field:combat::legit_toggles)add(toggle_path(field.key));
        for(const auto& field:combat::legit_numbers)add(number_path(field.key));
        for(const auto& field:combat::legit_choices)add(choice_path(field.key));
        for(const auto* path:{hitbox_path,trigger_hitbox_path,multipoint_path,"Combat/Legit/Aim assist/Checks","Combat/Legit/Triggerbot/Checks"})add(path);
        std::sort(result.begin(),result.end());return result;
    }();
    return std::binary_search(ids.begin(),ids.end(),id);
}
const char* toggle_path(const char* key) noexcept {for(const auto& item:toggle_paths)if(std::string_view(item.key)==key)return item.path;return "Combat/Legit";}
const char* number_path(const char* key) noexcept {for(const auto& item:number_paths)if(std::string_view(item.key)==key)return item.path;return "Combat/Legit";}
const char* choice_path(const char* key) noexcept {for(const auto& item:choice_paths)if(std::string_view(item.key)==key)return item.path;return "Combat/Legit";}
void selector(MenuRuntime& state,float width){
    const auto scope_item=[](const char* label,bool selected=false){ImGui::PushID(label);const bool clicked=widgets::popup_item(label,selected);ImGui::PopID();return clicked;};
    auto& model=state.values.cosmetics.combat_settings;model.initialize_profiles();auto* set=model.selected_set();if(!set)return;
    if(scope.owner!=&state||scope.profile!=model.selected_profile){scope={&state,model.selected_profile};scope.category=-1;}
    selected_settings(model);
    const char* label="Global";int count=2;
    for(const auto& item:set->categories)count+=item.enabled;
    for(const auto& item:set->weapons)count+=item.value.enabled;
    if(scope.weapon){if(const auto* weapon=combat::legit_weapon(scope.weapon))label=weapon->name;}
    else if(scope.category>=0)label=combat::legit_category_names[scope.category];
    bool add=false,remove=false;
    const auto removable=[&](const char* name,bool selected,int category,int weapon){
        const bool choose=scope_item(name,selected);
        if(ImGui::IsItemClicked(ImGuiMouseButton_Right)){
            removal={&state,model.selected_profile,category,weapon,name,{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}};
            remove=true;ImGui::CloseCurrentPopup();
        }
        return choose;
    };
    if(widgets::begin_dropdown("##legit_scope",label,count,width)){
        if(scope_item("Global",scope.category<0&&!scope.weapon)){scope.category=-1;scope.weapon=0;}
        for(int i=0;i<std::size(combat::legit_category_names);++i)if(set->categories[i].enabled&&removable(combat::legit_category_names[i],scope.category==i&&!scope.weapon,i,0)){scope.category=i;scope.weapon=0;}
        for(const auto& item:set->weapons)if(item.value.enabled)if(const auto* weapon=combat::legit_weapon(item.definition))if(removable(weapon->name,scope.weapon==item.definition,-1,item.definition)){scope.category=-1;scope.weapon=item.definition;}
        add=scope_item("Add...");widgets::end_dropdown();
    }
    const Bounds anchor{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    state.title_bounds=anchor;
    if(add)ImGui::OpenPopup("legit_add_scope");
    if(remove)ImGui::OpenPopup("legit_remove_scope");
    const auto before=state.values;bool changed=false;
    if(widgets::begin_popup("legit_add_scope",anchor,theme::popup_width,430,nullptr,false,true,false,false)){
        const auto caption=[&](const char* text){
            ImGui::Dummy({0,theme::popup_padding*state.scale});
            widgets::caption(ImGui::GetCursorScreenPos(),text,state.scale);
            ImGui::Dummy({0,theme::caption_height*state.scale});
        };
        caption("Categories");
        for(int i=0;i<std::size(combat::legit_category_names);++i){ImGui::BeginDisabled(set->categories[i].enabled);
            if(scope_item(combat::legit_category_names[i])){reset_scope_hotkeys(i,0);set->categories[i]={true,set->global};scope.category=i;scope.weapon=0;changed=true;}ImGui::EndDisabled();}
        ImGui::Dummy({0,theme::popup_padding*state.scale});ImGui::Separator();
        caption("Weapons");
        for(const auto& weapon:combat::legit_weapons){const auto found=std::find_if(set->weapons.begin(),set->weapons.end(),[&](const auto& item){return item.definition==weapon.definition;});
            ImGui::BeginDisabled(found!=set->weapons.end()&&found->value.enabled);
            if(scope_item(weapon.name)){reset_scope_hotkeys(-1,weapon.definition);if(found==set->weapons.end())set->weapons.push_back({weapon.definition,{true,set->global}});else found->value={true,set->global};scope.category=-1;scope.weapon=weapon.definition;changed=true;}ImGui::EndDisabled();}
        for(const auto& weapon:cosmetics::weapons())if(!weapon.glove&&!weapon.knife&&!combat::legit_weapon(weapon.definition)){
            ImGui::BeginDisabled();scope_item(weapon.name);ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))widgets::tooltipf("No implemented Legit override for this weapon.");
        }
        widgets::end_popup();
    }
    if(widgets::begin_popup("legit_remove_scope",removal.anchor,300,140,"Remove override")){
        ImGui::TextWrapped("Remove %s? Inherited settings will apply.",removal.label.c_str());
        if(widgets::button("Remove")){
            if(removal.owner==&state && removal.profile==model.selected_profile){
                reset_scope_hotkeys(removal.category,removal.weapon);
                if(removal.weapon)std::erase_if(set->weapons,[&](const auto& item){return item.definition==removal.weapon;});
                else if(removal.category>=0 && removal.category<static_cast<int>(set->categories.size()))set->categories[removal.category]={};
                scope.category=-1;scope.weapon=0;changed=true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(widgets::button("Cancel"))ImGui::CloseCurrentPopup();widgets::end_popup();
    }
    if(changed){state.dirty=true;commit_feature_edit(state,before);}
}
void render(MenuRuntime& state) {
    const auto before=state.values;auto& model=state.values.cosmetics.combat_settings;model.initialize_profiles();bool changed=synchronize_hotkeys(state);
    const float scale=state.scale;const int columns=ImGui::GetContentRegionAvail().x>=440*scale?2:1;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{theme::column_gap*scale*.5f,0});
    if(state.search_reveal_pending&&state.search_target.starts_with("Combat/Legit/")){scope.category=-1;scope.weapon=0;}
    auto* value=selected_settings(model);
    if(value&&ImGui::BeginTable("legit_setting_columns",columns,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)){
        ImGui::TableNextColumn();
        main_header(state,model,changed);
        if(columns==2){ImGui::TableNextRow();ImGui::TableNextColumn();}
        const bool blocked=!model.master_enabled;
        const auto multi=[&](const char* path,const char* label,std::span<bool> selected,std::span<const widgets::Option> choices,bool disabled){
            if(!matches(state,path,label))return false;
            ImGui::PushID(path);const bool edited=widgets::multi_combo("value",label,selected,choices,label,disabled,nullptr,true,selected_binding(path));reveal(state,path);ImGui::PopID();return edited;
        };
        constexpr widgets::Option hitboxes[]{{"Head"},{"Chest"},{"Stomach"},{"Pelvis"},{"Arms"},{"Legs"}};
        if(widgets::begin_group("legit_aim","Aimbot")){
            changed|=toggle(state,*value,"enabled",blocked);
            const bool inactive=blocked||!value->enabled;
            changed|=choice(state,*value,"aim_mode",inactive);
            constexpr widgets::Option mode_activations[]{{"Always"},{"On fire"},{"Keybind"}};
            struct ActivationKey {combat::LegitSettings* value;bool* changed;bool disabled;};
            ActivationKey key_context{value,&changed,inactive};
            const widgets::PopupContents key_popup{"Keybind",[](void* data){auto& context=*static_cast<ActivationKey*>(data);auto key=imgui_key(context.value->aim_key);if(widgets::key_row("aim_activation_key","Keybind",key,context.disabled)){context.value->aim_key=virtual_key(key);*context.changed=true;}},&key_context,90,state.search_reveal_pending&&state.search_target==aim_key_path};
            if(value->aim_mode==1&&value->activation==1){value->activation=0;changed=true;}
            if(value->aim_mode==1){constexpr widgets::Option choices[]{{"Always"},{"Keybind"}};int selected=value->activation==2?1:0;if(widgets::combo("activation","Activation",selected,choices,inactive,value->activation==2?&key_popup:nullptr,false,true)){value->activation=selected?2:0;value->on_fire=false;changed=true;}}
            else if(widgets::combo("activation","Activation",value->activation,mode_activations,inactive,value->activation==2?&key_popup:nullptr,false,true)){value->on_fire=value->activation==1;changed=true;}
            std::array conditions{value->pause_flashed,value->smoke_check,value->air_check};
            constexpr widgets::Option checks[]{{"Flash"},{"Smoke"},{"Air"}};
            if(multi("Combat/Legit/Aim assist/Checks","Checks",conditions,checks,inactive)){value->pause_flashed=conditions[0];value->smoke_check=conditions[1];value->air_check=conditions[2];changed=true;}
            changed|=toggle(state,*value,"through_walls",inactive);
            changed|=multi(hitbox_path,"Hitboxes",value->hitboxes,hitboxes,inactive);
            changed|=number(state,*value,"fov",inactive);
            changed|=toggle(state,*value,"visualize_fov",inactive);
            changed|=number(state,*value,value->aim_mode==0?"smoothing":"assist_strength",inactive);
            if(matches(state,number_path("reaction_time"),"Engagement delay")||matches(state,number_path("reaction_max"),"Engagement delay")){changed|=widgets::range_slider("aim_delay","Engagement delay",value->reaction_time,value->reaction_max,0,500,1,inactive);reveal(state,number_path("reaction_time"));reveal(state,number_path("reaction_max"));}
            constexpr const char* recoil_path="Combat/Legit/RCS";
            if(matches(state,recoil_path,"Recoil control Enabled Start bullet Vertical Horizontal Standalone")||state.search_target.starts_with(recoil_path)){
                ImGui::PushID(recoil_path);
                const auto opened=widgets::panel_row("value","Recoil control",blocked);
                const bool request=state.search_reveal_pending&&state.search_target.starts_with(recoil_path);
                reveal(state,recoil_path);
                if(!blocked&&(opened.clicked||(request&&!ImGui::IsPopupOpen("details"))))ImGui::OpenPopup("details");
                if(widgets::begin_popup("details",opened.bounds,theme::popup_width,430,nullptr,true,true)){
                    changed|=toggle(state,*value,"recoil_control",blocked);
                    for(const auto* key:{"recoil_vertical","recoil_horizontal"})changed|=number(state,*value,key,blocked||!value->recoil_control);
                    changed|=toggle(state,*value,"standalone_recoil",blocked||!value->recoil_control);
                    widgets::end_popup();
                }
                ImGui::PopID();
            }
        }widgets::end_group();group_gap(state);
        ImGui::TableNextColumn();
        if(widgets::begin_group("legit_trigger","Triggerbot")){
            changed|=toggle(state,*value,"trigger",blocked);
            const bool inactive=blocked||!value->trigger;
            std::array conditions{value->trigger_flash,value->trigger_smoke,value->trigger_air,value->scoped_only};
            constexpr widgets::Option checks[]{{"Flash"},{"Smoke"},{"Air"},{"Require sniper scope"}};
            if(multi("Combat/Legit/Triggerbot/Checks","Checks",conditions,checks,inactive)){value->trigger_flash=conditions[0];value->trigger_smoke=conditions[1];value->trigger_air=conditions[2];value->scoped_only=conditions[3];changed=true;}
            changed|=multi(trigger_hitbox_path,"Hitboxes",value->trigger_hitboxes,hitboxes,inactive);
            changed|=number(state,*value,"trigger_hit_chance",inactive);
            changed|=number(state,*value,"trigger_minimum_damage",inactive);
            if(matches(state,number_path("trigger_reaction"),"Delay")||matches(state,number_path("trigger_reaction_max"),"Delay")){changed|=widgets::range_slider("trigger_delay","Delay",value->trigger_reaction,value->trigger_reaction_max,0,1000,1,inactive);reveal(state,number_path("trigger_reaction"));reveal(state,number_path("trigger_reaction_max"));}
        }widgets::end_group();ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    remember_hotkey_frame();
    if(changed){if(value)combat::normalize_delays(*value);state.dirty=true;commit_feature_edit(state,before);}
}
void evaluate(MenuRuntime& state) {
    if(!state.gameplay_wired)return;
    auto& model=state.values.cosmetics.combat_settings;auto* set=model.selected_set();if(!set)return;
    const auto before=state.values;bool any_changed=synchronize_hotkeys(state);
    each_scope(*set,[&](combat::LegitSettings& value,int category,int weapon){
        const auto binding=[&](const char* path){return legit_profiles::binding(path,category,weapon);};
        bool changed=false;
        for(const auto& field:combat::legit_toggles){if(!exposed(toggle_path(field.key)))continue;const auto id=binding(toggle_path(field.key));auto& option=hotkeys::option(id);option.kind=hotkeys::Kind::Boolean;const auto key=std::string_view(field.key);const bool inactive=key=="standalone_recoil"?!value.recoil_control:key=="visualize_fov"?!value.enabled:false;changed|=hotkeys::evaluate(id,value.*field.member,!model.master_enabled||inactive);}
        for(const auto& field:combat::legit_numbers){if(!exposed(number_path(field.key)))continue;const auto id=binding(number_path(field.key));auto& option=hotkeys::option(id);const double multiplier=percent_field(field.key)?100.:1.;option.kind=hotkeys::Kind::Number;option.minimum=field.low*multiplier;option.maximum=field.high*multiplier;double number=(value.*field.member)*multiplier;const auto path=std::string_view(number_path(field.key));const bool inactive=path.starts_with("Combat/Legit/RCS")?!value.recoil_control:path.starts_with("Combat/Legit/Triggerbot")?!value.trigger:!value.enabled;if(hotkeys::evaluate(id,number,!model.master_enabled||inactive)){value.*field.member=static_cast<float>(number/multiplier);changed=true;}}
        for(const auto& field:combat::legit_choices){if(!exposed(choice_path(field.key)))continue;const auto id=binding(choice_path(field.key));auto& option=hotkeys::option(id);option.kind=hotkeys::Kind::Selection;option.minimum=0;option.maximum=static_cast<double>(field.names.size()-1);option.choices.assign(field.names.begin(),field.names.end());option.choice_disabled.assign(field.names.size(),false);double number=value.*field.member;if(hotkeys::evaluate(id,number,!model.master_enabled)){value.*field.member=std::clamp(static_cast<int>(number),0,static_cast<int>(field.names.size())-1);changed=true;}}
        for(const auto& item:{std::pair{hitbox_path,&value.hitboxes},std::pair{trigger_hitbox_path,&value.trigger_hitboxes},std::pair{multipoint_path,&value.multipoint}}){if(!exposed(item.first))continue;const auto id=binding(item.first);auto& option=hotkeys::option(id);option.kind=hotkeys::Kind::Multi;option.choices={"Head","Chest","Stomach","Pelvis","Arms","Legs"};option.choice_disabled.assign(6,false);changed|=hotkeys::evaluate(id,*item.second,!model.master_enabled||(std::string_view(item.first)==trigger_hitbox_path?!value.trigger:!value.enabled));}
        for(int trigger=0;trigger<2;++trigger){
            const char* path=trigger?"Combat/Legit/Triggerbot/Checks":"Combat/Legit/Aim assist/Checks";
            std::array<bool,4> conditions=trigger?std::array{value.trigger_flash,value.trigger_smoke,value.trigger_air,value.scoped_only}:std::array{value.pause_flashed,value.smoke_check,value.air_check,false};
            const auto id=binding(path);auto& option=hotkeys::option(id);option.kind=hotkeys::Kind::Multi;
            option.choices=trigger?std::vector<std::string>{"Flash","Smoke","Air","Require sniper scope"}:std::vector<std::string>{"Flash","Smoke","Air"};option.choice_disabled.assign(option.choices.size(),false);
            if(hotkeys::evaluate(id,std::span(conditions).first(option.choices.size()),!model.master_enabled||(trigger?!value.trigger:!value.enabled))){if(trigger){value.trigger_flash=conditions[0];value.trigger_smoke=conditions[1];value.trigger_air=conditions[2];value.scoped_only=conditions[3];}else{value.pause_flashed=conditions[0];value.smoke_check=conditions[1];value.air_check=conditions[2];}changed=true;}
        }
        // Keep existing condition hotkey identities and meanings across migration.
        for(int trigger=0;trigger<2;++trigger){
            const char* path=trigger?"Combat/Legit/Triggerbot/Conditions":"Combat/Legit/Aim assist/Conditions";
            auto& option=hotkeys::option(binding(path));if(option.entries.empty())continue;
            std::array<bool,3> conditions=trigger?std::array{value.trigger_flash,value.trigger_air,value.scoped_only}:std::array{value.activation==1,value.pause_flashed,false};
            option.kind=hotkeys::Kind::Multi;option.choices=trigger?std::vector<std::string>{"Flash","Air","Require sniper scope"}:std::vector<std::string>{"Only while firing","Pause while flashed"};option.choice_disabled.assign(option.choices.size(),false);
            if(hotkeys::evaluate(binding(path),std::span(conditions).first(option.choices.size()),!model.master_enabled||(trigger?!value.trigger:!value.enabled))){if(trigger){value.trigger_flash=conditions[0];value.trigger_air=conditions[1];value.scoped_only=conditions[2];}else{if(value.activation!=2)value.activation=conditions[0]?1:0;value.on_fire=conditions[0];value.pause_flashed=conditions[1];}changed=true;}
        }
        if(changed){combat::normalize_delays(value);any_changed=true;}
    });
    remember_hotkey_frame();
    if(any_changed){state.dirty=true;commit_feature_edit(state,before);}
}
}
