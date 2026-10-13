#include "core/render/chams_layer.hpp"
#include <imgui.h>

namespace iris::render::chams_layer {
void Layer::reset(){
    std::lock_guard lock(mutex_);context_.Reset();compose_context_.Reset();device_.Reset();deferred_.clear();texture_.Reset();resolved_.Reset();target_.Reset();image_.Reset();capture_blend_.Reset();compose_blend_.Reset();depth_states_.clear();width_=height_=surface_width_=surface_height_=0;samples_=1;quality_=0;drawn_=false;clear_=true;
}
void Layer::configure(ID3D11DeviceContext* context,UINT width,UINT height){
    std::lock_guard lock(mutex_);
    if(context==context_.Get()&&width==width_&&height==height_&&target_)return;
    reset();if(!context||!width||!height)return;
    Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
    device_=device;
    if(!surface(width,height,{1,0})){reset();return;}
    D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device->CreateBlendState(&blend,&capture_blend_))){reset();return;}
    rt.SrcBlend=D3D11_BLEND_ONE;
    if(FAILED(device->CreateBlendState(&blend,&compose_blend_))){reset();return;}
    context_=compose_context_=context;width_=width;height_=height;
}
bool Layer::surface(UINT width,UINT height,DXGI_SAMPLE_DESC samples){
    if(width==surface_width_&&height==surface_height_&&samples.Count==samples_&&samples.Quality==quality_&&target_)return true;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,resolved;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.ArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc=samples;desc.BindFlags=D3D11_BIND_RENDER_TARGET|(samples.Count==1?D3D11_BIND_SHADER_RESOURCE:0);
    if(FAILED(device_->CreateTexture2D(&desc,nullptr,&texture))||FAILED(device_->CreateRenderTargetView(texture.Get(),nullptr,&target)))return false;
    if(samples.Count>1){desc.SampleDesc={1,0};desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(FAILED(device_->CreateTexture2D(&desc,nullptr,&resolved)))return false;}
    if(FAILED(device_->CreateShaderResourceView(resolved?resolved.Get():texture.Get(),nullptr,&image)))return false;
    texture_=std::move(texture);resolved_=std::move(resolved);target_=std::move(target);image_=std::move(image);surface_width_=width;surface_height_=height;samples_=samples.Count;quality_=samples.Quality;drawn_=false;clear_=true;return true;
}
void Layer::composite(){
    std::lock_guard lock(mutex_);
    if(drawn_&&image_){
        if(resolved_)compose_context_->ResolveSubresource(resolved_.Get(),0,texture_.Get(),0,DXGI_FORMAT_R8G8B8A8_UNORM);
        auto* draw=ImGui::GetBackgroundDrawList();
        draw->AddCallback([](const ImDrawList*,const ImDrawCmd* command){auto* self=static_cast<Layer*>(command->UserCallbackData);self->compose_context_->OMSetBlendState(self->compose_blend_.Get(),nullptr,0xffffffff);},this);
        draw->AddImage(reinterpret_cast<ImTextureID>(image_.Get()),{0,0},ImGui::GetIO().DisplaySize);
        draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    }
    drawn_=false;clear_=true;
    for(const auto& capture:deferred_)capture->composite();
}
}
