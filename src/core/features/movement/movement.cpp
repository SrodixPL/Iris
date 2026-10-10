#include "core/features/runtime.hpp"
#include "core/features/movement/steering.hpp"
#include "core/features/combat/nospread.hpp"
#include "core/sdk/entities.hpp"
namespace iris::cosmetics::detail {
void __fastcall forward_input_hook(void* input,int slot,float* movement){
    auto& state=runtime();state.original_forward_input(input,slot,movement);
    if(slot==0)if(const auto value=memory::read<float>(reinterpret_cast<Address>(movement)))nospread::native_input.sample(0,*value,GetTickCount());
}
void __fastcall side_input_hook(void* input,int slot,float* movement){
    auto& state=runtime();state.original_side_input(input,slot,movement);
    if(slot==0)if(const auto value=memory::read<float>(reinterpret_cast<Address>(movement)))nospread::native_input.sample(1,*value,GetTickCount());
}
void release_strafe(Runtime& state) {
    state.movement_override=false;state.strafe_axis=0;state.forward_axis=0;
}
void publish_movement(Runtime& state) {
    const auto& f=state.requested;
    movement::Settings settings;settings.keys=settings.smooth=f.strafe_assist;
    settings.strength=f.strafe_strength;settings.bhop=f.bhop;
    static_cast<movement::Options&>(settings.options)=f.movement_options;
    settings.rage_stop=nospread::rage_active()&&f.no_spread_settings.quick_stop;
    const auto published=nospread::movement_settings.load();
    if(state.stopping)nospread::movement_settings.store(nullptr);
    else if(!published||*published!=settings)nospread::movement_settings.store(std::make_shared<const movement::Settings>(settings));
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool active=!state.stopping&&foreground==GetCurrentProcessId()&&!renderer::captures_keyboard();
    const auto controller=active?get<Address>(state.client+bindings::local_controller):0;
    const auto pawn=controller?entity(state,get<std::uint32_t>(controller+state.fields.pawn)):0;
    nospread::player_alive=pawn&&get<int>(pawn+state.fields.health)>0;
    const int rules_field=environment_offset(state,"C_CSGameRulesProxy","m_pGameRules");
    const int freeze_field=environment_offset(state,"C_CSGameRules","m_bFreezePeriod");
    const int designer_field=environment_offset(state,"CEntityIdentity","m_designerName");
    auto proxy=sdk::entity(state.client,state.rules_handle);const auto now=GetTickCount64();
    if(!proxy&&now>=state.rules_scan&&designer_field>=0){state.rules_scan=now+250;
        const auto chunks=get<Address>(state.client+bindings::entity_chunks);
        if(chunks)for(unsigned group=0;group<64&&!proxy;++group){const auto chunk=memory::read<Address>(chunks+group*8).value_or(0);if(!chunk)continue;
            std::array<std::byte,512*sizeof(EntityIdentity)> rows{};if(!memory::read(chunk,rows))continue;
            for(unsigned i=0;i<512;++i){const auto row=std::span<const std::byte>(rows).subspan(i*sizeof(EntityIdentity),sizeof(EntityIdentity));
                const auto handle=memory::read_field<std::uint32_t>(row,offsetof(EntityIdentity,handle)).value_or(0xffffffff);
                if((handle&0x7fff)!=group*512+i)continue;
                const auto name=memory::read_field<Address>(row,designer_field).value_or(0);std::array<char,32> text{};
                if(!name||!memory::read(name,std::as_writable_bytes(std::span(text))))continue;text.back()=0;
                if(std::strcmp(text.data(),"cs_gamerules")!=0)continue;
                state.rules_handle=handle;proxy=sdk::entity(state.client,handle);break;
            }
        }
    }
    const auto rules=proxy&&rules_field>=0?memory::read<Address>(proxy+rules_field).value_or(0):0;
    nospread::round_frozen=!rules||freeze_field<0||memory::read<bool>(rules+freeze_field).value_or(true);
    if(!active||!nospread::player_alive)release_strafe(state);
}
}
