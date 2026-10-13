#include "core/features/world/esp.hpp"
#include "core/features/runtime.hpp"
#include "core/sdk/entities.hpp"
#include "core/features/player/visual_native.hpp"
#include "core/features/player/esp/pipeline.hpp"
#include "ext/imgui/imgui.h"
#include <optional>
#include <string_view>

namespace iris::cosmetics::detail {
namespace {
using world::esp::Kind;
struct Candidate {Address address{};std::uint32_t handle{};Kind kind{};std::string label;};
// One game-thread cache, bounded and checked against the full serial-bearing
// handle before every sample. The render snapshot contains no game pointers.
std::vector<Candidate> candidates;
Address session_chunks{},session_pawn{};
std::uint32_t session_handle{0xffffffff};
ULONGLONG scan_at{};
constexpr std::size_t maximum_labels=512;

std::optional<std::pair<Kind,std::string>> classify(std::string_view name){
    if(name=="weapon_c4")return std::pair{Kind::Bomb,std::string{"C4"}};
    if(name.starts_with("weapon_"))return std::pair{Kind::Weapon,std::string{}};
    if(name=="hostage_entity")return std::pair{Kind::Hostage,std::string{"Hostage"}};
    if(name=="planted_c4")return std::pair{Kind::Bomb,std::string{"Planted C4"}};
    if(name=="hegrenade_projectile")return std::pair{Kind::Grenade,std::string{"HE grenade"}};
    if(name=="flashbang_projectile")return std::pair{Kind::Grenade,std::string{"Flashbang"}};
    if(name=="smokegrenade_projectile")return std::pair{Kind::Grenade,std::string{"Smoke grenade"}};
    if(name=="molotov_projectile")return std::pair{Kind::Grenade,std::string{"Molotov"}};
    if(name=="decoy_projectile")return std::pair{Kind::Grenade,std::string{"Decoy"}};
    return {};
}
bool enabled(const world::esp::Settings& settings,Kind kind){
    switch(kind){case Kind::Weapon:return settings.weapons;case Kind::Hostage:return settings.hostages;
      case Kind::Grenade:return settings.grenades;case Kind::Bomb:return settings.bomb;}
    return false;
}
void clear(){visuals::native::world_frame.store(nullptr);candidates.clear();session_chunks=session_pawn=0;session_handle=0xffffffff;scan_at=0;world::esp::stop();}
void scan(Runtime& state,Address chunks,ULONGLONG now){
    candidates.clear();scan_at=now+1000;
    const int designer=environment_offset(state,"CEntityIdentity","m_designerName");
    if(designer<0)return;
    for(unsigned group=0;group<64 && candidates.size()<maximum_labels;++group){
        const auto chunk=memory::read<Address>(chunks+group*sizeof(Address)).value_or(0);if(!chunk)continue;
        std::array<EntityIdentity,512> identities{};
        if(!memory::read(chunk,std::as_writable_bytes(std::span{identities})))continue;
        for(unsigned index=0;index<identities.size() && candidates.size()<maximum_labels;++index){
            const auto& identity=identities[index];
            if(!identity.entity||identity.handle==0xffffffff||(identity.handle&0x7fff)!=group*512+index)continue;
            const auto bytes=std::as_bytes(std::span{&identity,std::size_t{1}});
            const auto name=memory::read_field<Address>(bytes,designer).value_or(0);if(!name)continue;
            std::array<char,96> text{};
            if(!memory::read(name,std::as_writable_bytes(std::span{text})))continue;
            text.back()=0;
            if(const auto kind=classify(text.data()))candidates.push_back({identity.entity,identity.handle,kind->first,kind->second});
        }
    }
}
}
void clear_world_visuals(Runtime&){clear();}
void update_world_visuals(Runtime& state){
    const auto& settings=state.requested.world_esp;
    if(state.stopping||!settings.active()){clear();return;}
    const auto chunks=memory::read<Address>(state.client+bindings::entity_chunks).value_or(0);
    const auto controller=memory::read<Address>(state.client+bindings::local_controller).value_or(0);
    const auto handle=controller?memory::read<std::uint32_t>(controller+state.fields.pawn).value_or(0xffffffff):0xffffffff;
    const auto pawn=entity(state,handle);
    if(!chunks||!pawn||memory::read<int>(pawn+state.fields.health).value_or(0)<=0){clear();return;}
    if(chunks!=session_chunks||pawn!=session_pawn||handle!=session_handle){clear();session_chunks=chunks;session_pawn=pawn;session_handle=handle;}
    const auto now=GetTickCount64();if(now>=scan_at)scan(state,chunks,now);
    const auto matrix=memory::read<visuals::Matrix>(state.client+sdk::binding_values::client_view_matrix);
    if(!matrix){world::esp::stop();return;}
    for(const auto& row:*matrix)for(float value:row)if(!std::isfinite(value)){world::esp::stop();return;}
    const int origin_field=environment_offset(state,"CGameSceneNode","m_vecAbsOrigin");
    const int dormant_field=environment_offset(state,"CGameSceneNode","m_bDormant");
    if(origin_field<0||dormant_field<0){world::esp::stop();return;}
    auto effects=std::make_shared<visuals::native::Frame>();effects->sampled=now;
    auto frame=std::make_shared<world::esp::Snapshot>();frame->matrix=*matrix;frame->settings=settings;frame->sampled=now;
    for(auto it=candidates.begin();it!=candidates.end();){
        const auto& candidate=*it;
        if(entity(state,candidate.handle)!=candidate.address){it=candidates.erase(it);continue;}
        ++it;if(!enabled(settings,candidate.kind))continue;
        const auto scene=memory::read<Address>(candidate.address+state.fields.scene).value_or(0);
        if(!scene||memory::read<bool>(scene+dormant_field).value_or(true))continue;
        const auto origin=memory::read<visuals::Vector>(scene+origin_field);
        if(!origin||!nospread::finite(*origin))continue;
        std::string label=candidate.label;int definition=candidate.kind==Kind::Bomb?49:0;
        if(candidate.kind==Kind::Weapon||candidate.label=="C4"){
            const auto owner=memory::read<std::uint32_t>(candidate.address+state.fields.owner);
            // An unresolved valid owner may be dormant or between network
            // updates; it is not evidence that this weapon was dropped.
            if(!owner || *owner!=0xffffffff)continue;
            if(candidate.kind==Kind::Weapon){
                const auto item_definition=memory::read<std::uint16_t>(candidate.address+state.fields.attributes+state.fields.item+state.fields.definition);
                const auto weapon=item_definition?find_weapon(*item_definition):nullptr;if(!weapon)continue;
                label=weapon->name;definition=*item_definition;
            }
        }
        // Verify identity again after copying fields; do not publish a recycled entity.
        if(entity(state,candidate.handle)==candidate.address){
            frame->labels.push_back({*origin,candidate.kind,std::move(label),definition});
            if((candidate.kind==Kind::Weapon&&settings.weapons_chams)||(candidate.kind==Kind::Bomb&&settings.bomb_chams)){
                const auto chunk=memory::read<Address>(chunks+((candidate.handle&0x7fff)>>9)*sizeof(Address)).value_or(0);if(!chunk)continue;
                visuals::native::Target target;target.pawn=candidate.address;target.handle=candidate.handle;target.identity=chunk+(candidate.handle&0x1ff)*sizeof(EntityIdentity);
                const auto flags=memory::read<std::uint32_t>(target.identity+sdk::binding_values::layout_identity_flags).value_or(0);
                target.scene_handle=(((candidate.handle>>15)-(flags&1))<<15)|(candidate.handle&0x7fff);
                target.chams=target.visible_chams=true;target.visible_color=candidate.kind==Kind::Weapon?settings.weapons_color:settings.bomb_color;
                effects->materials=visuals::native::prepare_materials(0);effects->targets.push_back(target);
            }
        }
    }
    visuals::native::world_frame.store(std::move(effects));
    world::esp::snapshot.store(std::move(frame));
}
bool guarded_world_visuals(Runtime* state){
    __try {update_world_visuals(*state);return true;}
    __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){clear();return false;}
}
}
namespace iris::world::esp {
void stop() noexcept {snapshot.store(nullptr);}
bool draw(visuals::Icon (*lookup)(void*,int),void* owner){
    const auto current=snapshot.load();const auto ready=visuals::esp::prepared();const auto size=ImGui::GetIO().DisplaySize;
    if(!current||!ready||!ready->world||ready->width!=size.x||ready->height!=size.y||ready->world->settings!=current->settings)return false;
    const auto frame=ready->world;const auto now=GetTickCount64();
    if(!frame||now<frame->sampled||now-frame->sampled>150)return false;
    auto* draw=ImGui::GetBackgroundDrawList();bool drawn=false;
    for(std::size_t index=0;index<frame->labels.size();++index){
        const auto& label=frame->labels[index];const auto projected=ready->labels[index];
        if(!projected||projected->x<0||projected->x>size.x||projected->y<0||projected->y>size.y)continue;
        const auto& settings=frame->settings;
        const auto& color=label.kind==Kind::Weapon?settings.weapons_color:label.kind==Kind::Hostage?settings.hostages_color:label.kind==Kind::Grenade?settings.grenades_color:settings.bomb_color;
        if(color[3]<=0)continue;
        const bool text=label.kind==Kind::Weapon?settings.weapons_text:label.kind==Kind::Bomb?settings.bomb_text:true;
        const bool icon=label.kind==Kind::Weapon?settings.weapons_icon:label.kind==Kind::Bomb?settings.bomb_icon:false;
        const auto tint=ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],color[3]});
        if(icon){
            const auto art=lookup&&label.kind!=Kind::Bomb?lookup(owner,label.definition):visuals::Icon{};
            const float h=ImGui::GetFontSize()*1.5f,w=h*(art.texture?art.aspect:label.kind==Kind::Bomb?.8f:2.f);
            const float x=projected->x-w*.5f,y=projected->y+(text?ImGui::GetFontSize()*.7f:-h*.5f);
            if(art.texture)draw->AddImage(static_cast<ImTextureID>(art.texture),{x,y},{x+w,y+h},{art.u0,art.v0},{art.u1,art.v1},tint);
            else if(label.kind==Kind::Bomb){
                draw->AddRect({x,y+h*.12f},{x+w,y+h},tint,2,0,1.5f);draw->AddRect({x+w*.2f,y+h*.25f},{x+w*.8f,y+h*.42f},tint);
                for(int row=0;row<2;++row)for(int column=0;column<3;++column)draw->AddCircleFilled({x+w*(.25f+.25f*column),y+h*(.6f+.18f*row)},1,tint);
                draw->AddBezierCubic({x+w*.3f,y+h*.12f},{x+w*.1f,y-h*.1f},{x+w*.9f,y-h*.1f},{x+w*.7f,y+h*.12f},tint,1.5f);
            }else {
                draw->AddRectFilled({x+w*.15f,y+h*.3f},{x+w*.7f,y+h*.48f},tint);draw->AddLine({x+w*.7f,y+h*.36f},{x+w,y+h*.36f},tint,2);
                draw->AddTriangleFilled({x,y+h*.25f},{x+w*.2f,y+h*.3f},{x+w*.2f,y+h*.55f},tint);draw->AddQuadFilled({x+w*.35f,y+h*.45f},{x+w*.48f,y+h*.45f},{x+w*.43f,y+h*.8f},{x+w*.3f,y+h*.8f},tint);
            }
            drawn=true;
        }
        if(!text)continue;
        const auto text_size=ImGui::CalcTextSize(label.text.c_str());
        const ImVec2 position{projected->x-text_size.x*.5f,projected->y-text_size.y*.5f};
        draw->AddText({position.x+1,position.y+1},ImGui::ColorConvertFloat4ToU32({0,0,0,color[3]}),label.text.c_str());
        draw->AddText(position,ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],color[3]}),label.text.c_str());drawn=true;
    }
    return drawn;
}
}
