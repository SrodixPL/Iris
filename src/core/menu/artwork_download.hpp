#pragma once
#include <atomic>
#include <filesystem>
#include <string_view>
namespace iris::ui::artwork {
bool complete(const std::filesystem::path& root);
bool valid_filename(std::string_view name);
// Installer is exposed separately for data integrity and interruption tests.
void install(const std::filesystem::path& pack,const std::filesystem::path& destination,
             std::string_view expected_hash,const std::atomic_bool& cancelled);
std::filesystem::path fetch(const std::filesystem::path& data_folder,const std::atomic_bool& cancelled);
}
