#include "core/features/inventory/preview_receipts.hpp"
#include "core/features/runtime.hpp"
#include "core/sdk/binding_values.hpp"

namespace iris::cosmetics::detail {
namespace {
namespace b=sdk::binding_values;
bool close(float a,float c) noexcept {
    return std::isfinite(a)&&std::isfinite(c)&&std::abs(a-c)<=std::max(0.0001f,std::abs(c)*0.000001f);
}
bool equal(const std::array<float,3>& a,const std::array<float,3>& c) noexcept {
    for(unsigned i=0;i<3;++i)if(!close(a[i],c[i]))return false;
    return true;
}
bool on_thread(PreviewReceipts& r) noexcept {
    if(r.thread.load(std::memory_order_acquire)!=GetCurrentThreadId()) {
        r.off_thread.fetch_add(1,std::memory_order_relaxed);return false;
    }
    return true;
}
std::optional<PreviewCameraFrame> sample(PreviewReceipts& r) noexcept {
    try {
        if(!r.job || get<Address>(r.generator)!=r.client+b::client_icon_generator_vtable ||
           get<Address>(r.generator+b::layout_preview_item)!=r.item ||
           get<std::uint64_t>(r.generator+b::layout_preview_job)!=r.job)return {};
        const auto map=get<Address>(r.generator+b::layout_preview_map);
        if(!map || get<Address>(map)!=r.client+b::client_preview_scene_vtable ||
           get<Address>(map+b::layout_preview_scene_camera)!=r.client+b::client_preview_inner_camera_vtable)return {};
        const auto input=get<std::array<float,3>>(map+b::layout_preview_camera_source_angles);
        if(!equal(input,r.expected_angles) || !close(get<float>(map+b::layout_preview_camera_source_distance),r.expected_distance))return {};
        PreviewCameraFrame frame{map,get<std::array<float,3>>(map+b::layout_preview_camera_origin),
            get<std::array<float,3>>(map+b::layout_preview_camera_angles),get<float>(map+b::layout_preview_camera_fov),preview_target(r.generator)};
        const auto current=get<Address>(map+b::layout_preview_scene_camera+b::layout_preview_camera_current_state);
        if(!current || !equal(frame.origin,get<std::array<float,3>>(current+b::layout_preview_camera_state_origin)) ||
           !equal(frame.angles,get<std::array<float,3>>(current+b::layout_preview_camera_state_angles)) ||
           !std::isfinite(frame.fov) || frame.fov<=0 || frame.fov>=180)return {};
        return frame;
    } catch(...) {return {};}
}
void __fastcall camera_applied(void* scene) {
    auto& r=runtime().preview_receipts;
    const bool owner=on_thread(r) && r.job && memory::read<Address>(r.generator+b::layout_preview_map).value_or(0)==reinterpret_cast<Address>(scene);
    const auto generation=owner?r.generation:0;
    const auto before=owner?sample(r):std::optional<PreviewCameraFrame>{};
    r.original_apply(scene);
    if(!owner)return;
    r.observed|=1;
    const auto after=sample(r);
    // The first application may replace authored source inputs. Both sides
    // must already match the queued numeric request before admitting a frame.
    r.apply(generation,before,after);
}
bool __fastcall scene_submitted(void* generator) {
    auto& r=runtime().preview_receipts;
    const bool owner=on_thread(r) && reinterpret_cast<Address>(generator)==r.generator;
    const auto generation=owner?r.generation:0;
    const auto before=owner?sample(r):std::optional<PreviewCameraFrame>{};
    const bool accepted=r.original_submit(generator);
    if(!owner)return accepted;
    r.observed|=2;
    const auto after=sample(r);
    r.submit(generation,accepted,before,after);
    return accepted;
}
void __fastcall scene_completed(void* generator) {
    auto& r=runtime().preview_receipts;
    const bool owner=on_thread(r) && reinterpret_cast<Address>(generator)==r.generator;
    const auto generation=owner?r.generation:0;
    const auto before=owner?sample(r):std::optional<PreviewCameraFrame>{};
    const bool ready=owner && memory::read<int>(r.generator+b::layout_preview_phase).value_or(0)>=9;
    r.original_complete(generator);
    if(!owner)return;
    r.observed|=4;
    const auto after=sample(r);
    if(r.complete(generation,ready,before,after)) {
        const auto now=GetTickCount64();
        if(now-r.logged>5000) {
            r.logged=now;char message[260]{};
            std::snprintf(message,sizeof(message),"Preview camera receipts: applied/submitted/completed generation=%llu thread=%u angles=%.3f,%.3f,%.3f distance=%.3f off-thread=%u.",
                generation,GetCurrentThreadId(),r.expected_angles[0],r.expected_angles[1],r.expected_angles[2],r.expected_distance,r.off_thread.load());
            overlay_log(message);
        }
    }
}
}
PreviewTarget preview_target(Address generator) {
    const bool supersampling=(get<std::uint32_t>(generator+b::layout_preview_flags)&b::layout_preview_supersample_flags_mask)!=0 &&
        get<bool>(generator+b::layout_preview_supersampling);
    const int scale=supersampling?b::layout_preview_supersample_scale:1;
    auto texture=supersampling?get<Address>(generator+b::layout_preview_supersampled_texture):Address{};
    if(!texture)texture=get<Address>(generator+b::layout_preview_texture);
    const int width=get<int>(generator+b::layout_preview_width),height=get<int>(generator+b::layout_preview_height);
    if(width<=0 || height<=0 || width>2048/scale || height>2048/scale)return {};
    return {texture,width*scale,height*scale};
}
void PreviewReceipts::invalidate() noexcept {
    reset();job=0;
}
void bind_preview_receipts(Runtime& state) {
    auto& r=state.preview_receipts;
    r.apply_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+b::client_preview_camera_apply),reinterpret_cast<void*>(&camera_applied));
    r.submit_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+b::client_preview_scene_submit),reinterpret_cast<void*>(&scene_submitted));
    r.complete_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+b::client_preview_scene_complete),reinterpret_cast<void*>(&scene_completed));
    r.original_apply=r.apply_hook->original<PreviewReceipts::CameraApply>();
    r.original_submit=r.submit_hook->original<PreviewReceipts::Submit>();
    r.original_complete=r.complete_hook->original<PreviewReceipts::Complete>();
}
void enable_preview_receipts(Runtime& state) {
    auto& r=state.preview_receipts;r.apply_hook->enable();r.submit_hook->enable();r.complete_hook->enable();
}
void expect_preview_frame(Runtime& state) {
    auto& r=state.preview_receipts;r.invalidate();
    if(!state.preview.camera_supported)return;
    r.client=state.client;r.generator=preview_generator(state);r.item=reinterpret_cast<Address>(state.preview.item.data());r.job=state.preview.job;
    r.expected_angles=state.preview.camera_settings.applied_angles();r.expected_distance=state.preview.camera_settings.applied_distance();
}
bool completed_preview_frame(Runtime& state) {
    auto& r=state.preview_receipts;
    return r.accepts(sample(r));
}
}
