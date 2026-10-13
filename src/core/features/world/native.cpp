#include "core/sdk/binding_values.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/client.hpp"
#include "util/memory.hpp"
#include "core/log.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <Windows.h>

namespace iris::world {
namespace {
namespace wb=sdk::world_bindings;
using Address=std::uintptr_t;
using Vector=wb::ShaderValue;
using Shader=wb::SetShaderVector;
using Integer=wb::SetShaderInteger;
using Fog=wb::ClearFog;
using Aggregate=wb::Aggregate;
using Light=wb::Light;
using Smoke=wb::Smoke;
using SmokePrepare=wb::SmokePrepare;
std::atomic<std::shared_ptr<const cosmetics::Environment>> settings;
std::atomic<bool> active{},scene_ready{},ready{};
std::vector<std::unique_ptr<hooks::Hook>> installed;
Shader shader_original{},post_original{};
Integer integer_set{};
wb::GetShaderInteger integer_get{};
Fog fog_original{};
Aggregate aggregate_original{};
Light light_original{};
Smoke smoke_original{};
SmokePrepare smoke_prepare_original{};
Address scene{},queue{};

// These copies borrow only the object passed by the reviewed synchronous
// native callback. No pointer or temporary override escapes that callback.
template<class T> std::optional<T> borrow(Address address){T value{};if(!memory::read_local(address,std::as_writable_bytes(std::span{&value,std::size_t{1}})))return {};return value;}
template<class T> bool replace(Address address,const T& value){return memory::write_local(address,std::as_bytes(std::span{&value,std::size_t{1}}));}
std::shared_ptr<const cosmetics::Environment> current() {
    return active.load() ? settings.load() : nullptr;
}
Vector scalar(float value) {return {value,value,value,value};}
float linear(float channel) {return channel<=.04045f ? channel/12.92f : std::pow((channel+.055f)/1.055f,2.4f);}
std::uint64_t fog_flag_hash(int value) {
    std::uint64_t key=(std::uint64_t{0x6e0fad7e}<<32)|static_cast<std::uint32_t>(value);
    key=(key^(key>>30))*0xbf58476d1ce4e5b9;
    key=(key^(key>>27))*0x94d049bb133111eb;
    return key^(key>>31);
}
void fog_flag(void* map,int value) {
    const auto address=reinterpret_cast<Address>(map);
    auto* integers=reinterpret_cast<void*>(address+wb::integer_map);
    const int previous=integer_get(integers,0x6e0fad7e,0);
    if(previous==value)return;
    const auto hash=borrow<std::uint64_t>(address+wb::permutation_hash);if(!hash)return;
    integer_set(integers,0x6e0fad7e,value);
    // The raw setter does not update the shader permutation key; native fog does.
    (void)replace(address+wb::permutation_hash,*hash^fog_flag_hash(previous)^fog_flag_hash(value));
}
bool parameter(const cosmetics::Environment& w,std::uint32_t hash,Vector& value) {
    if(w.fog || w.remove_fog){
        switch(hash){
        case 0x4b01ff63:value={w.fog_start,std::max(w.fog_start+1,w.fog_distance),0,0};return true;
        case 0x0aa49c2a:value={w.remove_fog ? 0.f : w.fog_density,w.fog_falloff,0,0};return true;
        case 0xfbf6448d:value={linear(w.fog_color[0]),linear(w.fog_color[1]),linear(w.fog_color[2]),0};return true;
        }
    }
    if(w.dof_override && hash==0x2acab07c){value={w.dof_near_blurry,w.dof_near_crisp,w.dof_far_crisp,w.dof_far_blurry};return true;}
    if(w.bloom_override){
        switch(hash){
        case 0x565eaf76:value=scalar(.3f+w.bloom*1.2f);return true;
        case 0xba98a9b0:value=scalar(1.5f-w.bloom*1.2f);return true;
        case 0x2ae72b37:value=scalar(.5f+w.bloom*1.5f);return true;
        case 0xb692902e:value=scalar(.2f+w.bloom*.6f);return true;
        case 0x1313a424:value=scalar(.1f+w.bloom*.4f);return true;
        }
    }
    if(w.wetness_override){
        switch(hash){
        case 0x374c1b3c:value=scalar(w.wetness>0 ? 1.f : 0.f);return true;
        case 0x0f592812:value=scalar(w.wetness);return true;
        case 0x2dbee393:value=scalar(static_cast<float>(GetTickCount64()%10000000)/1000.f);return true;
        }
    }
    return false;
}
Address __fastcall shader_hook(void* map,std::uint32_t hash,const Vector* original) {
    auto w=current();alignas(16) Vector value{};
    return shader_original(map,hash,w && parameter(*w,hash,value) ? &value : original);
}
Address __fastcall post_hook(void* map,std::uint32_t hash,const Vector* original) {
    auto w=current();alignas(16) Vector value{};
    if(w && w->dof_override && hash!=0x2acab07c){
        alignas(16) const Vector ranges{w->dof_near_blurry,w->dof_near_crisp,w->dof_far_crisp,w->dof_far_blurry};
        post_original(map,0x2acab07c,&ranges);
    }
    return post_original(map,hash,w && parameter(*w,hash,value) ? &value : original);
}
Address __fastcall fog_hook(void* map,void* mode) {
    // This native callback clears stale parameters as well as fog. Keep that work.
    const auto result=fog_original(map,mode);
    auto w=current();if(!w || !map || (!w->fog && !w->remove_fog))return result;
    if(w->remove_fog){fog_flag(map,0);return result;}
    alignas(16) const Vector distance{w->fog_start,std::max(w->fog_start+1,w->fog_distance),0,0};
    alignas(16) const Vector density{w->fog_density,w->fog_falloff,0,0};
    alignas(16) const Vector color{linear(w->fog_color[0]),linear(w->fog_color[1]),linear(w->fog_color[2]),0};
    shader_original(map,0x4b01ff63,&distance);shader_original(map,0x0aa49c2a,&density);shader_original(map,0xfbf6448d,&color);
    fog_flag(map,1);
    return result;
}
void __fastcall aggregate_hook(Address owner,Address context,Address object) {
    aggregate_original(owner,context,object);
    auto w=current();if(!w || !w->world_override || !object)return;
    const auto data=borrow<Address>(object+8).value_or(0);
    const auto output=borrow<Address>(queue).value_or(0);
    const auto base=output ? borrow<Address>(output+iris::sdk::binding_values::layout_post_output).value_or(0) : 0;
    const auto end=output ? borrow<Address>(output+8).value_or(0) : 0;
    if(!data || !base)return;
    const int count=borrow<int>(data+4).value_or(0),index=borrow<int>(data+wb::aggregate_index).value_or(-1);
    if(count<=0 || count>65536 || index<0 || index>4194304-count)return;
    if(end<base || (static_cast<Address>(index)+count)*wb::record_stride>end-base)return;
    for(int i=0;i<count;++i){
        const Address address=base+(static_cast<Address>(index)+i)*wb::record_stride;
        auto color=borrow<std::array<std::uint8_t,4>>(address);if(!color)break;
        for(int c=0;c<3;++c)(*color)[c]=static_cast<std::uint8_t>(std::clamp((*color)[c]*w->world_color[c],0.f,255.f));
        (void)replace(address,*color);
    }
}
Address __fastcall light_hook(Address owner,Address object,Address context) {
    auto w=current();if(!w || !w->lighting_override || !object)return light_original(owner,object,context);
    const auto baseline=borrow<std::array<float,3>>(object+wb::light_color);if(!baseline)return light_original(owner,object,context);
    auto color=*baseline;for(int i=0;i<3;++i)color[i]*=w->lighting_color[i]*w->lighting_intensity;
    if(!replace(object+wb::light_color,color))return light_original(owner,object,context);
    const auto result=light_original(owner,object,context);
    if(borrow<std::array<float,3>>(object+wb::light_color)==color)(void)replace(object+wb::light_color,*baseline);
    return result;
}
void __fastcall smoke_hook(Address a,Address b,int c,int d,Address e,Address f) {
    auto w=current();if(w && w->remove_smoke)return;
    smoke_original(a,b,c,d,e,f);
}
void __fastcall smoke_prepare_hook(Address object) {
    auto w=current();
    if(!w || !w->smoke_override){smoke_prepare_original(object);return;}
    const auto original=borrow<std::array<float,3>>(object+wb::smoke_color);
    auto color=w->smoke_color;for(auto& channel:color)channel*=255.f;
    const bool written=original && replace(object+wb::smoke_color,color);
    smoke_prepare_original(object);
    if(written && borrow<std::array<float,3>>(object+wb::smoke_color)==color)(void)replace(object+wb::smoke_color,*original);
}
template<class T> void install(hooks::Runtime& runtime,Address target,void* callback,T& original) {
    auto hook=std::make_unique<hooks::Hook>(runtime,reinterpret_cast<void*>(target),callback);
    original=hook->original<T>();hook->enable();installed.push_back(std::move(hook));
}
}
void bind(Address client,hooks::Runtime& runtime) {
    if(!installed.empty())return;
    installed.reserve(8);
    integer_set=reinterpret_cast<Integer>(client+wb::shader_integer);
    integer_get=reinterpret_cast<wb::GetShaderInteger>(client+wb::read_shader_integer);
    install(runtime,client+wb::shader_vector,reinterpret_cast<void*>(&shader_hook),shader_original);
    install(runtime,client+wb::clear_fog,reinterpret_cast<void*>(&fog_hook),fog_original);
    install(runtime,client+wb::render_smoke,reinterpret_cast<void*>(&smoke_hook),smoke_original);
    install(runtime,client+wb::smoke_prepare,reinterpret_cast<void*>(&smoke_prepare_hook),smoke_prepare_original);
    const auto engine=GetModuleHandleW(L"engine2.dll");
    if(engine && sdk::sha256_file(engine)==wb::engine_sha256)
        install(runtime,reinterpret_cast<Address>(engine)+wb::post_vector,reinterpret_cast<void*>(&post_hook),post_original);
    const auto module=GetModuleHandleW(L"scenesystem.dll");
    if(module && sdk::sha256_file(module)==wb::scene_sha256){
        // +20 owns 16-byte records; this aggregate pass uses the adjacent 32-byte queue.
        scene=reinterpret_cast<Address>(module);queue=scene+wb::aggregate_queue;
        install(runtime,scene+wb::aggregate,reinterpret_cast<void*>(&aggregate_hook),aggregate_original);
        install(runtime,scene+wb::light,reinterpret_cast<void*>(&light_hook),light_original);
        scene_ready=true;
    }else overlay_log("World rendering: scenesystem fingerprint unavailable; native tint and lighting disabled.");
    ready=true;
    const auto value=settings.load();active=value && value->enabled;
}
void publish(const cosmetics::Environment& value) {
    settings.store(std::make_shared<const cosmetics::Environment>(value));active=ready && value.enabled;
}
void stop() noexcept {active=false;}
bool scene_verified() noexcept {return scene_ready;}
}
