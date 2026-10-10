#pragma once
#include <array>
namespace iris::sdk::reviewed_build {
inline constexpr int number=14190;
inline constexpr const char* client_sha256="44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008";
inline constexpr const char* engine2_sha256="4b8fe29ecdef4f492a602d9013cbfe5226561c9c0ba4361ce82e6f46d4f9bc04";
inline constexpr const char* scenesystem_sha256="bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831";
inline constexpr const char* tier0_sha256="00ecf3e3d33f9036f3fd6e69f2c89510148e1a6202dd8bdea592fecc7891cfd4";
inline constexpr const char* materialsystem2_sha256="336abe089468bd5b218f773f09da3942fa9346b7f49f24d9e3591722fc54c5e6";
inline constexpr const char* schemasystem_sha256="f362c13aada09ee03c402e3a0de2b66bdebfe57bbb15d23c437971b21f32b6e8";
inline constexpr const char* resourcesystem_sha256="ce1305d954794ffa91a6250feb231ca103c634ddc15916884b2bd8b2f4b243d0";
inline constexpr std::array modules{L"client.dll",L"engine2.dll",L"scenesystem.dll",L"tier0.dll",L"materialsystem2.dll",L"schemasystem.dll",L"resourcesystem.dll"};
inline constexpr std::array keys{"client.dll","engine2.dll","scenesystem.dll","tier0.dll","materialsystem2.dll","schemasystem.dll","resourcesystem.dll"};
inline constexpr std::array hashes{client_sha256,engine2_sha256,scenesystem_sha256,tier0_sha256,materialsystem2_sha256,schemasystem_sha256,resourcesystem_sha256};
}
