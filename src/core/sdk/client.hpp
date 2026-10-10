#pragma once
#include "util/memory.hpp"
#include "core/sdk/types.hpp"
#include <string>
#include <stdexcept>
namespace iris::sdk {
template<class T> T get(Address address) {
    const auto value = memory::read<T>(address);
    if (!value) throw std::runtime_error("Game object became unreadable; update suspended.");
    return *value;
}

std::string string_at(Address address);
Address method(Address object, int index);
void* interface(const wchar_t* module, const char* name);
int schema_field(Address scope, const char* class_name, const char* field_name);
int active_pawn_field(Address scope);
std::string sha256_file(HMODULE module);
Address entity(Address client, std::uint32_t handle);
}
