#pragma once
#include "core/menu/file_worker.hpp"
#include "core/menu/inventory.hpp"
#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>
#include <unordered_map>
#include <vector>
#include <atomic>
namespace iris::cosmetics {struct PreviewImage;}
namespace iris::ui {
class ImageCache {
    struct Pixels {std::uint64_t key{};unsigned width{},height{};std::vector<unsigned char> rgba;ImVec2 uv0{},uv1{1,1};};
    struct Image {Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;ImVec2 uv0{},uv1{1,1};float aspect{1};int last_frame{};bool pending{},missing{};std::uint64_t retry_after{};};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> preview_texture_;
    std::uint64_t preview_revision_{};
    float preview_aspect_{1};
    int preview_definition_{};
    bool preview_interactive_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    std::filesystem::path root_;std::mutex mutex_;std::deque<Pixels> decoded_;
    std::unordered_map<std::uint64_t,Image> images_;std::atomic_bool stopping_{};FileWorker worker_;
    Image& image(int definition,int paint);
public:
    explicit ImageCache(ID3D11Device* device);
    ~ImageCache();
    void pump();
    InventoryArt preview(const cosmetics::PreviewImage& image);
    InventoryArt last_preview(int definition) const;
    InventoryArt art(int definition,int paint);
    void draw(int definition,int paint,ImVec2 position,ImVec2 size);
};
}
