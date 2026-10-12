#include "core/menu/thumbnail_source.hpp"
#include "core/menu/file_worker.hpp"
#include "ext/json/json.hpp"
#include <Windows.h>

namespace iris::ui {
std::filesystem::path local_thumbnail_root(const std::filesystem::path& module_folder) {
    const auto available=[](const std::filesystem::path& candidate) {
        try {
            const auto manifest=nlohmann::json::parse(read_file(candidate / "manifest.json"));
            return manifest.value("source", "")=="installed-cs2-vpk" &&
                manifest.value("version",0)==1 && manifest.contains("images") &&
                manifest["images"].is_object() && !manifest["images"].empty();
        } catch(const std::exception&) {return false;}
    };
    // Game-derived art has a separate root from the former web cache.
    auto folder = module_folder;
    for (int depth = 0; depth < 8 && !folder.empty(); ++depth) {
        const auto candidate = folder / "assets/inventory-game";
        if(available(candidate))return candidate;
        const auto parent = folder.parent_path();
        if (parent == folder) break;
        folder = parent;
    }
    wchar_t data[32768]{};
    const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",data,static_cast<DWORD>(std::size(data)));
    if(length && length<std::size(data)) {
        const auto candidate=std::filesystem::path(data)/L"Iris/assets/inventory-game";
        if(available(candidate))return candidate;
    }
    return {};
}

std::filesystem::path local_thumbnail_path(const std::filesystem::path& root, int definition, int paint) {
    if (root.empty() || paint < 0 || (definition <= 0 && definition != -4)) return {};
    const auto path = root / (std::to_string(definition) + "_" + std::to_string(paint) + ".png");
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < 8 || size > 4 * 1024 * 1024) return {};
    return path;
}
}
