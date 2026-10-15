#pragma once
#include <array>
namespace iris::sdk::reviewed_build {
inline constexpr int number=14191;
inline constexpr const char* client_sha256="659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0";
inline constexpr const char* engine2_sha256="6456df18d8f954394024c2c07da8c848df15a5ace74ce7390d78970ceb39ee21";
inline constexpr const char* scenesystem_sha256="040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5";
inline constexpr const char* tier0_sha256="990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e";
inline constexpr const char* materialsystem2_sha256="b4d5c667eb65f13d9193d743a88eb1f9dfa623cc8f745cef764d9977969e38b6";
inline constexpr const char* schemasystem_sha256="d2a9b5164cb68953e77d0ef424c1263d2cc452a75e935316bc8b4c4500e63648";
inline constexpr const char* resourcesystem_sha256="0004439e37b6ffd5a37683f9f288726584be2a3b4ccc6674af8303cde0343027";
inline constexpr std::array modules{L"client.dll",L"engine2.dll",L"scenesystem.dll",L"tier0.dll",L"materialsystem2.dll",L"schemasystem.dll",L"resourcesystem.dll"};
inline constexpr std::array keys{"client.dll","engine2.dll","scenesystem.dll","tier0.dll","materialsystem2.dll","schemasystem.dll","resourcesystem.dll"};
inline constexpr std::array hashes{client_sha256,engine2_sha256,scenesystem_sha256,tier0_sha256,materialsystem2_sha256,schemasystem_sha256,resourcesystem_sha256};
}
