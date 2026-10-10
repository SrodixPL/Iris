#include "core/sdk/binding_values.hpp"
#include "core/sdk/client.hpp"
#include "core/sdk/schema.hpp"
#include "core/sdk/entities.hpp"
#include "core/log.hpp"
#include "core/sdk/generated/client_bindings.hpp"
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <cstdio>
namespace iris::sdk {
namespace bindings = iris::cosmetics::bindings;
std::string string_at(Address address) {
    char buffer[128]{};
    for (std::size_t i = 0; i + 1 < sizeof(buffer); ++i) {
        buffer[i] = get<char>(address + i);
        if (!buffer[i]) return buffer;
    }
    throw std::runtime_error("Invalid schema name.");
}
Address method(Address object, int index) { return get<Address>(get<Address>(object) + index * sizeof(Address)); }
void* interface(const wchar_t* module, const char* name) {
    const auto dll = GetModuleHandleW(module);
    if (!dll) throw std::runtime_error("Game module is not loaded.");
    using Factory = void*(__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<Factory>(GetProcAddress(dll, "CreateInterface"));
    if (!factory) throw std::runtime_error("Game interface factory is absent.");
    const auto result = factory(name, nullptr);
    if (!result) throw std::runtime_error("Required game interface is absent.");
#ifdef IRIS_DEBUG
    char message[180]{};
    std::snprintf(message, sizeof(message), "Interface %s=%p", name, result);
    overlay_log(message);
#endif
    return result;
}
int schema_field(Address scope, const char* class_name, const char* field_name) {
    using Find = void(__fastcall*)(void*, Address*, const char*);
    Address binding{};
    reinterpret_cast<Find>(method(scope,static_cast<int>(iris::sdk::binding_values::slots_schema_find_class)))(reinterpret_cast<void*>(scope), &binding, class_name);
    if (!binding) throw std::runtime_error(std::string("Missing schema class: ") + class_name);
    const auto count = get<std::uint16_t>(binding + offsetof(SchemaClassInfo, field_count));
    const auto fields = get<Address>(binding + offsetof(SchemaClassInfo, fields));
    const auto size = get<int>(binding + offsetof(SchemaClassInfo, size));
    if (count > 1024 || size < 1 || size > 0x10000) throw std::runtime_error("Schema layout mismatch.");
    for (unsigned i = 0; i < count; ++i) {
        const auto field = fields + i * sizeof(SchemaField);
        if (string_at(get<Address>(field)) == field_name) {
            const auto offset = get<int>(field + offsetof(SchemaField, offset));
            if (offset < 0 || offset >= size) throw std::runtime_error("Schema field is outside its class.");
#ifdef IRIS_DEBUG
            char message[256]{};
            std::snprintf(message, sizeof(message), "Schema %s::%s offset=0x%x", class_name, field_name, offset);
            overlay_log(message);
#endif
            return offset;
        }
    }
    throw std::runtime_error(std::string("Missing schema field: ") + class_name + "::" + field_name);
}
int active_pawn_field(Address scope) {
    // The round-owned player pawn remains the original body during bot takeover.
    return schema_field(scope, "CBasePlayerController", "m_hPawn");
}
std::string sha256_file(HMODULE module) {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) throw std::runtime_error("Cannot identify client binary.");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot fingerprint client binary.");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA256 provider unavailable.");
    const auto created = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    bool okay = created >= 0;
    std::array<char, 65536> buffer{};
    while (okay && stream) {
        stream.read(buffer.data(), buffer.size());
        if (stream.gcount()) okay = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(stream.gcount()), 0) >= 0;
    }
    std::array<unsigned char, 32> digest{};
    okay = okay && !stream.bad() && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!okay) throw std::runtime_error("Client fingerprint failed.");
    char result[65]{};
    for (std::size_t i = 0; i < digest.size(); ++i) std::snprintf(result + i * 2, 3, "%02x", digest[i]);
    return result;
}
Address entity(Address client, std::uint32_t handle) {
    if (handle == 0xffffffff || handle == 0xfffffffe) return 0;
    const auto chunks = get<Address>(client + bindings::entity_chunks);
    if (!chunks) return 0;
    const auto index = handle & 0x7fff;
    const auto chunk = get<Address>(chunks + (index >> 9) * sizeof(Address));
    if (!chunk) return 0;
    const auto identity = chunk + (index & 0x1ff) * sizeof(EntityIdentity);
    if (get<std::uint32_t>(identity + offsetof(EntityIdentity, handle)) != handle) return 0;
    return get<Address>(identity);
}
}
