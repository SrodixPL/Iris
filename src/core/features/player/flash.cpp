#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
void update_flash(Runtime& state,EnvironmentEntity& saved) {
    const auto& settings=state.environment;
    const auto offset=[&](const char* type,const char* name){return environment_offset(state,type,name);};

            const auto controller=get<Address>(state.client+bindings::local_controller);
            const auto pawn=controller?entity(state,get<std::uint32_t>(controller+state.fields.pawn)):0;
            const int alpha=offset("C_CSPlayerPawnBase","m_flFlashMaxAlpha");
            if(settings.remove_flash && saved.address==pawn)environment_write(saved,alpha,0.f);
            else restore_environment_field(saved,alpha);
        }

}
