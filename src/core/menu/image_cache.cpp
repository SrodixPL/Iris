#include "core/menu/image_cache.hpp"
#include "core/features/inventory/inventory.hpp"
#include "core/menu/theme.hpp"
#include "core/menu/thumbnail_source.hpp"
#include "core/log.hpp"
#include <wincodec.h>
#include <algorithm>
namespace iris::ui {
using Microsoft::WRL::ComPtr;
namespace {
std::uint64_t key_for(int definition,int paint){return (static_cast<std::uint64_t>(definition)<<32)|static_cast<unsigned>(paint);}
}
ImageCache::ImageCache(ID3D11Device* device):device_(device){
    const auto executable=executable_folder();worker_.push([this,executable]{
        root_=local_thumbnail_root(executable);
        overlay_log(root_.empty()?"Local inventory artwork unavailable in the Iris data directory.":
            "Local inventory artwork found in the Iris data directory.");
    });
}
ImageCache::~ImageCache(){stopping_=true;}
ImageCache::Image& ImageCache::image(int definition,int paint){
    const auto key=key_for(definition,paint);auto& entry=images_[key];entry.last_frame=ImGui::GetFrameCount();
    if(entry.texture||entry.pending||(entry.missing && GetTickCount64()<entry.retry_after))return entry;
    entry.missing=false;
    entry.pending=worker_.push([this,key,definition,paint]{
        if(stopping_)return;
        // An injected image can use the Iris data directory rather than a
        // module filename. Retry discovery if artwork was deployed later.
        if(root_.empty())root_=local_thumbnail_root(executable_folder());
        Pixels result;result.key=key;const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try{const bool decoded=[&]{
            const auto path=local_thumbnail_path(root_,definition,paint);
            if(path.empty())return false;
            ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
            if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(decoder->GetFrame(0,&frame))||FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))||FAILED(converter->GetSize(&result.width,&result.height)))return false;
            if(!result.width||!result.height||result.width>2048||result.height>2048)return false;
            result.rgba.resize(result.width*result.height*4);if(FAILED(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.rgba.size()),result.rgba.data())))return false;
            unsigned minx=result.width,miny=result.height,maxx=0,maxy=0;
            for(unsigned y=0;y<result.height;++y)for(unsigned x=0;x<result.width;++x)if(result.rgba[(y*result.width+x)*4+3]>16){minx=std::min(minx,x);maxx=std::max(maxx,x);miny=std::min(miny,y);maxy=std::max(maxy,y);}
            if(minx<=maxx&&miny<=maxy){result.uv0={static_cast<float>(minx)/result.width,static_cast<float>(miny)/result.height};result.uv1={static_cast<float>(maxx+1)/result.width,static_cast<float>(maxy+1)/result.height};}
            return true;
        }();if(!decoded)result.rgba.clear();
        }catch(...){result.rgba.clear();}
        if(SUCCEEDED(initialized))CoUninitialize();std::lock_guard lock(mutex_);decoded_.push_back(std::move(result));
    });return entry;
}
void ImageCache::pump(){
    for(int i=0;i<2;++i){Pixels pixels;{std::unique_lock lock(mutex_,std::try_to_lock);if(!lock||decoded_.empty())break;pixels=std::move(decoded_.front());decoded_.pop_front();}
        auto found=images_.find(pixels.key);if(found==images_.end())continue;auto& image=found->second;image.pending=false;
        if(pixels.rgba.empty()){image.missing=true;image.retry_after=GetTickCount64()+30000;continue;}D3D11_TEXTURE2D_DESC desc{};desc.Width=pixels.width;desc.Height=pixels.height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.rgba.data(),pixels.width*4,0};ComPtr<ID3D11Texture2D> texture;
        if(FAILED(device_->CreateTexture2D(&desc,&data,&texture))||FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&image.texture))){image.missing=true;continue;}
        image.uv0=pixels.uv0;image.uv1=pixels.uv1;image.aspect=(pixels.uv1.x-pixels.uv0.x)*pixels.width/((pixels.uv1.y-pixels.uv0.y)*pixels.height);
    }
    if(images_.size()>128)std::erase_if(images_,[](const auto& pair){return !pair.second.pending&&pair.second.last_frame<ImGui::GetFrameCount()-120;});
}
InventoryArt ImageCache::preview(const cosmetics::PreviewImage& image){
    if(!image.width||!image.height||image.width>4096||image.height>4096||image.rgba.size()!=std::size_t(image.width)*image.height*4)return last_preview(image.selection.definition);
    if(preview_revision_!=image.revision){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=image.width;desc.Height=image.height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels{image.rgba.data(),image.width*4,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
        if(FAILED(device_->CreateTexture2D(&desc,&pixels,&texture))||FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&view)))return last_preview(image.selection.definition);
        preview_texture_=std::move(view);preview_revision_=image.revision;preview_aspect_=static_cast<float>(image.width)/image.height;
        preview_definition_=image.selection.definition;preview_interactive_=image.interactive;
    }
    return last_preview(image.selection.definition);
}
InventoryArt ImageCache::last_preview(int definition) const {
    if(!preview_texture_ || preview_definition_!=definition)return {};
    return {ImTextureRef(reinterpret_cast<ImTextureID>(preview_texture_.Get())),{0,0},{1,1},preview_aspect_,preview_interactive_};
}
InventoryArt ImageCache::art(int definition,int paint){auto& value=image(definition,paint);return {ImTextureRef(reinterpret_cast<ImTextureID>(value.texture.Get())),value.uv0,value.uv1,value.aspect};}
void ImageCache::draw(int definition,int paint,ImVec2 p,ImVec2 size){
    auto& value=image(definition,paint);auto* draw=ImGui::GetWindowDrawList();
    if(value.texture){float width=std::min(size.x,size.y*value.aspect),height=width/value.aspect;ImVec2 from{p.x+(size.x-width)*.5f,p.y+(size.y-height)*.5f};draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(value.texture.Get())),from,{from.x+width,from.y+height},value.uv0,value.uv1,ImGui::GetColorU32(ImVec4{1,1,1,1}));}
    else{draw->AddRect({p.x+size.x*.35f,p.y+size.y*.3f},{p.x+size.x*.65f,p.y+size.y*.65f},ImGui::GetColorU32(theme::border),theme::control_radius*ImGui::GetStyle().FontScaleMain);const char* text=value.missing?"No image":"Loading";auto measured=ImGui::CalcTextSize(text);draw->AddText({p.x+(size.x-measured.x)*.5f,p.y+size.y*.7f},ImGui::GetColorU32(theme::muted),text);}
}

}
