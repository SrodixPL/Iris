#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace iris::models {
struct Model { std::string name, resource; };
struct Scan { std::vector<Model> entries; std::string detail; };
std::filesystem::path game_content();
bool valid_resource_path(const std::string& resource);
bool same_resource_path(std::string_view first,std::string_view second);
bool compiled_model(const std::filesystem::path& file);
Scan scan(const std::filesystem::path& content);
}
