#pragma once
#include "core/sdk/binding_values.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace iris::sdk::experimental {
using Address = std::uintptr_t;
inline constexpr std::uint64_t attack = 1;
inline constexpr int maximum_steps = 64;
// Reviewed against client SHA-256 659940c1...49e36b0 (build 14191). These are raw messages,
// including their native protobuf header, rather than Velocity's +0x10 views.
struct Header { Address vtable{}, arena{}; };
struct Repeated { Address arena{}; int size{}, capacity{}; Address rep{}; };
struct Buttons { Header header; std::uint32_t has{}, cached{}; std::uint64_t value{}, changed{}, scroll{}; };
struct Angles { Header header; std::uint32_t has{}, cached{}; float x{}, y{}, z{}; };
struct Step {
    Header header;
    std::uint32_t has{}, cached{};
    std::uint64_t button{};
    bool pressed{};
    float when{}, forward{}, left{}, pitch{}, yaw{};
};
struct Base {
    Header header;
    std::uint32_t has{}, cached{};
    Repeated moves;
    Address crc{}, buttons{}, angles{}, execution_notes{};
    int command_number{};
    int tick{};
    float forward{},left{},up{};
    std::array<std::byte,8> reserved64{};
    int random_seed{};
};
struct History {
    Header header;
    std::uint32_t has{}, cached{};
    std::array<Address,9> messages{};
    int render_tick{};
    float render_fraction{};
    int player_tick{};
    float player_fraction{};
    int frame{}, target{};
};
struct Command {
    Address vtable{};
    int number{};
    std::array<std::byte,4> reserved0c{};
    Header proto;
    std::uint32_t has{}, cached{};
    Repeated history;
    Address base{};
    std::array<std::byte,4> prediction{};
    int attack_history{}, secondary_history{};
    Address buttons_vtable{};
    std::uint64_t value{}, changed{}, scroll{};
    std::array<std::byte,32> tail{};
};
static_assert(sizeof(Command)==0x98 && offsetof(Command,number)==0x8 && offsetof(Command,base)==0x40 && offsetof(Command,value)==0x60);
static_assert(offsetof(Command,attack_history)==0x4c);
static_assert(sizeof(Step)==0x38 && offsetof(Step,when)==0x24 && offsetof(Step,button)==0x18);
static_assert(offsetof(Base,moves)==0x18 && offsetof(Base,buttons)==0x38 && offsetof(Base,tick)==0x54);
static_assert(offsetof(Base,command_number)==0x50 && offsetof(Base,random_seed)==0x6c && offsetof(Base,forward)==0x58 && offsetof(Base,left)==0x5c);
static_assert(sizeof(Repeated)==0x18 && sizeof(Buttons)==0x30 && offsetof(Angles,x)==0x18);
static_assert(sizeof(History)==0x78 && offsetof(History,render_tick)==0x60 && offsetof(History,player_tick)==0x68 && offsetof(History,player_fraction)==0x6c);
static_assert(offsetof(Command,history)==0x28);
namespace bindings {
inline auto& create_move=iris::sdk::binding_values::commands_create_move;
inline auto& input_vtable=iris::sdk::binding_values::commands_input_vtable;
inline auto& command_ring=iris::sdk::binding_values::commands_command_ring;
inline auto& command=iris::sdk::binding_values::commands_command;
inline auto& ring_sequence=iris::sdk::binding_values::commands_ring_sequence;
inline auto& assign_crc=iris::sdk::binding_values::commands_assign_crc;
inline auto& history_vtable=iris::sdk::binding_values::commands_history_vtable;
inline auto& command_vtable=iris::sdk::binding_values::commands_command_vtable;
inline auto& command_proto_vtable=iris::sdk::binding_values::commands_command_proto_vtable;
inline auto& prepare_command=iris::sdk::binding_values::commands_prepare_command;
inline auto& encode_command_delta=iris::sdk::binding_values::commands_encode_command_delta;
}
}
