#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
void update_wind(Runtime& state,EnvironmentEntity& saved) {
    const auto& settings=state.environment;
    const auto offset=[&](const char* type,const char* name){return environment_offset(state,type,name);};

            if(settings.wind==0){restore_environment_entity(saved);return;}
            const int shared=offset("C_EnvWind","m_EnvWindShared");
            if(shared>=0)for(const auto field:{"m_iMinWind","m_iMaxWind","m_iMinGust","m_iMaxGust"}){
                const int member=offset("C_EnvWindShared",field);
                if(member>=0)environment_write(saved,shared+member,static_cast<std::uint16_t>(settings.wind));
            }
        }

}
