#include "core/features/world/render.hpp"
#include <d3dcompiler.h>
#include <array>
#include <stdexcept>
#include <cmath>
namespace iris {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Environment renderer initialization failed.");}
constexpr char shader[]=R"(
Texture2D scene:register(t0); SamplerState linearSampler:register(s0);
cbuffer Settings:register(b0){
 float4 grade; // brightness, vibrance, contrast, gamma
 float4 look; // warmth, vignette, sharpness, haze
 float4 fog; // enabled, density, distance, forward-Z near plane
 float4 fogColor;
 float4 dimensions; // width, height, night, intensity
};
struct Vertex {float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};
Vertex VS(uint id:SV_VertexID){Vertex o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float hash(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float4 PS(Vertex v):SV_Target{
 float2 uv=v.uv;float3 rgb=scene.Sample(linearSampler,uv).rgb;
 float2 texel=1/dimensions.xy;
 float3 neighbors=(scene.Sample(linearSampler,uv+float2(texel.x,0)).rgb+scene.Sample(linearSampler,uv-float2(texel.x,0)).rgb+
  scene.Sample(linearSampler,uv+float2(0,texel.y)).rgb+scene.Sample(linearSampler,uv-float2(0,texel.y)).rgb)*.25;
 rgb+=look.z*(rgb-neighbors);
 rgb*=grade.x;
 float luma=dot(rgb,float3(.2126,.7152,.0722));
 rgb=lerp(luma.xxx,rgb,grade.y);
 rgb=(rgb-.5)*grade.z+.5;
 rgb*=float3(1+look.x*.16,1+look.x*.025,1-look.x*.16);
 rgb=pow(saturate(rgb),1/max(.1,grade.w));
 rgb*=1-look.y*smoothstep(.2,.8,length(uv-.5));
 if(dimensions.z>.5)rgb*=lerp(float3(1,1,1),float3(.16,.23,.36),dimensions.w);
 return float4(saturate(rgb),1);
}
)";
struct Pipeline {
 ID3D11DeviceContext* context;
 ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11GeometryShader> geometry;
 ComPtr<ID3D11HullShader> hull;ComPtr<ID3D11DomainShader> domain;
 std::array<ID3D11ClassInstance*,256> hs_instances{},ds_instances{};UINT hs_count=256,ds_count=256;
 ComPtr<ID3D11Predicate> predicate;BOOL predicate_value;
 std::array<ID3D11ClassInstance*,256> vs_instances{},ps_instances{},gs_instances{};UINT vs_count=256,ps_count=256,gs_count=256;
 ComPtr<ID3D11Buffer> constant;ComPtr<ID3D11ShaderResourceView> source,depth_source;ComPtr<ID3D11SamplerState> sampler;
 ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology;
 ComPtr<ID3D11BlendState> blend;FLOAT blend_factor[4]{};UINT mask;
 ComPtr<ID3D11DepthStencilState> depth;UINT stencil;
 ComPtr<ID3D11RasterizerState> raster;UINT viewport_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
 std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports;
 std::array<ID3D11RenderTargetView*,8> targets{};ComPtr<ID3D11DepthStencilView> target_depth;
 explicit Pipeline(ID3D11DeviceContext* c):context(c){
  c->VSGetShader(&vertex,vs_instances.data(),&vs_count);c->PSGetShader(&pixel,ps_instances.data(),&ps_count);c->GSGetShader(&geometry,gs_instances.data(),&gs_count);
  c->HSGetShader(&hull,hs_instances.data(),&hs_count);c->DSGetShader(&domain,ds_instances.data(),&ds_count);c->GetPredication(&predicate,&predicate_value);
  c->PSGetConstantBuffers(0,1,&constant);c->PSGetShaderResources(0,1,&source);c->PSGetSamplers(0,1,&sampler);
  c->PSGetShaderResources(1,1,&depth_source);
  c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
  c->OMGetBlendState(&blend,blend_factor,&mask);c->OMGetDepthStencilState(&depth,&stencil);
  c->RSGetState(&raster);c->RSGetViewports(&viewport_count,viewports.data());c->OMGetRenderTargets(8,targets.data(),&target_depth);
 }
 ~Pipeline(){auto* c=context;
  c->VSSetShader(vertex.Get(),vs_instances.data(),vs_count);c->PSSetShader(pixel.Get(),ps_instances.data(),ps_count);c->GSSetShader(geometry.Get(),gs_instances.data(),gs_count);
  c->HSSetShader(hull.Get(),hs_instances.data(),hs_count);c->DSSetShader(domain.Get(),ds_instances.data(),ds_count);c->SetPredication(predicate.Get(),predicate_value);
  c->PSSetConstantBuffers(0,1,constant.GetAddressOf());c->PSSetShaderResources(0,1,source.GetAddressOf());c->PSSetSamplers(0,1,sampler.GetAddressOf());
  c->PSSetShaderResources(1,1,depth_source.GetAddressOf());
  c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
  c->OMSetBlendState(blend.Get(),blend_factor,mask);c->OMSetDepthStencilState(depth.Get(),stencil);
  c->RSSetState(raster.Get());c->RSSetViewports(viewport_count,viewports.data());c->OMSetRenderTargets(8,targets.data(),target_depth.Get());
  for(auto* p:targets)if(p)p->Release();
  for(UINT i=0;i<hs_count;++i)hs_instances[i]->Release();for(UINT i=0;i<ds_count;++i)ds_instances[i]->Release();
  for(UINT i=0;i<vs_count;++i)vs_instances[i]->Release();for(UINT i=0;i<ps_count;++i)ps_instances[i]->Release();for(UINT i=0;i<gs_count;++i)gs_instances[i]->Release();
 }
};
}
EnvironmentRender::EnvironmentRender(ID3D11Device* device):device_(device){
 ComPtr<ID3DBlob> vs,ps,errors;
 check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors));
 check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors));
 check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_));
 check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_));
 D3D11_BUFFER_DESC b{};b.ByteWidth=80;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&b,nullptr,&constants_));
 D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;check(device->CreateSamplerState(&s,&sampler_));
 D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;check(device->CreateBlendState(&blend,&blend_));
 D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;check(device->CreateDepthStencilState(&depth,&depth_));
 D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;check(device->CreateRasterizerState(&r,&raster_));
}
void EnvironmentRender::draw(ID3D11DeviceContext* context,IDXGISwapChain* chain,const ui::WorldState& w,float,ID3D11Texture2D*){
 if(!w.enabled || !chain)return;
 ComPtr<ID3D11Texture2D> back; if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&back))))return;
 D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
 // SDR only. Do not interpret an HDR swap chain as an eight-bit scene.
 if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return;
 D3D11_TEXTURE2D_DESC cached{};if(copy_)copy_->GetDesc(&cached);
 if(!copy_ || cached.Width!=desc.Width || cached.Height!=desc.Height || cached.Format!=desc.Format){
  copy_.Reset();source_.Reset();auto d=desc;d.SampleDesc={1,0};d.MipLevels=d.ArraySize=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=d.MiscFlags=0;
  check(device_->CreateTexture2D(&d,nullptr,&copy_));check(device_->CreateShaderResourceView(copy_.Get(),nullptr,&source_));
 }
 Pipeline saved(context);
 context->OMSetRenderTargets(0,nullptr,nullptr);
 if(desc.SampleDesc.Count>1)context->ResolveSubresource(copy_.Get(),0,back.Get(),0,desc.Format);else context->CopyResource(copy_.Get(),back.Get());
 ComPtr<ID3D11RenderTargetView> target;check(device_->CreateRenderTargetView(back.Get(),nullptr,&target));
 std::array<float,20> values{w.brightness*w.ambient,w.vibrance,w.contrast,w.gamma,w.warmth,w.vignette,w.sharpness,0,
  0.f,w.fog_density,w.fog_distance,7.f,w.fog_color.x,w.fog_color.y,w.fog_color.z,0,
  static_cast<float>(desc.Width),static_cast<float>(desc.Height),w.night_mode?1.f:0.f,w.night_intensity};
 context->UpdateSubresource(constants_.Get(),0,nullptr,values.data(),0,0);
 context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);context->OMSetBlendState(blend_.Get(),nullptr,~0u);context->OMSetDepthStencilState(depth_.Get(),0);
 context->RSSetState(raster_.Get());D3D11_VIEWPORT viewport{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height),0,1};context->RSSetViewports(1,&viewport);
 context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context->VSSetShader(vertex_.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->PSSetShader(pixel_.Get(),nullptr,0);
 context->PSSetShaderResources(0,1,source_.GetAddressOf());context->PSSetSamplers(0,1,sampler_.GetAddressOf());context->PSSetConstantBuffers(0,1,constants_.GetAddressOf());
 ID3D11ShaderResourceView* fog_source=nullptr;context->PSSetShaderResources(1,1,&fog_source);
 context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);context->SetPredication(nullptr,FALSE);
 context->Draw(3,0);
 ID3D11ShaderResourceView* empty{};context->PSSetShaderResources(0,1,&empty);
 context->PSSetShaderResources(1,1,&empty);
}
}
