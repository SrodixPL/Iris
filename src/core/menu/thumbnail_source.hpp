#pragma once
#include <filesystem>
namespace iris::ui {
// Worker-thread lookup in an art pack decoded from the installed game VPK.
std::filesystem::path local_thumbnail_root(const std::filesystem::path& module_folder);
std::filesystem::path local_thumbnail_path(const std::filesystem::path& root, int definition, int paint);
}
