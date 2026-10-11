#include "core/features/runtime.hpp"
#include "core/features/player/visuals.hpp"
#include "core/features/player/visual_native.hpp"
#include "core/sdk/entities.hpp"
#include "core/sdk/binding_values.hpp"
#include <set>
#include <algorithm>

namespace iris::cosmetics::detail {
namespace {
using visuals::Vector;
using nospread::finite;
template<class T> T field(Runtime& s,Address object,const char* type,const char* name,T fallback={}) {
    if(!object)return fallback;
    const auto offset=environment_offset(s,type,name);
    return offset<0?fallback:memory::read<T>(object+offset).value_or(fallback);
}
bool visible(Runtime& s,Address pawn,Address target,Vector eye,Vector point) {
    alignas(16) std::array<std::byte,0x48> filter{};
    alignas(16) std::array<std::byte,0x30> ray{};
    struct Result {std::array<std::byte,8> surface{};Address entity{};std::array<std::byte,0x100> rest{};} result;
    using Filter=void(__fastcall*)(void*,Address,std::uint64_t,std::uint8_t,int);
    using Trace=bool(__fastcall*)(Address,void*,const Vector*,const Vector*,const void*,Result*);
    const auto manager=memory::read<Address>(s.client+sdk::binding_values::layout_trace_manager).value_or(0);
    if(!manager)return false;
    reinterpret_cast<Filter>(s.client+sdk::binding_values::combat_filter_rva)(filter.data(),pawn,0x1c300b,3,7);
    return reinterpret_cast<Trace>(s.client+sdk::binding_values::combat_trace_rva)(manager,ray.data(),&eye,&point,filter.data(),&result)&&result.entity==target;
}
std::vector<visuals::Shape> shapes(Runtime& s,Address scene) {
    const int offset=environment_offset(s,"CSkeletonInstance","m_modelState");
    if(offset<0)return {};
    const auto model_state=scene+offset;
    const int handle_offset=environment_offset(s,"CModelState","m_hModel");
    if(handle_offset<0)return {};
    const auto handle=get<Address>(model_state+handle_offset),model=handle?get<Address>(handle):0;
    if(!model)return {};
    using Refresh=void(__fastcall*)(Address,unsigned);
    reinterpret_cast<Refresh>(s.client+sdk::binding_values::layout_refresh_bones)(scene,0x100);
    const auto cache=get<Address>(model_state+sdk::binding_values::layout_bone_cache);
    const auto meshes=get<Address>(model+sdk::binding_values::layout_model_meshes);
    const auto mesh_count=get<int>(model+sdk::binding_values::layout_model_mesh_count);
    if(!cache||!meshes||mesh_count<=0||mesh_count>64)return {};
    const auto mesh=get<Address>(meshes);if(!mesh)return {};
    const int sets=get<int>(mesh+sdk::binding_values::layout_mesh_set_count);
    if(sets<=0||sets>32||!(get<unsigned>(mesh+sdk::binding_values::layout_mesh_set_flags)&0x7fffffff))return {};
    const auto set=get<Address>(mesh+sdk::binding_values::layout_mesh_sets);if(!set)return {};
    const auto count=get<int>(set+sdk::binding_values::layout_set_shape_count);
    const auto array=get<Address>(set+sdk::binding_values::layout_set_shapes);
    if(!array||count<=0||count>32)return {};
    std::vector<visuals::Shape> result;
    struct Reference {Address shape;int mesh{},reserved{};};
    using Resolve=int(__fastcall*)(Address,const Reference*);
    for(int i=0;i<count;++i) {
        const auto shape=array+sdk::binding_values::layout_shape_stride*i;
        if(get<bool>(shape+sdk::binding_values::layout_shape_custom_transform))continue;
        const Reference reference{shape};
        const auto bone=reinterpret_cast<Resolve>(s.client+sdk::binding_values::layout_resolve_bone)(model,&reference);
        if(bone<0||bone>=256)continue;
        const auto transform=get<nospread::BoneTransform>(cache+32*bone);
        const auto minimum=get<Vector>(shape+sdk::binding_values::layout_shape_minimum),maximum=get<Vector>(shape+sdk::binding_values::layout_shape_maximum);
        if(!finite(minimum)||!finite(maximum)||nospread::length(minimum)>1000||nospread::length(maximum)>1000)continue;
        const auto bounds=visuals::local_bounds(minimum,maximum,get<std::uint8_t>(shape+sdk::binding_values::layout_shape_type),get<float>(shape+sdk::binding_values::layout_shape_radius));if(!bounds)continue;
        const auto lo=(*bounds)[0],hi=(*bounds)[1];
        visuals::Shape value;value.group=get<int>(shape+sdk::binding_values::layout_shape_group);
        const auto center=nospread::transform_point(transform,(minimum+maximum)*.5f);
        if(!center)continue;
        value.center=*center;value.bone=transform.position;
        bool valid=true;
        for(int j=0;j<8;++j){const auto p=nospread::transform_point(transform,{j&1?hi.x:lo.x,j&2?hi.y:lo.y,j&4?hi.z:lo.z});if(!p){valid=false;break;}value.corners[j]=*p;}
        if(valid)result.push_back(value);
    }
    return result;
}
}
void update_player_visuals(Runtime& s) {
    const auto& config=s.requested.player_visuals;
    const auto clear=[&]{visuals::snapshot.store(nullptr);visuals::native::stop();s.player_sounds.clear();};
    if(s.stopping||!visuals::any_esp_enabled(config)){clear();return;}
    const auto controller=memory::read<Address>(s.client+bindings::local_controller).value_or(0);
    const auto local=controller?entity(s,memory::read<std::uint32_t>(controller+s.fields.pawn).value_or(0xffffffff)):0;
    if(!local||field<int>(s,local,"C_BaseEntity","m_iHealth")<=0){clear();return;}
    const auto local_scene=field<Address>(s,local,"C_BaseEntity","m_pGameSceneNode");
    const auto origin=field<Vector>(s,local_scene,"CGameSceneNode","m_vecAbsOrigin");
    const auto eye=origin+field<Vector>(s,local,"C_BaseModelEntity","m_vecViewOffset");
    const int team=field<std::uint8_t>(s,local,"C_BaseEntity","m_iTeamNum");
    const auto matrix=memory::read<visuals::Matrix>(s.client+sdk::binding_values::client_view_matrix);
    if(!local_scene||!finite(origin)||!finite(eye)||!matrix||team<2||team>3){clear();return;}
    for(const auto& row:*matrix)for(float v:row)if(!std::isfinite(v)){clear();return;}
    auto next=std::make_shared<visuals::Snapshot>();next->matrix=*matrix;next->settings=config;next->sampled=GetTickCount64();
    const auto chunks=memory::read<Address>(s.client+bindings::entity_chunks).value_or(0),chunk=chunks?memory::read<Address>(chunks).value_or(0):0;
    if(!chunk){clear();return;}
    auto effects=std::make_shared<visuals::native::Frame>();effects->sampled=next->sampled;
    if(config.chams)effects->materials=visuals::native::prepare_materials(config.chams_material);
    std::set<std::uint32_t> active_sounds;
    for(unsigned i=1;i<=64;++i)try {
        const auto identity=memory::read<EntityIdentity>(chunk+i*sizeof(EntityIdentity));
        if(!identity||(identity->handle&0x7fff)!=i||!identity->entity||identity->entity==controller)continue;
        if(get<Address>(identity->entity)!=s.client+sdk::binding_values::layout_controller_vtable)continue;
        const auto pawn_handle=field<std::uint32_t>(s,identity->entity,"CCSPlayerController","m_hPlayerPawn",0xffffffff);
        const auto pawn=entity(s,pawn_handle);if(!pawn||pawn==local)continue;
        visuals::Player p;p.health=field<int>(s,pawn,"C_BaseEntity","m_iHealth");if(p.health<=0||p.health>1000)continue;
        const int player_team=field<std::uint8_t>(s,pawn,"C_BaseEntity","m_iTeamNum");if(player_team<2||player_team>3)continue;
        p.enemy=player_team!=team;const auto target_config=visuals::for_target(config,p.enemy);if(!target_config.enabled)continue;
        const auto scene=field<Address>(s,pawn,"C_BaseEntity","m_pGameSceneNode");
        if(!scene||field<bool>(s,scene,"CGameSceneNode","m_bDormant",true))continue;
        p.origin=field<Vector>(s,scene,"CGameSceneNode","m_vecAbsOrigin");if(!finite(p.origin))continue;
        p.distance=nospread::length(p.origin-origin)*.0254f;if(p.distance>std::max({config.max_distance,config.box?config.box_max_distance:0.f,config.chams?config.chams_max_distance:0.f}))continue;
        const auto collision=field<Address>(s,pawn,"C_BaseEntity","m_pCollision");
        if(!collision)continue;
        p.minimum=field<Vector>(s,collision,"CCollisionProperty","m_vecMins");p.maximum=field<Vector>(s,collision,"CCollisionProperty","m_vecMaxs");
        if(!finite(p.minimum)||!finite(p.maximum)||p.maximum.z<=p.minimum.z)continue;
        const auto view=field<Vector>(s,pawn,"C_BaseModelEntity","m_vecViewOffset");
        p.visible=visible(s,local,pawn,eye,p.origin+view);
        if(target_config.name){const int offset=environment_offset(s,"CBasePlayerController","m_iszPlayerName");if(offset>=0){std::array<char,128> name{};if(memory::read(identity->entity+offset,std::as_writable_bytes(std::span{name}))){name.back()=0;p.name=name.data();}}}
        p.armor=field<int>(s,pawn,"C_CSPlayerPawn","m_ArmorValue");
        const auto items=field<Address>(s,pawn,"C_BasePlayerPawn","m_pItemServices");
        p.flags[0]=field<bool>(s,pawn,"C_CSPlayerPawn","m_bIsDefusing");p.flags[1]=field<bool>(s,items,"CCSPlayer_ItemServices","m_bHasDefuser");
        p.helmet=field<bool>(s,items,"CCSPlayer_ItemServices","m_bHasHelmet");
        p.flags[2]=field<float>(s,pawn,"C_CSPlayerPawnBase","m_flFlashDuration")>0;
        p.flags[3]=field<bool>(s,pawn,"C_CSPlayerPawn","m_bIsScoped");
        const auto services=field<Address>(s,pawn,"C_BasePlayerPawn","m_pWeaponServices");
        const auto weapon=entity(s,field<std::uint32_t>(s,services,"CPlayer_WeaponServices","m_hActiveWeapon",0xffffffff));
        p.flags[4]=field<bool>(s,weapon,"C_CSWeaponBase","m_bInReload");
        const auto money=field<Address>(s,identity->entity,"CCSPlayerController","m_pInGameMoneyServices");
        p.money=field<int>(s,money,"CCSPlayerController_InGameMoneyServices","m_iAccount",-1);p.flags[6]=p.money>=0;
        const auto hostage=field<Address>(s,pawn,"C_CSPlayerPawn","m_pHostageServices");
        p.flags[7]=field<std::uint32_t>(s,hostage,"CCSPlayer_HostageServices","m_hCarriedHostage",0xffffffff)!=0xffffffff;
        p.flags[8]=p.armor>0;
        if(config.sounds&&p.enemy){p.sound_age=s.player_sounds[pawn_handle].sample(pawn,field<float>(s,pawn,"C_CSPlayerPawn","m_flEmitSoundTime",-1),GetTickCount64());active_sounds.insert(pawn_handle);}
        if(services&&target_config.flags[5]){
            const auto vector=services+s.fields.weapons;
            const auto count=memory::read<std::uint64_t>(vector).value_or(0),handles=memory::read<Address>(vector+8).value_or(0);
            if(count<=64&&handles)for(std::uint64_t j=0;j<count;++j){const auto held=entity(s,memory::read<std::uint32_t>(handles+j*4).value_or(0xffffffff));if(held&&memory::read<std::uint16_t>(held+s.fields.attributes+s.fields.item+s.fields.definition).value_or(0)==49){p.flags[5]=true;p.planting=field<bool>(s,held,"C_C4","m_bStartedArming");break;}}
        }
        if(weapon){
            const auto item=environment_offset(s,"C_EconEntity","m_AttributeManager"),embedded=environment_offset(s,"C_AttributeContainer","m_Item"),definition=environment_offset(s,"C_EconItemView","m_iItemDefinitionIndex");
            if(item>=0&&embedded>=0&&definition>=0){const auto id=memory::read<std::uint16_t>(weapon+item+embedded+definition).value_or(0);p.definition=id;if(const auto* w=find_weapon(id))p.weapon=w->name;if(id==49)p.flags[5]=true;}
            p.clip=field<int>(s,weapon,"C_BasePlayerWeapon","m_iClip1",-1);
            const auto vdata=memory::read<Address>(weapon+sdk::binding_values::layout_weapon_vdata).value_or(0);
            p.max_clip=field<int>(s,vdata,"CBasePlayerWeaponVData","m_iMaxClip1");
        }
        if(target_config.skeleton||target_config.hitboxes)try {p.shapes=shapes(s,scene);}catch(const std::exception&){}
        if((config.glow||config.chams)&&p.enemy){
            const int glow=environment_offset(s,"C_BaseModelEntity","m_Glow");
            const auto pawn_chunk=memory::read<Address>(chunks+((pawn_handle&0x7fff)>>9)*sizeof(Address)).value_or(0);
            if(glow>=0&&pawn_chunk){
                visuals::native::Target target{pawn,pawn_chunk+(pawn_handle&0x1ff)*sizeof(EntityIdentity),pawn+glow,pawn_handle,p.visible?config.enemy_color:config.hidden_color};
                const auto flags=memory::read<std::uint32_t>(target.identity+sdk::binding_values::layout_identity_flags).value_or(0);
                target.scene_handle=(((pawn_handle>>15)-(flags&1))<<15)|(pawn_handle&0x7fff);
                target.glow_enabled=config.glow&&p.distance<=config.max_distance&&(!config.visible_only||p.visible);target.chams=config.chams&&p.distance<=config.chams_max_distance;
                target.visible_chams=config.chams_visible||config.chams_visible_only;target.hidden_chams=!config.chams_visible_only;
                target.original_material=config.chams_material==2;
                target.visible_color=config.chams_color;target.hidden_color=config.chams_hidden_color;
                target.visible_color[3]*=config.chams_opacity;target.hidden_color[3]*=config.chams_opacity;
                effects->targets.push_back(target);
            }
        }
        next->players.push_back(std::move(p));
    }catch(const std::exception&){continue;}
    visuals::native::frame.store(std::move(effects));visuals::snapshot.store(std::move(next));
    if(config.chams)visuals::native::report();
    for(auto it=s.player_sounds.begin();it!=s.player_sounds.end();)if(!active_sounds.contains(it->first))it=s.player_sounds.erase(it);else ++it;
}
bool guarded_player_visuals(Runtime* s) {
    __try {update_player_visuals(*s);return true;}
    __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
