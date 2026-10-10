#pragma once
#include "core/menu/menu_state.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
namespace iris {
class EnvironmentRender {
public:
    explicit EnvironmentRender(ID3D11Device* device);
    void draw(ID3D11DeviceContext* context,IDXGISwapChain* chain,const ui::WorldState& settings,float time,ID3D11Texture2D* scene_depth=nullptr);
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source_;
};
}
