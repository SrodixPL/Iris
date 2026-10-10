#include "core/features/player/visual_native.hpp"
#include "core/features/player/visual_materials.hpp"
#include "core/sdk/binding_values.hpp"
#include "core/sdk/entities.hpp"
#include "core/sdk/client.hpp"
#include "core/sdk/world_bindings.hpp"
#include "core/sdk/visual_bindings.hpp"
#include "util/memory.hpp"
#include "core/log.hpp"
#include <cstdio>
#include <Windows.h>


namespace iris::visuals::native {
namespace {
using Address=std::uintptr_t;
using Draw=void(__fastcall*)(Address,Address,Address,int,Address,Address,Address);
Draw draw_original{};
Address tier{},materials_module{};
std::array<Address,8> material_handles{};
bool materials_attempted{};
std::atomic<std::uint64_t> submitted{},matched{},passes{};
std::vector<std::unique_ptr<hooks::Hook>> installed;
bool current(const Frame& value,const Target& target) noexcept {
    const auto now=GetTickCount64();
    if(now<value.sampled||now-value.sampled>250)return false;
    const auto identity=memory::read<sdk::EntityIdentity>(target.identity);
    return identity && identity->entity==target.pawn && identity->handle==target.handle;
}
std::uint32_t packed(const std::array<float,4>& color) noexcept {
    std::uint32_t result{};for(unsigned i=0;i<4;++i)result|=static_cast<unsigned>(color[i]*255.f+.5f)<<(i*8);
    return result;
}
void __fastcall draw_hook(Address descriptor,Address context,Address meshes,int count,Address view,Address layer,Address stats) {
    const auto value=frame.load();
    if(!value||value->targets.empty()||count<=0||count>4096)return draw_original(descriptor,context,meshes,count,view,layer,stats);
    submitted.fetch_add(count,std::memory_order_relaxed);
    int pending{};
    for(int i=0;i<count;++i){
        const auto address=meshes+sizeof(sdk::visual_bindings::Primitive)*i;
        const auto primitive=memory::read<sdk::visual_bindings::Primitive>(address);
        const auto owner=primitive?memory::read<std::uint32_t>(primitive->object+sdk::binding_values::layout_scene_owner):std::nullopt;
        const Target* target{};
        if(owner)for(const auto& candidate:value->targets)
            if(candidate.chams&&candidate.scene_handle==*owner&&current(*value,candidate)){target=&candidate;break;}
        const bool hidden=target&&target->hidden_chams&&value->materials[1]&&target->hidden_color[3]>0;
        const bool visible=target&&target->visible_chams&&(target->original_material||value->materials[0])&&target->visible_color[3]>0;
        if(!hidden&&!visible)continue;
        if(i>pending)draw_original(descriptor,context,meshes+sizeof(*primitive)*pending,i-pending,view,layer,stats);
        matched.fetch_add(1,std::memory_order_relaxed);
        const auto pass=[&](Address material,const std::array<float,4>& color){
            auto mesh=*primitive;
            if(material)mesh.material=mesh.material_copy=material;
            mesh.color=packed(color);mesh.opacity=1.f;
            passes.fetch_add(1,std::memory_order_relaxed);
            draw_original(descriptor,context,reinterpret_cast<Address>(&mesh),1,view,layer,stats);
        };
        if(hidden)pass(value->materials[1],target->hidden_color);
        if(visible)pass(target->original_material?0:value->materials[0],target->visible_color);
        else draw_original(descriptor,context,address,1,view,layer,stats);
        pending=i+1;
    }
    if(pending<count)draw_original(descriptor,context,meshes+sizeof(sdk::visual_bindings::Primitive)*pending,count-pending,view,layer,stats);
}
template<class T> void install(hooks::Runtime& runtime,Address target,void* callback,T& original) {
    auto hook=std::make_unique<hooks::Hook>(runtime,reinterpret_cast<void*>(target),callback);
    original=hook->original<T>();hook->enable();installed.push_back(std::move(hook));
}
}
void bind_chams(hooks::Runtime& runtime) {
    if(!installed.empty())return;
    const auto scene=GetModuleHandleW(L"scenesystem.dll"),parser=GetModuleHandleW(L"tier0.dll"),material=GetModuleHandleW(L"materialsystem2.dll");
    if(scene&&parser&&material&&sdk::sha256_file(scene)==sdk::world_bindings::scene_sha256&&
        sdk::sha256_file(parser)==sdk::visual_bindings::tier_sha256&&sdk::sha256_file(material)==sdk::visual_bindings::material_sha256){
        tier=reinterpret_cast<Address>(parser);materials_module=reinterpret_cast<Address>(material);
        install(runtime,reinterpret_cast<Address>(scene)+sdk::binding_values::scene_player_draw,reinterpret_cast<void*>(&draw_hook),draw_original);
        overlay_log("Player chams: animated mesh draw hook installed (CAnimatableSceneObjectDesc).");
    }else overlay_log("Player chams: native module fingerprint mismatch; material passes disabled.");
}
std::array<Address,2> prepare_materials(int style) {
    if(!draw_original||!tier||!materials_module)return {};
    if(!materials_attempted){
        materials_attempted=true;
        using Construct=void*(__fastcall*)(void*,std::uint8_t,std::uint8_t);
        struct Id {const char* name;std::uint64_t low,high;};
        using Load=bool(__fastcall*)(void*,void*,const char*,const Id*,const char*,unsigned);
        using Create=Address*(__fastcall*)(void*,Address*,const char*,void*,unsigned,bool);
        // Parsed trees and material bindings remain pinned with the callbacks.
        static std::array<std::array<std::uint64_t,2>,8> trees{};
        const Id id{"generic",0x41b818518343427e,0xb5f447c23c0cdf8c};
        for(unsigned i=0;i<8;++i){
            const auto text=material_text(i/2,(i&1)!=0);
            auto* tree=trees[i].data();
            if(reinterpret_cast<Construct>(tier+sdk::binding_values::tier_kv3_construct)(tree,1,6)!=tree)continue;
            if(!reinterpret_cast<Load>(tier+sdk::binding_values::tier_kv3_load)(tree,nullptr,text.c_str(),&id,nullptr,0))continue;
            constexpr const char* names[]{"materials/iris/player_flat_visible.vmat","materials/iris/player_flat_hidden.vmat","materials/iris/player_glow_visible.vmat","materials/iris/player_glow_hidden.vmat","materials/iris/player_rim_visible.vmat","materials/iris/player_rim_hidden.vmat","materials/iris/player_solid_visible.vmat","materials/iris/player_solid_hidden.vmat"};
            reinterpret_cast<Create>(materials_module+sdk::binding_values::material_create)(nullptr,&material_handles[i],names[i],tree,0,true);
        }
        overlay_log(material_handles[0]&&material_handles[1]?"Player chams: visible and depth-independent materials created.":"Player chams: material creation failed; retaining native rendering.");
    }
    std::array<Address,2> result{};
    const unsigned base=style==1?2:style==3?4:style==4?6:0;
    for(unsigned i=0;i<2;++i){const auto handle=material_handles[base+i]?material_handles[base+i]:material_handles[i];if(handle)result[i]=memory::read<Address>(handle).value_or(0);}
    return result;
}
void report() {
    static std::uint64_t next{};const auto now=GetTickCount64();if(now<next)return;next=now+10000;
    const auto value=frame.load();char text[256];
    std::snprintf(text,sizeof(text),"Player chams: targets=%zu material_data=%p/%p submitted=%llu matched=%llu passes=%llu",value?value->targets.size():0,
        reinterpret_cast<void*>(value?value->materials[0]:0),reinterpret_cast<void*>(value?value->materials[1]:0),
        static_cast<unsigned long long>(submitted.load()),static_cast<unsigned long long>(matched.load()),static_cast<unsigned long long>(passes.load()));
    overlay_log(text);
}

}
