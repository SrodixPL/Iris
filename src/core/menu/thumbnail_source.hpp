#pragma once
#include <filesystem>
#include <atomic>
namespace iris::ui {
bool download_thumbnail(int definition, int paint, const std::filesystem::path& destination, const std::atomic_bool& stopping);
}
