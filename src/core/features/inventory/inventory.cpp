#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/inventory/patch.hpp"
namespace iris::cosmetics::detail {
void publish_finishes(Runtime& state) {
    auto snapshot = std::make_shared<std::map<Address, Selection>>();
    auto menu_snapshot = std::make_shared<std::map<Address, Selection>>();
    for (const auto& [handle, weapon] : state.changed) {
        (void)handle;
        if (weapon.finish_override) snapshot->emplace(weapon.entity + state.fields.attributes + state.fields.item, weapon.selection);
        if(weapon.menu_weapon && weapon.finish_override)menu_snapshot->emplace(weapon.entity+state.fields.attributes+state.fields.item,weapon.selection);
    }
    if (state.glove && state.glove->finish_override)
        snapshot->emplace(state.glove->entity + state.fields.gloves, state.glove->selection);
    if (state.preview.constructed && state.preview.rendering)
        snapshot->emplace(reinterpret_cast<Address>(state.preview.item.data()), *state.preview.rendering);
    state.finishes.store(std::move(snapshot));
    state.menu_finishes.store(std::move(menu_snapshot));
}
std::optional<Selection> overridden_finish(void* item) {
    // Material work may run on another thread while the update waits for it.
    // An immutable snapshot avoids taking the game-update mutex in that path.
    const auto snapshot = runtime().finishes.load();
    if (snapshot) if (const auto found = snapshot->find(reinterpret_cast<Address>(item)); found != snapshot->end()) return found->second;
    return {};
}
int __fastcall paint_hook(void* item) {
    if (const auto selected = overridden_finish(item)) {
        if (is_glove(selected->definition)) ++runtime().glove_reads;
        return selected->paint;
    }
    return runtime().original_paint(item);
}
int __fastcall seed_hook(void* item) {
    if (const auto selected = overridden_finish(item)) return selected->seed;
    return runtime().original_seed(item);
}
float __fastcall wear_hook(void* item, float fallback) {
    if (const auto selected = overridden_finish(item)) return selected->wear;
    return runtime().original_wear(item, fallback);
}
bool __fastcall attributes_hook(void* item, void* visitor) {
    auto& state=runtime();
    // Inventory image generation walks attributes rather than the paint/wear
    // getters. Supply those attributes for our owned preview items only; never
    // attach borrowed storage to a network-owned attribute vector.
    const auto menu_snapshot=state.menu_finishes.load();
    const auto menu_selection=menu_snapshot?menu_snapshot->find(reinterpret_cast<Address>(item)):std::map<Address,Selection>::const_iterator{};
    const bool menu_item=menu_snapshot && menu_selection!=menu_snapshot->end();
    if(item==state.preview.item.data() || menu_item) {
        const auto selected=menu_item?std::optional<Selection>{menu_selection->second}:overridden_finish(item);
        if(selected) {
            using Schema = void*(__fastcall*)();
            using Definition = void*(__fastcall*)(void*, int);
            using Visit = bool(__fastcall*)(void*, void*, void*, const float*);
            const auto schema=reinterpret_cast<Schema>(state.client+bindings::item_schema)();
            const std::array<std::pair<int,float>,3> values{{{6,static_cast<float>(selected->paint)},
                {7,static_cast<float>(selected->seed)}, {8,selected->wear}}};
            for(const auto& [id,value]:values) {
                const auto definition=reinterpret_cast<Definition>(state.client+bindings::attribute_definition)(schema,id);
                if(!definition)continue;
                const auto type=memory::read<Address>(reinterpret_cast<Address>(definition)+iris::sdk::binding_values::layout_attribute_type).value_or(0);
                const auto table=memory::read<Address>(type).value_or(0);
                const auto visit=reinterpret_cast<Visit>(memory::read<Address>(table+iris::sdk::binding_values::layout_attribute_visit_slot).value_or(0));
                if(!visit)continue;
                if(!visit(reinterpret_cast<void*>(type),visitor,definition,&value))return false;
            }
            // Menu items can carry the equipped skin's original attributes;
            // visiting them after the override would restore that old paint.
            if(menu_item)return true;
        }
    }
    return state.original_attributes(item,visitor);
}
const char* __fastcall name_hook(void* item, bool short_name) {
    auto& state = runtime();
    auto& cached = name_cache.get();
    const auto selected = overridden_finish(item);
    const auto previous = cached.find(item);
    const bool changed = selected ? previous == cached.end() || previous->second != *selected : previous != cached.end();
    if (changed) {
        // The native getter lazily builds this description once. Reuse its
        // generator to rebuild an existing description after an item change.
        const auto description = memory::read<Address>(reinterpret_cast<Address>(item) + bindings::item_name_cache);
        if (description && *description)
            state.rebuild_name(reinterpret_cast<void*>(*description), state.name_context(), item, nullptr);
        if (selected) cached.insert_or_assign(item, *selected);
        else if (previous != cached.end()) cached.erase(previous);
    }
    return state.original_name(item, short_name);
}
void __fastcall hud_update_hook(void* self, void* pawn) {
    auto& state = runtime();
    // Regular HUD rows are cached by entity handle. A model switch keeps that
    // handle, so retire the rows through their native lifecycle on this thread.
    // Each view has its own state; embedded and main views update together.
    auto& views = hud_cache.get();
    auto& previous = views[self];
    const auto knife = state.hud_knife.load();
    const Address vdata = knife ? knife->vdata : 0;
    const int team = knife ? knife->team : 0;
    if (pawn && (vdata != previous.vdata || team != previous.team)) {
        if (vdata || previous.vdata) {
            state.clear_hud(self);
            overlay_log("Knife model changed: retired cached weapon rows on the regular HUD update thread.");
        }
        previous = HudKnife{team, vdata};
    }
    state.original_hud_update(self, pawn);
}

void restore(Runtime& state, ChangedWeapon& weapon) {
    weapon.finish_override = false;
    publish_finishes(state);
    if (entity(state, weapon.handle) != weapon.entity) return;
    const auto scene = get<Address>(weapon.entity + state.fields.scene);
    for (auto it = weapon.fields.rbegin(); it != weapon.fields.rend(); ++it) {
        if (it->scene && scene != it->scene) continue;
        if (!memory::write(it->address, std::span<const std::byte>(it->bytes))) throw std::runtime_error("Cannot restore a cosmetic field.");
    }
    if (weapon.model_changed) {
        state.subclass_changed(reinterpret_cast<void*>(weapon.entity));
        if (!weapon.original_model.empty()) state.set_model(reinterpret_cast<void*>(weapon.entity), weapon.original_model.c_str());
        state.rebuild_materials(reinterpret_cast<void*>(weapon.entity), false);
    } else if(weapon.menu_weapon) {
        state.rebuild_materials(reinterpret_cast<void*>(weapon.entity), false);
    }
    // A held weapon can have a zero mesh snapshot. Restoring that after a drop
    // hides the world weapon. Normalize to its original visible mesh instead.
    if (scene && scene == weapon.scene && !weapon.original_mesh &&
        !entity(state, get<std::uint32_t>(weapon.entity + state.fields.owner)))
        if (!memory::write(scene + state.fields.model + state.fields.mesh, std::uint64_t{1})) throw std::runtime_error("Cannot restore world-weapon visibility.");
}
void sample_roster(Runtime& state,RosterSample& next) {
    const Address manager = state.client + bindings::inventory_manager;
    if (get<Address>(manager) != state.client + bindings::inventory_vtable ||
        method(manager,static_cast<int>(iris::sdk::binding_values::slots_inventory_local)) != state.client + bindings::local_inventory)
        throw std::runtime_error("Inventory manager differs from reviewed bindings.");
    using LocalInventory = Address(__fastcall*)(void*);
    using LoadoutItem = Address(__fastcall*)(void*, int, int);
    const auto inventory = reinterpret_cast<LocalInventory>(state.client + bindings::local_inventory)(reinterpret_cast<void*>(manager));
    if (!inventory) throw std::runtime_error("Waiting for the equipped inventory.");
    if (method(inventory,static_cast<int>(iris::sdk::binding_values::slots_inventory_loadout)) != state.client + bindings::loadout_item)
        throw std::runtime_error("Equipped-item reader differs from reviewed bindings.");
    for (int team = 0; team < 2; ++team) {
        for (int slot = 0; slot < 58; ++slot) {
            const auto item = reinterpret_cast<LoadoutItem>(state.client + bindings::loadout_item)(reinterpret_cast<void*>(inventory), team + 2, slot);
            if (!item) continue;
            const int definition = get<std::uint16_t>(item + state.fields.definition);
            if (!find_weapon(definition)) continue;
            const bool firearm_slot = (slot >= 2 && slot <= 6) || (slot >= 8 && slot <= 12) || (slot >= 14 && slot <= 18);
            if ((slot == 0 && is_knife(definition)) || firearm_slot || is_glove(definition)) {
                bool duplicate=false;
                for(std::size_t i=0;i<next.counts[team];++i) {
                    const auto& entry=next.teams[team][i];
                    if(entry.definition==definition || (is_glove(entry.definition)&&is_glove(definition))){duplicate=true;break;}
                }
                if(!duplicate)next.teams[team][next.counts[team]++]={slot,definition};
            }
        }
        std::size_t firearms{};
        for(std::size_t i=0;i<next.counts[team];++i) {
            const auto definition=next.teams[team][i].definition;
            if(!is_knife(definition)&&!is_glove(definition))++firearms;
        }
        if(firearms!=15)throw std::runtime_error("The equipped inventory has not supplied all 15 weapon slots.");
    }
}
bool guarded_roster_sample(Runtime* state,RosterSample* sample) {
    __try {sample_roster(*state,*sample);return true;}
    __except(GetExceptionCode()==0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER){return false;}
}
void read_roster(Runtime& state) {
    RosterSample sample;
    if(!guarded_roster_sample(&state,&sample))throw std::runtime_error("Equipped inventory reader raised an exception.");
    // SEH can abandon native callback frames. Publish STL objects outside that boundary.
    EquippedLoadout next;
    for(std::size_t team=0;team<sample.teams.size();++team)
        for(std::size_t i=0;i<sample.counts[team];++i)next.teams[team].push_back(sample.teams[team][i]);
    next.available = true;
    next.detail = "Equipped loadout synced";
    next.revision = state.roster.revision + (next.teams != state.roster.teams || !state.roster.available);
    state.roster = std::move(next);
}
bool guarded_roster(Runtime* state) {read_roster(*state);return true;}

void update(Runtime& state) {
    const auto previous_revision = state.report.applied_revision;
    const auto previous_count = state.report.updated_weapons;
    const auto previous_pawn = state.last_pawn;
    const auto previous_camera = state.camera_applied;
    const auto controller = get<Address>(state.client + bindings::local_controller);
    const auto pawn_handle = controller ? get<std::uint32_t>(controller + state.fields.pawn) : 0xffffffffu;
    const auto pawn = entity(state, pawn_handle);
    const bool alive = pawn && get<int>(pawn + state.fields.health) > 0;
    const int team = pawn ? get<std::uint8_t>(pawn + state.fields.team) : 0;
    state.report.team = team;
    update_agent(state, pawn, pawn_handle, alive, team);
    update_glove(state, pawn, pawn_handle, alive, team);
    const auto camera = get<Address>(state.client + bindings::input);
    state.camera_enabled.store(camera && alive && !state.stopping && state.requested.thirdperson);
    if (camera && alive && !state.stopping && state.requested.thirdperson) {
        if (!state.camera_saved) {
            state.original_camera = get<bool>(camera + bindings::camera_flag);
            state.camera_saved = true;
        }
        if (!state.camera_applied || state.last_pawn != pawn || !get<bool>(camera + bindings::camera_flag)) state.third();
        state.camera_applied = true;
    } else if (state.camera_saved && camera) {
        // Turning the switch off is an explicit request for first person.
        // Restoring a captured third-person flag kept the switch stuck on.
        state.first();
        state.camera_saved = state.camera_applied = false;
    }
    state.last_pawn = pawn;
    const auto owners=weapon_owners(state,pawn,alive,team);
    bool refreshed = false, refresh_hud = false;
    for (auto it = state.changed.begin(); it != state.changed.end();) {
        const auto* desired = selection_for(state.requested, it->second.team, it->second.original_definition);
        const bool exists = entity(state, it->first) == it->second.entity;
        const bool changed_team = exists && entity(state, get<std::uint32_t>(it->second.entity + state.fields.owner)) == pawn && team != it->second.team;
        const bool preview_retired=exists && it->second.menu_weapon && std::find(owners.begin(),owners.end(),
            std::pair{entity(state,get<std::uint32_t>(it->second.entity+state.fields.owner)),it->second.team})==owners.end();
        // Retain a dropped weapon's finish and original team until its serial
        // expires, an override changes, or the changer is disabled.
        if (!exists || changed_team || preview_retired || state.stopping || !state.requested.enabled || !desired || *desired != it->second.selection) {
            if (exists) { refresh_hud |= it->second.model_changed && !it->second.menu_weapon; restore(state, it->second); refreshed = true; }
            it = state.changed.erase(it);
            publish_finishes(state);
        } else {
            // Resource loading and HUD-model creation complete after SetModel.
            // Refresh again on later updates rather than only on the change tick.
            if ((alive || it->second.menu_weapon) && it->second.refresh_ticks) {
                --it->second.refresh_ticks;
                if (it->second.refresh_ticks == 4 || it->second.refresh_ticks == 0) {
                    state.rebuild_materials(reinterpret_cast<void*>(it->second.entity), false);
                    refresh_hud |= !it->second.menu_weapon;
                    refreshed = true;
                }
            }
            ++it;
        }
    }
    if (state.requested.enabled && !state.stopping)for(const auto& [weapon_owner,owner_team]:owners) {
        const bool menu_weapon=weapon_owner!=pawn;
        const auto services = get<Address>(weapon_owner + state.fields.services);
        if (services) {
            const auto vector = services + state.fields.weapons;
            const auto count = get<std::uint64_t>(vector);
            const auto handles = get<Address>(vector + 8);
            if (count > 64 || (count && !handles)) throw std::runtime_error("Weapon services layout mismatch.");
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto handle = get<std::uint32_t>(handles + i * sizeof(std::uint32_t));
                const auto address = entity(state, handle);
                if (!address || state.changed.contains(handle)) continue;
                const auto owner = entity(state, get<std::uint32_t>(address + state.fields.owner));
                if (owner != weapon_owner) continue;
                const auto item = address + state.fields.attributes + state.fields.item;
                const auto definition = get<std::uint16_t>(item + state.fields.definition);
                const auto* selected = selection_for(state.requested, owner_team, definition);
                if (!selected || !valid(*selected)) continue;
                auto [entry, inserted] = state.changed.emplace(handle, ChangedWeapon{address, handle, *selected, {}});
                (void)inserted;
                auto& saved = entry->second;
                saved.original_definition = definition;
                saved.team = owner_team;
                saved.menu_weapon=menu_weapon;
                const auto node = get<Address>(address + state.fields.scene);
                saved.scene = node;
                if (node) {
                    saved.original_mesh = get<std::uint64_t>(node + state.fields.model + state.fields.mesh);
                    const auto name = get<Address>(node + state.fields.model + state.fields.model_name);
                    if (name) saved.original_model = string_at(name);
                }
                if (is_knife(definition) && selected->definition != definition) {
                    // Newly spawned weapons can exist before their model resource
                    // arrives. No fields have changed yet; retry on a later update.
                    if (saved.original_model.empty()) { state.changed.erase(entry); continue; }
                    patch(saved, item + state.fields.definition, static_cast<std::uint16_t>(selected->definition));
                    const auto subclass_name = std::to_string(selected->definition);
                    std::uint32_t token{};
                    state.make_token(&token, subclass_name.c_str());
                    patch(saved, address + state.fields.subclass, token);
                    saved.model_changed = true;
                    state.subclass_changed(reinterpret_cast<void*>(address));
                    state.set_model(reinterpret_cast<void*>(address), find_weapon(selected->definition)->model);
                    saved.refresh_ticks = 8;
                    refresh_hud |= !menu_weapon;
                }
                patch(saved, address + state.fields.paint, selected->paint);
                patch(saved, address + state.fields.seed, selected->seed);
                patch(saved, address + state.fields.wear, selected->wear);
                patch(saved, address + state.fields.stattrak, selected->stattrak);
                patch(saved, item + state.fields.name, selected->name);
                patch(saved, item + state.fields.id_high, std::uint32_t{0xffffffff});
                saved.finish_override = is_knife(definition) || menu_weapon;
                if (saved.finish_override) publish_finishes(state);
                const auto current_node = get<Address>(address + state.fields.scene);
                const auto* finish = find_finish(selected->definition, selected->paint);
                if (current_node) {
                    patch(saved, current_node + state.fields.model + state.fields.mesh, std::uint64_t{finish && finish->legacy ? 2u : 1u});
                    saved.fields.back().scene = current_node;
                }
                if (saved.model_changed || menu_weapon) {
                    state.rebuild_materials(reinterpret_cast<void*>(address), false);
                    saved.refresh_ticks=8;
                }
                refreshed = true;
            }
        }
    }
    if (refreshed && pawn) state.regenerate();
    std::shared_ptr<const HudKnife> hud_knife;
    if (alive && state.requested.enabled && !state.stopping) {
        for (const auto& [handle, weapon] : state.changed) {
            if (!weapon.model_changed || weapon.team != team || entity(state, handle) != weapon.entity ||
                entity(state, get<std::uint32_t>(weapon.entity + state.fields.owner)) != pawn) continue;
            const auto vdata = get<Address>(weapon.entity + state.fields.subclass + sizeof(Address));
            if (vdata) hud_knife = std::make_shared<HudKnife>(HudKnife{team, vdata});
            break;
        }
    }
    state.hud_knife.store(std::move(hud_knife));
    if (refresh_hud && alive) {
        const auto services = get<Address>(pawn + state.fields.services);
        if (services) {
            // The registered OnActiveWeaponChanged callback emits Panorama's
            // local-player weapon notification so the HUD can rebuild its icon.
            if (method(services,static_cast<int>(iris::sdk::binding_values::slots_weapon_active_changed)) != state.client + bindings::active_weapon_changed) throw std::runtime_error("Weapon HUD notification differs from the reviewed callback.");
            reinterpret_cast<SubclassChanged>(state.client + bindings::active_weapon_changed)(reinterpret_cast<void*>(services));
        }
    }
    state.report.updated_weapons = static_cast<unsigned>(state.changed.size());
    state.report.applied_revision = state.requested.revision;
    state.report.detail = state.stopping ? "Cosmetic updates stopped; original values restored." : !alive ? "Waiting for a live player in a map." :
        state.changed.empty() ? "Ready. Select a finish, enable skins, and equip that weapon." : "Attributes updated; inspect the equipped weapon to verify its finish.";
    if (state.first_update || previous_pawn != pawn || previous_revision != state.requested.revision || previous_count != state.report.updated_weapons || previous_camera != state.camera_applied) {
        char message[512]{};
        std::snprintf(message, sizeof(message), "Cosmetics: revision=%llu player=%s camera=%s weapons=%u. %s", state.report.applied_revision,
            alive ? "alive" : "absent", state.camera_applied ? "third" : "first", state.report.updated_weapons, state.report.detail.c_str());
        overlay_log(message);
        state.first_update = false;
    }
}
bool guarded_update(Runtime* state) {
    __try { update(*state); return true; }
    __except (GetExceptionCode() == 0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
