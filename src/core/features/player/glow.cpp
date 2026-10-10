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
using Predicate=bool(__fastcall*)(Address);
using Color=void(__fastcall*)(Address,float*);
Predicate predicate_original{};
Color color_original{};
std::vector<std::unique_ptr<hooks::Hook>> installed;
const Target* find(const Frame& value,Address property) noexcept {
    const auto now=GetTickCount64();
    if(now<value.sampled || now-value.sampled>250)return nullptr;
    for(const auto& target:value.targets)if(target.glow_enabled && target.glow==property){
        const auto identity=memory::read<sdk::EntityIdentity>(target.identity);
        if(identity && identity->entity==target.pawn && identity->handle==target.handle &&
            memory::read<Address>(property+sdk::binding_values::layout_glow_owner)==target.pawn)return &target;
    }
    return nullptr;
}
bool __fastcall predicate_hook(Address property) {
    const auto value=frame.load();
    return (value && find(*value,property)) || predicate_original(property);
}
void __fastcall color_hook(Address property,float* color) {
    const auto value=frame.load();
    if(value)if(const auto* target=find(*value,property)){
        if(memory::write(reinterpret_cast<Address>(color),target->color))return;
    }
    color_original(property,color);
}
template<class T> void install(hooks::Runtime& runtime,Address target,void* callback,T& original) {
    auto hook=std::make_unique<hooks::Hook>(runtime,reinterpret_cast<void*>(target),callback);
    original=hook->original<T>();hook->enable();installed.push_back(std::move(hook));
}
}
void bind_glow(Address client,hooks::Runtime& runtime) {
    if(!installed.empty())return;
    installed.reserve(2);
    install(runtime,client+sdk::binding_values::client_glow_predicate,reinterpret_cast<void*>(&predicate_hook),predicate_original);
    install(runtime,client+sdk::binding_values::client_glow_color,reinterpret_cast<void*>(&color_hook),color_original);
    overlay_log("Player glow: native eligibility and color hooks installed.");
}

}
