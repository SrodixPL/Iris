#include "core/features/world/esp.hpp"
#include "core/features/runtime.hpp"
#include "core/sdk/entities.hpp"
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
void clear(){candidates.clear();session_chunks=session_pawn=0;session_handle=0xffffffff;scan_at=0;world::esp::stop();}
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
    auto frame=std::make_shared<world::esp::Snapshot>();frame->matrix=*matrix;frame->settings=settings;frame->sampled=now;
    for(auto it=candidates.begin();it!=candidates.end();){
        const auto& candidate=*it;
        if(entity(state,candidate.handle)!=candidate.address){it=candidates.erase(it);continue;}
        ++it;if(!enabled(settings,candidate.kind))continue;
        const auto scene=memory::read<Address>(candidate.address+state.fields.scene).value_or(0);
        if(!scene||memory::read<bool>(scene+dormant_field).value_or(true))continue;
        const auto origin=memory::read<visuals::Vector>(scene+origin_field);
        if(!origin||!nospread::finite(*origin))continue;
        std::string label=candidate.label;
        if(candidate.kind==Kind::Weapon||candidate.label=="C4"){
            const auto owner=memory::read<std::uint32_t>(candidate.address+state.fields.owner);
            // An unresolved valid owner may be dormant or between network
            // updates; it is not evidence that this weapon was dropped.
            if(!owner || *owner!=0xffffffff)continue;
            if(candidate.kind==Kind::Weapon){
                const auto definition=memory::read<std::uint16_t>(candidate.address+state.fields.attributes+state.fields.item+state.fields.definition);
                const auto weapon=definition?find_weapon(*definition):nullptr;if(!weapon)continue;
                label=weapon->name;
            }
        }
        // Verify identity again after copying fields; do not publish a recycled entity.
        if(entity(state,candidate.handle)==candidate.address)frame->labels.push_back({*origin,candidate.kind,std::move(label)});
    }
    world::esp::snapshot.store(std::move(frame));
}
bool guarded_world_visuals(Runtime* state){
    __try {update_world_visuals(*state);return true;}
    __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){clear();return false;}
}
}
namespace iris::world::esp {
void stop() noexcept {snapshot.store(nullptr);}
bool draw(){
    const auto frame=snapshot.load();const auto now=GetTickCount64();
    if(!frame||now<frame->sampled||now-frame->sampled>150)return false;
    const auto size=ImGui::GetIO().DisplaySize;auto* draw=ImGui::GetBackgroundDrawList();bool drawn=false;
    for(const auto& label:frame->labels){
        const auto projected=visuals::project(frame->matrix,label.origin,size.x,size.y);
        if(!projected||projected->x<0||projected->x>size.x||projected->y<0||projected->y>size.y)continue;
        const auto& settings=frame->settings;
        const auto& color=label.kind==Kind::Weapon?settings.weapons_color:label.kind==Kind::Hostage?settings.hostages_color:label.kind==Kind::Grenade?settings.grenades_color:settings.bomb_color;
        if(color[3]<=0)continue;
        const auto text_size=ImGui::CalcTextSize(label.text.c_str());
        const ImVec2 position{projected->x-text_size.x*.5f,projected->y-text_size.y*.5f};
        draw->AddText({position.x+1,position.y+1},ImGui::ColorConvertFloat4ToU32({0,0,0,color[3]}),label.text.c_str());
        draw->AddText(position,ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],color[3]}),label.text.c_str());drawn=true;
    }
    return drawn;
}
}
