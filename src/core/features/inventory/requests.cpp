#include "core/features/inventory/requests.hpp"
#include "core/features/inventory/rarities.hpp"
#include "core/sdk/binding_values.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <optional>

namespace iris::cosmetics::inventory {
namespace {
namespace b=iris::sdk::binding_values;
std::uint16_t metadata(unsigned quality,int definition,int paint=0) {
    return static_cast<std::uint16_t>((quality<<b::layout_econ_quality_shift) |
        (1u<<b::layout_econ_level_shift) |
        (static_cast<unsigned>(item_rarity(definition,paint))<<b::layout_econ_rarity_shift));
}
native::ItemSpec item(const Selection& selection) {
    native::ItemSpec spec;spec.definition=static_cast<std::uint16_t>(selection.definition);
    const bool special=is_knife(selection.definition)||is_glove(selection.definition);
    const auto quality=selection.stattrak>=0?b::layout_econ_quality_strange:
        special?b::layout_econ_quality_unusual:
        selection.paint?b::layout_econ_quality_unique:b::layout_econ_quality_normal;
    spec.packed_metadata=metadata(static_cast<unsigned>(quality),selection.definition,selection.paint);spec.name=selection.name;
    // These IDs and storage types come from the installed attribute schema.
    // Keep zero paint explicit so a shared item cannot inherit a previous finish.
    spec.attributes={{6,std::bit_cast<std::uint32_t>(static_cast<float>(selection.paint))},
        {7,std::bit_cast<std::uint32_t>(static_cast<float>(selection.seed))},
        {8,std::bit_cast<std::uint32_t>(selection.wear)}};
    if(selection.stattrak>=0) {
        spec.attributes.push_back({80,static_cast<std::uint32_t>(selection.stattrak)});
        spec.attributes.push_back({81,0});
    }
    return spec;
}
std::optional<std::uint16_t> firearm_slot(const EquippedLoadout& roster,std::size_t team,int definition) {
    if(!roster.available)throw std::runtime_error("Waiting for the actual equipped weapon routes.");
    int found=-1;
    for(const auto& route:roster.teams[team])if(route.definition==definition) {
        const bool flexible=(route.slot>=2&&route.slot<=6)||(route.slot>=8&&route.slot<=12)||(route.slot>=14&&route.slot<=18);
        if(!flexible || found!=-1)throw std::runtime_error("The selected weapon has an ambiguous native loadout route.");
        found=route.slot;
    }
    if(found<0)return std::nullopt;
    return static_cast<std::uint16_t>(found);
}
}
std::vector<Request> requests(const Configuration& configuration,const EquippedLoadout& roster) {
    validate_configuration(configuration);
    std::vector<Request> desired;
    if(!configuration.enabled)return desired;
    for(std::size_t team=0;team<2;++team) {
        const auto native_team=static_cast<std::uint16_t>(team+2);
        for(const auto& selection:configuration.loadouts[team]) {
            if(!selection.enabled)continue;
            const auto slot=is_knife(selection.definition)?std::optional<std::uint16_t>(static_cast<std::uint16_t>(b::layout_econ_slot_knife)):
                selection.definition==31?std::optional<std::uint16_t>(static_cast<std::uint16_t>(b::layout_econ_slot_zeus)):
                firearm_slot(roster,team,selection.definition);
            // Keep the copied selection until this weapon has an actual route.
            // A skin selection must not replace a different loadout weapon.
            if(slot)desired.push_back({{native_team,*slot},item(selection)});
        }
        const auto& glove=configuration.gloves[team];
        if(glove.enabled)desired.push_back({{native_team,static_cast<std::uint16_t>(b::layout_econ_slot_gloves)},item(glove)});
        // Custom models displace only an Iris-owned catalog agent. Omitting the
        // native request lets OwnedItems restore that slot and retire its item.
        if(configuration.custom_models[team].empty() && configuration.agents[team]) {
            native::ItemSpec spec;spec.definition=static_cast<std::uint16_t>(configuration.agents[team]);
            spec.packed_metadata=metadata(static_cast<unsigned>(b::layout_econ_quality_unique),spec.definition);
            desired.push_back({{native_team,static_cast<std::uint16_t>(b::layout_econ_slot_agent)},std::move(spec)});
        }
    }
    if(configuration.music_kit) {
        native::ItemSpec spec;spec.definition=static_cast<std::uint16_t>(b::layout_econ_music_definition);
        spec.packed_metadata=metadata(static_cast<unsigned>(b::layout_econ_quality_unique),spec.definition);
        spec.attributes={{166,static_cast<std::uint32_t>(configuration.music_kit)}};
        desired.push_back({{static_cast<std::uint16_t>(b::layout_econ_music_team),static_cast<std::uint16_t>(b::layout_econ_slot_music)},std::move(spec)});
    }
    return desired;
}
}
