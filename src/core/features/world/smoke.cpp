#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
void update_smoke(Runtime& state,EnvironmentEntity& saved,std::uint32_t handle) {
    const auto& settings=state.environment;
    const auto offset=[&](const char* type,const char* name){return environment_offset(state,type,name);};

            const int tint=offset("C_SmokeGrenadeProjectile","m_vSmokeColor");
            if(settings.smoke_override){auto rgb=settings.smoke_color;for(auto& channel:rgb)channel*=255.f;environment_write(saved,tint,rgb);}
            else restore_environment_field(saved,tint);
            if(settings.smoke_override){
                const auto volume=memory::read<Address>(saved.address+sdk::world_bindings::smoke_state).value_or(0);
                if(volume){
                    auto& held=state.smoke_volumes[handle];
                    if(held.address!=volume){restore_environment_entity(held,false);held.address=volume;}
                    environment_write(held,static_cast<int>(sdk::world_bindings::smoke_state_color),settings.smoke_color);
                }
            }
        }

}
