#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/features/world/weather_assets.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>

namespace iris::cosmetics::detail {
template<std::size_t N>
void install_weather_asset(const std::filesystem::path& path,const std::array<std::uint8_t,N>& bytes) {
    std::ifstream existing(path,std::ios::binary);
    std::vector<std::uint8_t> current((std::istreambuf_iterator<char>(existing)),{});
    if(current.size()==bytes.size() && std::equal(current.begin(),current.end(),bytes.begin()))return;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if(!output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()))throw std::runtime_error("Cannot install Iris precipitation resource.");
}
void clear_weather(Runtime& state){
    const auto manager=memory::read<Address>(state.client+bindings::particle_manager).value_or(0);
    const auto chunks=memory::read<Address>(state.client+bindings::entity_chunks).value_or(0);
    if(manager && manager==state.weather_manager && chunks==state.weather_chunks)
        for(auto index:state.weather_particles){
            reinterpret_cast<ParticleDestroy>(state.client+bindings::particle_destroy)(nullptr,index,true);
            reinterpret_cast<ParticleRelease>(state.client+bindings::particle_release)(nullptr,index);
        }
    state.weather_particles.clear();state.weather_pawn=0;state.weather_mode=0;state.weather_retry=0;
}
void update_weather(Runtime& state){
    const auto& settings=state.environment;
    const auto manager=get<Address>(state.client+bindings::particle_manager);
    const auto chunks=get<Address>(state.client+bindings::entity_chunks);
    const auto controller=get<Address>(state.client+bindings::local_controller);
    const auto pawn=controller?entity(state,get<std::uint32_t>(controller+state.fields.pawn)):0;
    if(state.stopping || !settings.enabled || !settings.weather || settings.weather_intensity<=0 || !manager ||
       !pawn || get<int>(pawn+state.fields.health)<=0){clear_weather(state);return;}
    if(state.weather_manager!=manager || state.weather_chunks!=chunks || state.weather_mode!=settings.weather || state.weather_pawn!=pawn)clear_weather(state);
    state.weather_manager=manager;state.weather_chunks=chunks;state.weather_mode=settings.weather;state.weather_pawn=pawn;
    // Each standalone emitter owns the complete precipitation volume.
    const unsigned count=1;
    while(state.weather_particles.size()>count){
        auto index=state.weather_particles.back();state.weather_particles.pop_back();
        reinterpret_cast<ParticleDestroy>(state.client+bindings::particle_destroy)(nullptr,index,true);
        reinterpret_cast<ParticleRelease>(state.client+bindings::particle_release)(nullptr,index);
    }
    if(GetTickCount64()>=state.weather_retry)while(state.weather_particles.size()<count){
        std::uint32_t index=0xffffffff;
        const char* effect=settings.weather==1?"particles/iris/rain_v1.vpcf":"particles/iris/snow_v1.vpcf";
        const auto root=models::game_content()/"particles/iris";
        install_weather_asset(root/"rain_v1.vpcf_c",weather_assets::rain);
        install_weather_asset(root/"snow_v1.vpcf_c",weather_assets::snow);
        const auto resource=load_resource(state,effect,0x66637076);
        if(!resource){state.weather_retry=GetTickCount64()+1000;break;}
        using Create=std::uint32_t*(__fastcall*)(void*,std::uint32_t*,const char*,int,void*,void*,void*,int);
        reinterpret_cast<Create>(state.client+sdk::world_bindings::particle_create)(reinterpret_cast<void*>(manager),&index,effect,8,nullptr,nullptr,nullptr,0);
        bool collection=false;
        const auto entries=get<Address>(manager+iris::sdk::binding_values::layout_particle_entries);
        const auto size=get<int>(manager+iris::sdk::binding_values::layout_particle_count);
        if(entries && size>=0 && size<65536)for(int i=0;i<size;++i){
            const auto entry=get<Address>(entries+i*sizeof(Address));
            if(entry && get<std::uint32_t>(entry+iris::sdk::binding_values::layout_particle_index)==index){collection=get<Address>(entry+iris::sdk::binding_values::layout_particle_collection)!=0;break;}
        }
        if(index==0xffffffff || !collection){
            if(index!=0xffffffff){
                reinterpret_cast<ParticleDestroy>(state.client+bindings::particle_destroy)(nullptr,index,true);
                reinterpret_cast<ParticleRelease>(state.client+bindings::particle_release)(nullptr,index);
            }
            state.weather_retry=GetTickCount64()+3000;
            overlay_log("Native weather creation returned no particle collection; retry delayed.");break;
        }
        if(state.weather_particles.empty()){
            char message[180]{};std::snprintf(message,sizeof(message),"Native weather collection created: %s handle=%u, standalone world-origin precipitation.",effect,index);overlay_log(message);
        }
        state.weather_particles.push_back(index);
    }
    const auto scene=get<Address>(pawn+state.fields.scene);
    const int field=environment_offset(state,"CGameSceneNode","m_vecAbsOrigin");
    if(!scene || field<0)return;
    auto origin=get<std::array<float,3>>(scene+field);
    using Control=bool(__fastcall*)(void*,std::uint32_t,int,const std::array<float,3>*,float);
    const auto control=reinterpret_cast<Control>(state.client+sdk::world_bindings::particle_control);
    for(unsigned i=0;i<state.weather_particles.size();++i){
        auto position=origin;position[2]+=160.f;
        position[0]+=settings.wind*.5f;
        control(reinterpret_cast<void*>(manager),state.weather_particles[i],0,&position,0.f);
        const std::array<float,3> intensity{std::clamp(settings.weather_intensity,0.f,1.f),1,1};
        control(reinterpret_cast<void*>(manager),state.weather_particles[i],1,&intensity,0.f);
    }
}

}
