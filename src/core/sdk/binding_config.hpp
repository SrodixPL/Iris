#pragma once
#include "core/sdk/binding_values.hpp"
#include "core/sdk/offsets/build.hpp"
#include "core/sdk/generated/client_bindings.hpp"
#include "core/sdk/world_bindings.hpp"
#include "core/sdk/visual_bindings.hpp"
#include "ext/json/json.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace iris::sdk::binding_config {
inline constexpr const char* updated_message="CS2 has updated. A manual Iris update is in progress.";
inline constexpr auto modules=reviewed_build::modules;
inline constexpr auto hashes=reviewed_build::hashes;
inline constexpr auto keys=reviewed_build::keys;
struct Entry { const char* group; const char* name; std::uintptr_t* destination; std::uintptr_t compiled; };
inline Entry entries[]{
#define IRIS_BINDING(group, name, value) {#group, #name, &binding_values::group##_##name, value},
#define IRIS_LAYOUT(group, name, value) {#group, #name, nullptr, value},
#include "core/sdk/offsets/offsets.inc"
#undef IRIS_BINDING
#undef IRIS_LAYOUT
};
inline auto read(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Iris bindings.json is missing or unreadable.");
    if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Iris bindings.json exceeds the size limit.");
    auto data=nlohmann::json::parse(stream);
    if(data.at("format_version")!=1 || data.at("reviewed_build")!=reviewed_build::number)
        throw std::runtime_error(updated_message);
    for(std::size_t i=0;i<keys.size();++i)
        if(data.at("fingerprints").at(keys[i])!=hashes[i])throw std::runtime_error(updated_message);
    std::array<std::uintptr_t,std::size(entries)> values{};
    for(std::size_t i=0;i<std::size(entries);++i){
        const auto& entry=entries[i];
        const auto& value=data.at("bindings").at(entry.group).at(entry.name);
        if(!value.is_number_unsigned())throw std::runtime_error("Iris bindings.json contains an invalid offset type.");
        const auto number=value.get<std::uint64_t>();
        // Reviewed constants include zero offsets and 64-bit identifier masks.
        // Compare them exactly below. Mutable native RVAs remain nonzero and
        // bounded to 32 bits; vtable slots retain their separate bound.
        if((entry.destination && (number==0 || number>UINT32_MAX)) || (std::string_view(entry.group)=="slots" && number>4096))throw std::runtime_error("Iris bindings.json contains an out-of-range offset.");
        if(!entry.destination && number!=entry.compiled)throw std::runtime_error("Iris bindings.json requires a compiled layout update.");
        values[i]=static_cast<std::uintptr_t>(number);
    }
    return values;
}
enum class Source { built_in, external };
// Missing optional JSON selects the compiled registry. Invalid/unreadable JSON
// fails before mutation. Bootstrap verifies loaded-module fingerprints first.
inline Source load(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path)) {
        for(const auto& entry:entries)if(entry.destination)*entry.destination=entry.compiled;
        return Source::built_in;
    }
    const auto values=read(path);
    for(std::size_t i=0;i<std::size(entries);++i)if(entries[i].destination)*entries[i].destination=values[i];
    return Source::external;
}
}
