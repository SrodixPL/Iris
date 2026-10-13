#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <mutex>
#include <array>
#include <vector>
#include <cstring>
#include <atomic>
#include <cmath>
#include "core/thread_state.hpp"

namespace iris::render::chams_layer {
// Only the synchronous, reviewed hidden-material dispatch sets this marker.
// No native mesh, scene pointer or draw arguments survive the dispatch.
inline ThreadValue hidden_pass;
class Layer {
    std::recursive_mutex mutex_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> compose_context_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> capture_blend_,compose_blend_;
    struct DepthState {D3D11_DEPTH_STENCIL_DESC description{};Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;};
    std::vector<DepthState> depth_states_;
    std::vector<std::unique_ptr<Layer>> deferred_;
    UINT width_{},height_{};
    UINT surface_width_{},surface_height_{},samples_{1},quality_{};
    bool drawn_{},clear_{true};
    std::atomic<std::uint64_t> captures_{};
    std::atomic<std::uint64_t> marked_{},rejected_context_{},rejected_viewport_{};
    bool surface(UINT width,UINT height,DXGI_SAMPLE_DESC samples);
public:
    std::uint64_t captures() const noexcept {return captures_.load(std::memory_order_relaxed);}
    std::array<std::uint64_t,3> diagnostics() const noexcept {return {marked_.load(),rejected_context_.load(),rejected_viewport_.load()};}
    void configure(ID3D11DeviceContext* context,UINT width,UINT height);
    void reset();
    void composite();
    template<class Draw> void capture(ID3D11DeviceContext* context,Draw draw){
        if(!hidden_pass)return;
        marked_.fetch_add(1,std::memory_order_relaxed);
        std::lock_guard lock(mutex_);
        if(!context_||!target_)return;
        if(context!=context_.Get()){
            Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
            if(context->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED||device.Get()!=device_.Get()){++rejected_context_;return;}
            Layer* selected{};for(const auto& capture:deferred_)if(capture->context_.Get()==context){selected=capture.get();break;}
            if(!selected){if(deferred_.size()>=16){++rejected_context_;return;}auto capture=std::make_unique<Layer>();capture->configure(context,width_,height_);capture->compose_context_=context_;selected=capture.get();deferred_.push_back(std::move(capture));}
            const auto before=selected->captures();const auto reasons=selected->diagnostics();selected->capture(context,draw);captures_.fetch_add(selected->captures()-before,std::memory_order_relaxed);const auto after=selected->diagnostics();rejected_context_.fetch_add(after[1]-reasons[1]);rejected_viewport_.fetch_add(after[2]-reasons[2]);return;
        }
        UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);
        if(count!=1||viewport.TopLeftX!=0||viewport.TopLeftY!=0||viewport.Width<1||viewport.Height<1||viewport.Width>32768||viewport.Height>32768||std::abs(viewport.Width/viewport.Height-static_cast<float>(width_)/height_)>.02f){++rejected_viewport_;return;}
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> raw{};
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> saved;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
        Microsoft::WRL::ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask{};
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state;UINT stencil{};
        context->OMGetRenderTargets(static_cast<UINT>(raw.size()),raw.data(),&depth);
        for(std::size_t i=0;i<raw.size();++i)saved[i].Attach(raw[i]);
        DXGI_SAMPLE_DESC samples{1,0};
        if(raw[0]){Microsoft::WRL::ComPtr<ID3D11Resource> resource;Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;raw[0]->GetResource(&resource);if(SUCCEEDED(resource.As(&texture))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);samples=desc.SampleDesc;}}
        if(!surface(static_cast<UINT>(viewport.Width),static_cast<UINT>(viewport.Height),samples))return;
        context->OMGetBlendState(&blend,factors,&mask);
        context->OMGetDepthStencilState(&depth_state,&stencil);
        ID3D11DepthStencilState* readonly{};
        if(hidden_pass==2&&depth){
            D3D11_DEPTH_STENCIL_DESC desc{};
            if(depth_state)depth_state->GetDesc(&desc);
            else {desc.DepthEnable=TRUE;desc.DepthFunc=D3D11_COMPARISON_LESS;desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;desc.StencilReadMask=D3D11_DEFAULT_STENCIL_READ_MASK;desc.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};desc.BackFace=desc.FrontFace;}
            desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;desc.StencilWriteMask=0;
            for(const auto& cached:depth_states_)if(std::memcmp(&cached.description,&desc,sizeof(desc))==0){readonly=cached.state.Get();break;}
            if(!readonly){Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);DepthState cached;cached.description=desc;
                if(FAILED(device->CreateDepthStencilState(&desc,&cached.state)))return;
                if(depth_states_.size()>=16)depth_states_.erase(depth_states_.begin());depth_states_.push_back(std::move(cached));readonly=depth_states_.back().state.Get();}
        }
        if(clear_){constexpr FLOAT transparent[4]{};context->ClearRenderTargetView(target_.Get(),transparent);clear_=false;}
        context->OMSetRenderTargets(1,target_.GetAddressOf(),hidden_pass==2?depth.Get():nullptr);
        if(readonly)context->OMSetDepthStencilState(readonly,stencil);
        context->OMSetBlendState(capture_blend_.Get(),nullptr,0xffffffff);
        draw();
        context->OMSetBlendState(blend.Get(),factors,mask);
        if(readonly)context->OMSetDepthStencilState(depth_state.Get(),stencil);
        context->OMSetRenderTargets(static_cast<UINT>(raw.size()),raw.data(),depth.Get());
        drawn_=true;
        captures_.fetch_add(1,std::memory_order_relaxed);
    }
};
inline Layer layer;
}
