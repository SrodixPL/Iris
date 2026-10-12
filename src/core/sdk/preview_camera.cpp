#include "core/sdk/preview_camera.hpp"
#include "core/sdk/binding_values.hpp"
#include "core/sdk/client.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace iris::sdk::preview {
namespace {
namespace b=binding_values;
struct Key {std::uint32_t hash{};std::int32_t index{-1};const char* name{};};
static_assert(sizeof(Key)==b::layout_kv3_key_size);
static_assert(offsetof(Key,hash)==b::layout_kv3_key_hash && offsetof(Key,index)==b::layout_kv3_key_index &&
    offsetof(Key,name)==b::layout_kv3_key_name && b::layout_kv3_size==16);
enum class Operation {Authored,Copy,Find,Vector,Number,SetArray,SetNumber,Destroy};
struct Call {
    Operation operation{};Address client{},object{},argument{},result{};
    Key key{};std::int32_t hint{-1};float* vector{};double number{};
    std::uint8_t subtype{};bool accepted{};
};
bool invoke(Call* c) {
    __try {
        auto* object=reinterpret_cast<void*>(c->object);
        switch(c->operation) {
        case Operation::Authored:c->result=reinterpret_cast<Address(__fastcall*)(void*)>(c->client+b::client_preview_authored_settings)(object);break;
        case Operation::Copy:reinterpret_cast<void*(__fastcall*)(void*,const void*)>(c->client+b::client_kv3_copy_construct)(object,reinterpret_cast<const void*>(c->argument));break;
        case Operation::Find:c->result=reinterpret_cast<Address(__fastcall*)(const void*,const Key*,std::int32_t*,void*)>(c->client+b::client_kv3_find_member)(object,&c->key,&c->hint,nullptr);break;
        case Operation::Vector:c->accepted=reinterpret_cast<bool(__fastcall*)(const void*,int,float*)>(c->client+b::client_kv3_get_vector)(object,3,c->vector);break;
        case Operation::Number:c->number=reinterpret_cast<double(__fastcall*)(const void*)>(c->client+b::client_kv3_get_double)(object);break;
        case Operation::SetArray:reinterpret_cast<void(__fastcall*)(void*,int,const float*,std::uint8_t)>(c->client+b::client_kv3_set_float_array)(object,3,c->vector,c->subtype);break;
        case Operation::SetNumber:reinterpret_cast<void(__fastcall*)(void*,double,std::uint8_t)>(c->client+b::client_kv3_set_double)(object,c->number,c->subtype);break;
        case Operation::Destroy:reinterpret_cast<void(__fastcall*)(void*)>(c->client+b::client_kv3_destroy)(object);break;
        }
        return true;
    } __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void run(Call& c) {if(!invoke(&c))throw std::runtime_error("Native preview settings faulted; retained ownership requires a host restart.");}
std::uint64_t tag(Address node) {
    const auto value=memory::read<std::uint64_t>(node+b::layout_kv3_tag);
    if(!value)throw std::runtime_error("Native preview settings are unreadable.");
    return *value;
}
unsigned type(Address node) {return static_cast<unsigned>((tag(node)>>b::layout_kv3_type_shift)&b::layout_kv3_type_mask);}
std::uint8_t subtype(Address node) {return static_cast<std::uint8_t>((tag(node)>>b::layout_kv3_subtype_shift)&b::layout_kv3_subtype_mask);}
Address member(Address client,Address root,std::uint32_t hash,Address name) {
    Call c{Operation::Find,client,root};c.key={hash,-1,reinterpret_cast<const char*>(name)};run(c);return c.result;
}
Address camera(Address client,Address root) {return member(client,root,static_cast<std::uint32_t>(b::layout_kv3_hash_camera),client+b::client_kv3_name_camera);}
Address angle(Address client,Address root) {return member(client,root,static_cast<std::uint32_t>(b::layout_kv3_hash_angle),client+b::client_kv3_name_angle);}
Address orbit(Address client,Address root) {return member(client,root,static_cast<std::uint32_t>(b::layout_kv3_hash_orbit_distance),client+b::client_kv3_name_orbit_distance);}
template<std::size_t N>bool name_matches(Address at,const char(&expected)[N]) {
    std::array<char,N> value{};
    return memory::read(at,std::span<std::byte>(reinterpret_cast<std::byte*>(value.data()),N)) && std::memcmp(value.data(),expected,N)==0;
}
}
bool valid_control(const CameraControl& p) noexcept {
    return std::isfinite(p.yaw)&&std::isfinite(p.pitch)&&std::isfinite(p.zoom)&&
        std::abs(p.yaw)<=180 && std::abs(p.pitch)<=75 && p.zoom>=-1.1f && p.zoom<=1.5f;
}
void CameraSettings::require_thread() const {
    if(!client_ || thread_!=GetCurrentThreadId() || client_!=reinterpret_cast<Address>(GetModuleHandleW(L"client.dll")))
        throw std::runtime_error("Preview settings require their owning game thread and reviewed client.");
    if(uncertain_)throw std::runtime_error("Uncertain preview settings ownership requires a host restart.");
}
void CameraSettings::clear() {
    if(!constructed_)return;
    require_thread();uncertain_=true;
    Call c{Operation::Destroy,client_,reinterpret_cast<Address>(root_.data())};run(c);
    uncertain_=constructed_=false;distance_=0;root_={};
}
bool CameraSettings::prepare(Address client,Address scratch) {
    clear();client_=client;thread_=GetCurrentThreadId();require_thread();
    if(memory::read<Address>(scratch).value_or(0)!=client+b::client_preview_item_vtable ||
        !name_matches(client+b::client_kv3_name_camera,"camera") ||
        !name_matches(client+b::client_kv3_name_angle,"angle") ||
        !name_matches(client+b::client_kv3_name_orbit_distance,"orbit_distance"))
        throw std::runtime_error("Preview camera input differs from reviewed bindings.");
    Call source{Operation::Authored,client,scratch};run(source);
    if(!source.result || type(source.result)!=b::layout_kv3_type_object)return false;
    // Mark before a native call: a fault can occur after allocation or mutation.
    constructed_=uncertain_=true;
    Call copy{Operation::Copy,client,reinterpret_cast<Address>(root_.data()),source.result};run(copy);
    uncertain_=false;
    const auto root=reinterpret_cast<Address>(root_.data());const auto cam=camera(client,root);
    const auto a=cam?angle(client,cam):0;const auto d=cam?orbit(client,cam):0;
    if(!a || !d || (type(a)!=b::layout_kv3_type_array && type(a)!=b::layout_kv3_type_string) ||
        (type(d)!=b::layout_kv3_type_double && type(d)!=b::layout_kv3_type_int && type(d)!=b::layout_kv3_type_uint)) {clear();return false;}
    Call vector{Operation::Vector,client,a};vector.vector=angles_.data();run(vector);
    Call number{Operation::Number,client,d};run(number);distance_=number.number;
    if(!vector.accepted || !std::isfinite(distance_) || distance_<=0 || distance_>100000 ||
        !std::isfinite(angles_[0]) || !std::isfinite(angles_[1]) || !std::isfinite(angles_[2])) {clear();return false;}
    return true;
}
void CameraSettings::apply(const CameraControl& p) {
    require_thread();if(!ready() || !valid_control(p))throw std::runtime_error("Invalid native preview camera request.");
    const auto root=reinterpret_cast<Address>(root_.data());const auto cam=camera(client_,root);
    const auto a=cam?angle(client_,cam):0;const auto d=cam?orbit(client_,cam):0;
    if(!a || !d)throw std::runtime_error("Copied preview camera children are missing.");
    auto values=angles_;values[0]+=p.pitch;values[1]+=p.yaw;
    const auto array_subtype=subtype(a);const auto number_subtype=subtype(d);
    uncertain_=true;
    Call set_array{Operation::SetArray,client_,a};set_array.vector=values.data();set_array.subtype=array_subtype;run(set_array);
    // Reacquire after a native edit; never retain a borrowed child across it.
    const auto current_cam=camera(client_,root);const auto current_distance=current_cam?orbit(client_,current_cam):0;
    if(!current_distance)throw std::runtime_error("Copied preview distance is missing after an edit.");
    const double expected_distance=distance_*std::exp(-static_cast<double>(p.zoom));
    Call set_number{Operation::SetNumber,client_,current_distance};set_number.number=expected_distance;set_number.subtype=number_subtype;run(set_number);
    // Validate numeric interpretation, not just copied byte count. Integer
    // arrays can hold the same four-byte words but decode float bits as integers.
    const auto checked_cam=camera(client_,root);const auto checked_angle=checked_cam?angle(client_,checked_cam):0;
    const auto checked_distance=checked_cam?orbit(client_,checked_cam):0;
    if(!checked_angle || !checked_distance)throw std::runtime_error("Copied camera values disappeared after writing.");
    std::array<float,3> decoded{};Call read_array{Operation::Vector,client_,checked_angle};read_array.vector=decoded.data();run(read_array);
    Call read_distance{Operation::Number,client_,checked_distance};run(read_distance);
    uncertain_=false;
    if(!read_array.accepted || !std::isfinite(read_distance.number) ||
       std::abs(read_distance.number-expected_distance)>std::max(1e-6,std::abs(expected_distance)*1e-6))
        throw std::runtime_error("Native preview camera distance failed numeric readback.");
    for(std::size_t i=0;i<values.size();++i)
        if(!std::isfinite(decoded[i]) || std::abs(decoded[i]-values[i])>1e-4f)
            throw std::runtime_error("Native preview camera angle failed numeric readback.");
    applied_angles_=decoded;applied_distance_=static_cast<float>(read_distance.number);
}
}
