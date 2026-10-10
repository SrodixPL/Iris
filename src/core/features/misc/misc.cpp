#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
namespace iris::cosmetics::detail {
void __fastcall deathnotice_hook(void* self,void* event){
    auto& state=runtime();
    const auto address=reinterpret_cast<Address>(self);
    const auto lifetime=memory::read<float>(address+iris::sdk::binding_values::layout_decal_lifetime);
    const auto multiplier=memory::read<float>(address+iris::sdk::binding_values::layout_decal_multiplier);
    const bool preserve=state.killfeed_enabled.load() && lifetime && multiplier;
    // Extend new notices through the native event path, with no retained HUD
    // pointer and no permanent edits to the panel's cached style values.
    if(preserve){(void)memory::write(address+iris::sdk::binding_values::layout_decal_lifetime,3600.f);(void)memory::write(address+iris::sdk::binding_values::layout_decal_multiplier,1.f);}
    state.original_deathnotice(self,event);
    if(preserve){(void)memory::write(address+iris::sdk::binding_values::layout_decal_lifetime,*lifetime);(void)memory::write(address+iris::sdk::binding_values::layout_decal_multiplier,*multiplier);}
}

}
