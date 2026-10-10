#include "core/menu/backdrop.hpp"
#include "core/menu/menu_state.hpp"
#include "core/menu/theme.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace iris::ui::backdrop {
namespace {
using Microsoft::WRL::ComPtr;
struct Image {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  ComPtr<ID3D11RenderTargetView> target;
};
struct State {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> downsample,gaussian;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11DepthStencilState> depth;
  ComPtr<ID3D11RasterizerState> raster;
  Image gradient,source,ping,pong;
  // Stable texture references survive cached popup fades and resized GPU resources.
  ImTextureData gradient_id,blur_id;
  Bounds area;
  float scale{1};
  UINT width{},height{};
  int rendered_frame{-1};
  bool enabled{},failed{},visible{},last_ready{};
  bool logged{};
  bool target_reported{};
  void (*log)(const char*){};
  ImVec4 gradient_start{},gradient_end{};
} state;
void fallback() {
  state.failed=true;
  if(!state.logged && state.log)state.log("Menu blur unavailable; using alpha-only surfaces.");
  state.logged=true;
}
ImTextureRef reference(ImTextureData& texture) { ImTextureRef ref; ref._TexData=&texture; return ref; }
ImTextureID identifier(ID3D11ShaderResourceView* view) { return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(view)); }
bool image(Image& out,UINT width,UINT height,bool target,const void* pixels=nullptr,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM,UINT pixel_bytes=4) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
  desc.Format=format; desc.SampleDesc.Count=1;
  desc.Usage=pixels ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|(target ? D3D11_BIND_RENDER_TARGET : 0);
  D3D11_SUBRESOURCE_DATA data{pixels,width*pixel_bytes,0};
  return SUCCEEDED(state.device->CreateTexture2D(&desc,pixels ? &data : nullptr,out.texture.ReleaseAndGetAddressOf())) &&
    SUCCEEDED(state.device->CreateShaderResourceView(out.texture.Get(),nullptr,out.view.ReleaseAndGetAddressOf())) &&
    (!target || SUCCEEDED(state.device->CreateRenderTargetView(out.texture.Get(),nullptr,out.target.ReleaseAndGetAddressOf())));
}
bool gradient() {
  constexpr auto size=theme::gradient_texture_size;
  std::vector<uint16_t> pixels(size*size*4);
  for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
    const float diagonal=static_cast<float>(x+y)/(2*(size-1));
    const float t=(diagonal+theme::gradient_offset)/(1+theme::gradient_offset);
    auto color=theme::mix(theme::window_gradient_start,theme::window_gradient_end,t);
    // Full-resolution sub-code dither breaks display banding without magnified grain.
    unsigned hash=(x+1)*0x9e3779b9u^(y+1)*0x85ebca6bu;
    hash^=hash>>16; hash*=0x7feb352du; hash^=hash>>15;
    const float noise=(static_cast<float>(hash&65535)/65535-.5f)*2*theme::gradient_dither;
    color.x+=noise; color.y+=noise; color.z+=noise;
    auto* pixel=pixels.data()+(y*size+x)*4;
    pixel[0]=static_cast<uint16_t>(std::clamp(std::lround(color.x*65535),0l,65535l));
    pixel[1]=static_cast<uint16_t>(std::clamp(std::lround(color.y*65535),0l,65535l));
    pixel[2]=static_cast<uint16_t>(std::clamp(std::lround(color.z*65535),0l,65535l)); pixel[3]=65535;
  }
  Image generated;
  if(!image(generated,size,size,false,pixels.data(),DXGI_FORMAT_R16G16B16A16_UNORM,8)) {
    std::vector<unsigned char> fallback(pixels.size());
    for(size_t i=0;i<pixels.size();++i) fallback[i]=static_cast<unsigned char>(pixels[i]>>8);
    if(!image(generated,size,size,false,fallback.data())) return false;
  }
  state.gradient=std::move(generated);
  state.gradient_start=theme::window_gradient_start; state.gradient_end=theme::window_gradient_end;
  state.gradient_id.SetTexID(identifier(state.gradient.view.Get())); state.gradient_id.SetStatus(ImTextureStatus_OK);
  return true;
}
constexpr char shaders[]=R"(
Texture2D scene : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Parameters : register(b0) { float2 texel; float2 direction; };
struct Vertex { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex vertex(uint id : SV_VertexID) {
  Vertex v; v.uv=float2((id<<1)&2,id&2); v.pos=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float4 downsample(Vertex v) : SV_TARGET {
  return .25*(scene.Sample(linearClamp,v.uv+texel*float2(-1,-1))+
    scene.Sample(linearClamp,v.uv+texel*float2(1,-1))+
    scene.Sample(linearClamp,v.uv+texel*float2(-1,1))+scene.Sample(linearClamp,v.uv+texel*float2(1,1)));
}
float4 gaussian(Vertex v) : SV_TARGET {
  float2 step=texel*direction;
  return scene.Sample(linearClamp,v.uv)*.227027027+
    (scene.Sample(linearClamp,v.uv+step*1.384615385)+scene.Sample(linearClamp,v.uv-step*1.384615385))*.316216216+
    (scene.Sample(linearClamp,v.uv+step*3.230769231)+scene.Sample(linearClamp,v.uv-step*3.230769231))*.070270270;
}
)";
bool pipeline() {
  ComPtr<ID3DBlob> code;
  const auto compile=[&](const char* entry,const char* profile) {
    return SUCCEEDED(D3DCompile(shaders,sizeof(shaders)-1,nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.ReleaseAndGetAddressOf(),nullptr));
  };
  if(!compile("vertex","vs_4_0") || FAILED(state.device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,state.vertex.GetAddressOf()))) return false;
  if(!compile("downsample","ps_4_0") || FAILED(state.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,state.downsample.GetAddressOf()))) return false;
  if(!compile("gaussian","ps_4_0") || FAILED(state.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,state.gaussian.GetAddressOf()))) return false;
  D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=16; buffer.Usage=D3D11_USAGE_DEFAULT; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
  D3D11_BLEND_DESC blend{}; blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
  D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=FALSE;
  D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
  return SUCCEEDED(state.device->CreateBuffer(&buffer,nullptr,state.constants.GetAddressOf())) &&
    SUCCEEDED(state.device->CreateSamplerState(&sampler,state.sampler.GetAddressOf())) &&
    SUCCEEDED(state.device->CreateBlendState(&blend,state.blend.GetAddressOf())) &&
    SUCCEEDED(state.device->CreateDepthStencilState(&depth,state.depth.GetAddressOf())) &&
    SUCCEEDED(state.device->CreateRasterizerState(&raster,state.raster.GetAddressOf()));
}
struct Pipeline {
  ID3D11DeviceContext* context;
  std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
  std::array<ID3D11UnorderedAccessView*,D3D11_PS_CS_UAV_REGISTER_COUNT> uavs{};
  ComPtr<ID3D11DepthStencilView> depth_target;
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11GeometryShader> geometry;
  std::array<ID3D11ClassInstance*,256> vertex_classes{},pixel_classes{},geometry_classes{};
  UINT vertex_count{256},pixel_count{256},geometry_count{256};
  ComPtr<ID3D11ShaderResourceView> resource;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11DepthStencilState> depth;
  ComPtr<ID3D11RasterizerState> raster;
  ComPtr<ID3D11InputLayout> layout;
  D3D11_PRIMITIVE_TOPOLOGY topology{};
  std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
  UINT viewport_count{static_cast<UINT>(viewports.size())},mask{},stencil{};
  float factors[4]{};
  explicit Pipeline(ID3D11DeviceContext* value):context(value) {
    context->OMGetRenderTargets(static_cast<UINT>(targets.size()),targets.data(),depth_target.GetAddressOf());
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,static_cast<UINT>(uavs.size()),uavs.data());
    context->VSGetShader(vertex.GetAddressOf(),vertex_classes.data(),&vertex_count);
    context->PSGetShader(pixel.GetAddressOf(),pixel_classes.data(),&pixel_count);
    context->GSGetShader(geometry.GetAddressOf(),geometry_classes.data(),&geometry_count);
    context->PSGetShaderResources(0,1,resource.GetAddressOf()); context->PSGetSamplers(0,1,sampler.GetAddressOf());
    context->PSGetConstantBuffers(0,1,constants.GetAddressOf()); context->OMGetBlendState(blend.GetAddressOf(),factors,&mask);
    context->OMGetDepthStencilState(depth.GetAddressOf(),&stencil); context->RSGetState(raster.GetAddressOf());
    context->RSGetViewports(&viewport_count,viewports.data()); context->IAGetInputLayout(layout.GetAddressOf()); context->IAGetPrimitiveTopology(&topology);
  }
  ~Pipeline() {
    ID3D11ShaderResourceView* none{}; context->PSSetShaderResources(0,1,&none);
    UINT count=static_cast<UINT>(targets.size());while(count && !targets[count-1])--count;
    std::array<UINT,D3D11_PS_CS_UAV_REGISTER_COUNT> counters; counters.fill(UINT(-1));
    context->OMSetRenderTargetsAndUnorderedAccessViews(count,targets.data(),depth_target.Get(),count,static_cast<UINT>(uavs.size())-count,uavs.data()+count,counters.data()+count);
    for(auto* target : targets) if(target) target->Release();
    for(auto* view : uavs)if(view)view->Release();
    context->VSSetShader(vertex.Get(),vertex_classes.data(),vertex_count); context->PSSetShader(pixel.Get(),pixel_classes.data(),pixel_count);
    context->GSSetShader(geometry.Get(),geometry_classes.data(),geometry_count);
    for(UINT i=0;i<vertex_count;++i) vertex_classes[i]->Release();
    for(UINT i=0;i<pixel_count;++i) pixel_classes[i]->Release();
    for(UINT i=0;i<geometry_count;++i) geometry_classes[i]->Release();
    auto* input=resource.Get(); context->PSSetShaderResources(0,1,&input);
    auto* sampling=sampler.Get(); context->PSSetSamplers(0,1,&sampling);
    auto* buffer=constants.Get(); context->PSSetConstantBuffers(0,1,&buffer);
    context->OMSetBlendState(blend.Get(),factors,mask); context->OMSetDepthStencilState(depth.Get(),stencil);
    context->RSSetState(raster.Get()); context->RSSetViewports(viewport_count,viewports.data());
    context->IASetInputLayout(layout.Get()); context->IASetPrimitiveTopology(topology);
  }
};
void pass(Image& input,Image& output,ID3D11PixelShader* shader,float x,float y) {
  auto* context=state.context.Get();
  ID3D11ShaderResourceView* none{}; context->PSSetShaderResources(0,1,&none);
  auto* target=output.target.Get(); context->OMSetRenderTargets(1,&target,nullptr);
  D3D11_TEXTURE2D_DESC source{},destination{}; input.texture->GetDesc(&source); output.texture->GetDesc(&destination);
  const float parameters[]{1.f/source.Width,1.f/source.Height,x,y};
  context->UpdateSubresource(state.constants.Get(),0,nullptr,parameters,0,0);
  const D3D11_VIEWPORT viewport{0,0,static_cast<float>(destination.Width),static_cast<float>(destination.Height),0,1};
  context->RSSetViewports(1,&viewport); context->PSSetShader(shader,nullptr,0);
  auto* view=input.view.Get(); context->PSSetShaderResources(0,1,&view); context->Draw(3,0);
}
ImVec2 uv(ImVec2 position) {
  return {(position.x-state.area.min.x)/(state.area.max.x-state.area.min.x),(position.y-state.area.min.y)/(state.area.max.y-state.area.min.y)};
}
void sample(ImDrawList* draw,Bounds bounds,float radius,ImDrawFlags corners,float opacity) {
  if(!available() || !state.visible) return;
  // Preserve tint opacity through cached popup fades, then resolve the two layers together.
  draw->AddImageRounded(reference(state.blur_id),bounds.min,bounds.max,uv(bounds.min),uv(bounds.max),ImGui::GetColorU32(ImVec4{opacity,1,0,1}),radius,corners);
}
void remap(ImDrawData* data,bool ready) {
  for(auto* draw : data->CmdLists) for(auto& command : draw->CmdBuffer) if(command.TexRef._TexData==&state.blur_id) {
    if(!ready) { command.ElemCount=0; command.TexRef=ImGui::GetIO().Fonts->TexRef; continue; }
    for(unsigned i=command.IdxOffset;i<command.IdxOffset+command.ElemCount;++i) {
      auto& vertex=draw->VtxBuffer[draw->IdxBuffer[i]+command.VtxOffset]; vertex.uv=uv(vertex.pos);
      const ImU32 marker=(255u<<IM_COL32_G_SHIFT);
      if((vertex.col&((255u<<IM_COL32_G_SHIFT)|(255u<<IM_COL32_B_SHIFT)))==marker) {
        const float fade=static_cast<float>((vertex.col>>IM_COL32_A_SHIFT)&255)/255;
        const float opacity=static_cast<float>((vertex.col>>IM_COL32_R_SHIFT)&255)/255;
        const float alpha=fade*(1-opacity)/std::max(.0001f,1-fade*opacity);
        vertex.col=IM_COL32_WHITE&~IM_COL32_A_MASK;
        vertex.col|=static_cast<ImU32>(std::lround(alpha*255))<<IM_COL32_A_SHIFT;
      }
    }
  }
}
}
void initialize(ID3D11Device* device,ID3D11DeviceContext* context,bool blur,void (*log)(const char*)) {
  shutdown(); state.device=device; state.context=context; state.enabled=blur;
  state.log=log;
  if(!device) return;
  if(!gradient())fallback();
  if(!context) { fallback(); return; }
  if(blur && !pipeline()) fallback();
}
void shutdown() { state=State{}; }
void begin_frame() { state.visible=false; }
bool available() { return state.enabled && !state.failed && state.device && state.vertex; }
void window(ImDrawList* draw,Bounds bounds,float sidebar,float scale) {
  state.visible=true; state.scale=scale;
  const auto changed=[](ImVec4 a,ImVec4 b) { return a.x!=b.x || a.y!=b.y || a.z!=b.z || a.w!=b.w; };
  if(state.device && (changed(state.gradient_start,theme::window_gradient_start) || changed(state.gradient_end,theme::window_gradient_end)) && !gradient()) {
    state.gradient=Image{};state.gradient_id.SetTexID(0);fallback();
  }
  const auto* viewport=ImGui::GetMainViewport();
  const float margin=(theme::blur_margin+theme::blur_radius)*scale;
  state.area={{std::floor(std::max(viewport->Pos.x,bounds.min.x-margin)),std::floor(std::max(viewport->Pos.y,bounds.min.y-margin))},
    {std::ceil(std::min(viewport->Pos.x+viewport->Size.x,bounds.max.x+margin)),std::ceil(std::min(viewport->Pos.y+viewport->Size.y,bounds.max.y+margin))}};
  const float radius=theme::window_radius*scale;
  const float opacity=std::max(theme::content_opacity,theme::minimum_surface_opacity);
  sample(draw,bounds,radius,ImDrawFlags_RoundCornersAll,opacity);
  if(state.gradient.view) draw->AddImageRounded(reference(state.gradient_id),bounds.min,bounds.max,{0,0},{1,1},ImGui::GetColorU32(ImVec4{1,1,1,opacity}),radius);
  else { auto tint=theme::window_gradient_start; tint.w=opacity; draw->AddRectFilled(bounds.min,bounds.max,ImGui::GetColorU32(tint),radius); }
  auto overlay=theme::sidebar_overlay;
  overlay.w=std::max(overlay.w,(theme::sidebar_opacity-opacity)/std::max(.001f,1-opacity));
  draw->AddRectFilled(bounds.min,{bounds.min.x+sidebar,bounds.max.y},ImGui::GetColorU32(overlay),radius,ImDrawFlags_RoundCornersLeft);
}
void surface(ImDrawList* draw,Bounds bounds,float radius,ImVec4 tint,ImDrawFlags corners) {
  tint.w=std::max(tint.w,theme::minimum_surface_opacity);
  sample(draw,bounds,radius,corners,tint.w);
  draw->AddRectFilled(bounds.min,bounds.max,ImGui::GetColorU32(tint),radius,corners);
}
void surface(ImDrawList* draw,ImVec2 min,ImVec2 max,float radius,ImVec4 tint) { surface(draw,{min,max},radius,tint); }
bool render(ID3D11RenderTargetView* target,ImDrawData* data) {
  const int frame=ImGui::GetFrameCount();
  if(state.rendered_frame==frame) { remap(data,state.last_ready); return state.last_ready; }
  state.rendered_frame=frame;
  bool ready=false;
  if(state.visible && available() && target) {
    const float guard=theme::blur_radius*state.scale;
    for(const auto* draw : data->CmdLists) for(const auto& command : draw->CmdBuffer) if(command.ElemCount && command.TexRef._TexData==&state.blur_id) {
      state.area.min.x=std::min(state.area.min.x,command.ClipRect.x-guard);
      state.area.min.y=std::min(state.area.min.y,command.ClipRect.y-guard);
      state.area.max.x=std::max(state.area.max.x,command.ClipRect.z+guard);
      state.area.max.y=std::max(state.area.max.y,command.ClipRect.w+guard);
    }
    state.area.min.x=std::floor(std::max(data->DisplayPos.x,state.area.min.x));
    state.area.min.y=std::floor(std::max(data->DisplayPos.y,state.area.min.y));
    state.area.max.x=std::ceil(std::min(data->DisplayPos.x+data->DisplaySize.x,state.area.max.x));
    state.area.max.y=std::ceil(std::min(data->DisplayPos.y+data->DisplaySize.y,state.area.max.y));
    const auto framebuffer=data->FramebufferScale;
    const auto width=static_cast<UINT>((state.area.max.x-state.area.min.x)*framebuffer.x);
    const auto height=static_cast<UINT>((state.area.max.y-state.area.min.y)*framebuffer.y);
    if(width!=state.width || height!=state.height) {
      const auto small_width=std::max(1u,(width+theme::blur_downsample-1)/theme::blur_downsample);
      const auto small_height=std::max(1u,(height+theme::blur_downsample-1)/theme::blur_downsample);
      if(!image(state.source,width,height,false) || !image(state.ping,small_width,small_height,true) || !image(state.pong,small_width,small_height,true)) fallback();
      else { state.width=width; state.height=height; state.blur_id.SetTexID(identifier(state.ping.view.Get())); state.blur_id.SetStatus(ImTextureStatus_OK); }
    }
  }
  if(state.visible && available() && state.pong.view && target) {
    ComPtr<ID3D11Resource> resource; target->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> scene; resource.As(&scene);
    if(scene) {
      D3D11_TEXTURE2D_DESC desc{}; scene->GetDesc(&desc);
      const auto framebuffer=data->FramebufferScale;
      const UINT x=static_cast<UINT>(std::max(0.f,(state.area.min.x-data->DisplayPos.x)*framebuffer.x));
      const UINT y=static_cast<UINT>(std::max(0.f,(state.area.min.y-data->DisplayPos.y)*framebuffer.y));
      if(desc.SampleDesc.Count==1 && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM && x+state.width<=desc.Width && y+state.height<=desc.Height) {
        if(!state.target_reported && state.log){state.log("Menu render target: DXGI_FORMAT_R8G8B8A8_UNORM, samples=1.");state.target_reported=true;}
        Pipeline restore(state.context.Get());
        auto* context=state.context.Get(); context->OMSetRenderTargets(0,nullptr,nullptr);
        const D3D11_BOX box{x,y,0,x+state.width,y+state.height,1};
        context->CopySubresourceRegion(state.source.texture.Get(),0,0,0,0,scene.Get(),0,&box);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(state.vertex.Get(),nullptr,0); context->GSSetShader(nullptr,nullptr,0);
        auto* sampler=state.sampler.Get(); context->PSSetSamplers(0,1,&sampler);
        auto* buffer=state.constants.Get(); context->PSSetConstantBuffers(0,1,&buffer);
        context->OMSetBlendState(state.blend.Get(),nullptr,0xffffffff); context->OMSetDepthStencilState(state.depth.Get(),0); context->RSSetState(state.raster.Get());
        pass(state.source,state.ping,state.downsample.Get(),0,0);
        const float radius=theme::blur_radius*state.scale/theme::blur_downsample/3.230769231f;
        pass(state.ping,state.pong,state.gaussian.Get(),radius*framebuffer.x,0);
        pass(state.pong,state.ping,state.gaussian.Get(),0,radius*framebuffer.y);
        ready=SUCCEEDED(state.device->GetDeviceRemovedReason());
        if(!ready) fallback();
      } else fallback();
    } else fallback();
  }
  remap(data,ready);
  state.last_ready=ready;
  return ready;
}
}
