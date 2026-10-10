#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
void update_postprocessing(Runtime& state,EnvironmentEntity& saved) {
    const auto& settings=state.environment;
    const auto offset=[&](const char* type,const char* name){return environment_offset(state,type,name);};

            const int exposure=offset("C_PostProcessingVolume","m_flExposureCompensation"),control=offset("C_PostProcessingVolume","m_bExposureControl");
            if(settings.exposure!=0){environment_write(saved,exposure,settings.exposure);environment_write(saved,control,true);}
            else {restore_environment_field(saved,exposure);restore_environment_field(saved,control);}
        }

}
