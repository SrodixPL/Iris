#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
namespace iris::cosmetics::detail {
// The current client uses CCSItemIconGeneratorGameSystem's scene renderer.
// CEconItemView::GetInventoryImageRgba only registers a legacy request and
// never submits this scene. All generator calls run on the game frame thread.
Address preview_generator(const Runtime& state) {
    const auto generator = state.client + bindings::icon_generator;
    if (get<Address>(generator) != state.client + bindings::icon_generator_vtable)
        throw std::runtime_error("Preview scene renderer binding changed.");
    return generator;
}
void clear_preview(Runtime& state) {
    auto& preview = state.preview;
    state.preview_receipts.invalidate();
    if (!preview.constructed && !preview.modality) return;
    const auto generator = preview_generator(state);
    if (preview.modality) {
        // This native operation cancels our preview request and schedules scene
        // destruction. Keep its borrowed item alive until destruction completes.
        using Enable = void(__fastcall*)(void*, bool);
        reinterpret_cast<Enable>(state.client + bindings::icon_preview_enable)(reinterpret_cast<void*>(generator), false);
        preview.modality = false;
        preview.retiring = true;
    }
    // Queue synchronously owns its settings copy; this caller copy is not a
    // borrowed scene item and can be cleared independently of map retirement.
    preview.camera_settings.clear();
    preview.camera_supported=false;
    if (get<Address>(generator + iris::sdk::binding_values::layout_preview_map) || get<Address>(generator + iris::sdk::binding_values::layout_preview_item) == reinterpret_cast<Address>(preview.item.data()) ||
        get<int>(generator + iris::sdk::binding_values::layout_preview_pending) != -1) return;
    if (preview.constructed)
        reinterpret_cast<void(__fastcall*)(void*)>(state.client + bindings::item_destroy)(preview.item.data());
    preview.constructed = preview.retiring = false;
    preview.job = 0;
    preview.rendering.reset();
    publish_finishes(state);
}

namespace {
void copy_preview_frame(Runtime& state,Address generator,ULONGLONG now,int phase) {
    auto& preview=state.preview;
    if(preview.camera_supported && !completed_preview_frame(state)) {
        if(now-preview.diagnostic>5000) {
            preview.diagnostic=now;char message[180]{};
            const auto& receipts=state.preview_receipts;
            std::snprintf(message,sizeof(message),"Preview waiting for camera receipts: generation=%llu observed=%u applied=%llu submitted=%llu completed=%llu off-thread=%u.",
                receipts.generation,receipts.observed,receipts.applied_generation,receipts.submitted_generation,receipts.completed_generation,receipts.off_thread.load());
            overlay_log(message);
        }
        return;
    }
    // Accumulate a 60Hz deadline rather than rounding each game frame's
    // interval upward. Do not catch up missed frames with extra native copies.
    const auto clock=now*60;
    if(clock<preview.sampled)return;
    preview.sampled=(!preview.sampled || clock-preview.sampled>1000)?clock+1000:preview.sampled+1000;
    // Match the native render attachment and icon reader. External preview
    // can render solely into the supersampled target, leaving the base empty.
    const auto target=preview_target(generator);
    const auto texture=target.texture;
    const int width=target.width,height=target.height;
    const auto device = get<Address>(state.client + bindings::render_device);
    const auto report_readback = [&](const char* result) {
        if (now - preview.diagnostic <= 5000) return;
        preview.diagnostic = now;
        char message[240]{};
        std::snprintf(message,sizeof(message),"Preview readback: %s; phase=%d job=%llu texture=%p device=%p size=%dx%d.",
            result,phase,preview.job,reinterpret_cast<void*>(texture),reinterpret_cast<void*>(device),width,height);
        overlay_log(message);
    };
    if (!texture || !device || width <= 0 || height <= 0 || width > 2048 || height > 2048) {
        report_readback("target unavailable");
        return;
    }
    const auto table = get<Address>(device);
    // IRenderDevice::ReadTexturePixels, reviewed at the native icon-cache
    // writer: texture, source rectangle, mip, slice, destination rectangle,
    // RGBA8 buffer, flags, row pitch. No borrowed GPU resources enter ImGui.
    using ReadPixels = bool(__fastcall*)(void*, void*, const int*, int, int, const int*, void*, int, int);
    const auto read_pixels = reinterpret_cast<ReadPixels>(get<Address>(table + iris::sdk::binding_values::layout_texture_read_pixels_slot));
    const std::array<int,4> rectangle{0,0,width,height};
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
    if (!read_pixels(reinterpret_cast<void*>(device),reinterpret_cast<void*>(texture),rectangle.data(),0,0,rectangle.data(),pixels.data(),0,width*4)) {
        report_readback("native copy rejected");
        return;
    }
    bool visible = false;
    for (std::size_t i = 3; i < pixels.size(); i += 4) if (pixels[i]) { visible = true; break; }
    if (!visible) {
        const bool color = std::any_of(pixels.begin(),pixels.end(),[](unsigned char value){return value!=0;});
        report_readback(color ? "copied color without alpha" : "copied empty target");
        return;
    }
    const bool first = preview.image.rgba.empty();
    preview.image.selection = *preview.rendering;
    preview.image.rgba = std::move(pixels);
    preview.image.width = width; preview.image.height = height;
    preview.image.detail.clear(); ++preview.image.revision;
    preview.image.interactive=preview.camera_supported;
    if (first) overlay_log("Embedded preview pixels received from the native scene render target.");
}
}
void update_preview(Runtime& state) {
    auto& preview = state.preview;
    const auto now = GetTickCount64();
    if (preview.failed) { clear_preview(state); return; }
    if (state.stopping || now - preview.touched > 1500) { clear_preview(state); return; }
    if(!preview.requested)return;
    // Coalesce short finish edits while bounding the wait during a continuous
    // slider drag. Scene replacement still follows native retirement.
    if(now-preview.changed<40 && now-preview.started<100)return;
    const auto generator = preview_generator(state);
    if (preview.retiring || !preview.rendering || *preview.rendering != *preview.requested) {
        clear_preview(state);
        if (preview.constructed || preview.retiring) return;
        // Respect the game's own active icon/workshop preview requests. Enter
        // external preview mode only after the normal queue has drained.
        if (get<bool>(generator + iris::sdk::binding_values::layout_preview_busy) || get<Address>(generator + iris::sdk::binding_values::layout_preview_item) || get<int>(generator + iris::sdk::binding_values::layout_preview_pending) != -1) return;
        reinterpret_cast<void*(__fastcall*)(void*)>(state.client + bindings::item_construct)(preview.item.data());
        preview.constructed = true;
        preview.rendering = preview.requested;
        const auto item = reinterpret_cast<Address>(preview.item.data());
        if (!memory::write(item + state.fields.definition, static_cast<std::uint16_t>(preview.rendering->definition)) ||
            !memory::write(item + state.fields.initialized, true) ||
            !memory::write(item + state.fields.quality, is_glove(preview.rendering->definition) ? 3 : 0))
            throw std::runtime_error("Cannot initialize preview item.");
        publish_finishes(state);
        preview.camera_supported=preview.camera_settings.prepare(state.client,item);
        if(preview.camera_supported)preview.camera_settings.apply(preview.pose);
        if(preview.camera_supported)overlay_log("Native preview camera enabled with a private authored-settings copy.");
        using Enable = void(__fastcall*)(void*, bool);
        using Queue = std::uint64_t(__fastcall*)(void*, void*, void*, void*, void*);
        reinterpret_cast<Enable>(state.client + bindings::icon_preview_enable)(reinterpret_cast<void*>(generator), true);
        preview.modality = true;
        preview.job = reinterpret_cast<Queue>(state.client + bindings::icon_preview_queue)(reinterpret_cast<void*>(generator), preview.item.data(), preview.camera_settings.data(), nullptr, nullptr);
        if (!preview.job) throw std::runtime_error("Preview scene request was rejected.");
        expect_preview_frame(state);
        preview.started = now;
        preview.submitted_pose=preview.pose;preview.camera_retry=now+500;
        preview.sampled = preview.diagnostic = 0;
        char message[170]{};
        std::snprintf(message,sizeof(message),"Preview scene queued: job=%llu definition=%d paint=%d wear=%.3f seed=%d.",preview.job,preview.rendering->definition,preview.rendering->paint,preview.rendering->wear,preview.rendering->seed);
        overlay_log(message);
        // Keep the last completed frame during finish edits. Its selection
        // remains the old one until new pixels have actually been copied.
        if (preview.image.selection.definition != preview.rendering->definition || preview.image.rgba.empty())
            preview.image = PreviewImage{*preview.rendering, preview.image.revision + 1, 0, 0, {}, "Rendering preview"};
    }
    const int phase = get<int>(generator + iris::sdk::binding_values::layout_preview_phase);
    const bool owned = get<Address>(generator + iris::sdk::binding_values::layout_preview_item) == reinterpret_cast<Address>(preview.item.data()) &&
        get<std::uint64_t>(generator + iris::sdk::binding_values::layout_preview_job) == preview.job;
    if (!owned || (phase != 8 && phase != 9)) {
        if (now - preview.diagnostic > 5000) {
            preview.diagnostic = now;
            char message[140]{};
            std::snprintf(message,sizeof(message),"Preview scene waiting: phase=%d owned=%d map=%p.",phase,owned,reinterpret_cast<void*>(get<Address>(generator + iris::sdk::binding_values::layout_preview_map)));
            overlay_log(message);
        }
        return;
    }
    bool prepared=false;
    if(!preview.camera_supported && now>=preview.camera_retry) {
        preview.camera_retry=now+500;
        prepared=preview.camera_supported=preview.camera_settings.prepare(state.client,reinterpret_cast<Address>(preview.item.data()));
    }
    if(preview.camera_supported && (prepared || preview.submitted_pose!=preview.pose)) {
        // Coalesce motion while a native pose is still in flight. Replacing
        // its generation every game frame would starve the completion gate.
        if(!prepared && !completed_preview_frame(state))return;
        // Publish the completed preceding pose before queuing the next one.
        // Continuous dragging must not invalidate every frame before copying.
        copy_preview_frame(state,generator,now,phase);
        preview.camera_settings.apply(preview.pose);
        using Queue=std::uint64_t(__fastcall*)(void*,void*,void*,void*,void*);
        const auto reused=reinterpret_cast<Queue>(state.client+bindings::icon_preview_queue)(reinterpret_cast<void*>(generator),preview.item.data(),preview.camera_settings.data(),nullptr,nullptr);
        if(reused!=preview.job)throw std::runtime_error("Native camera update did not preserve its owned scene identity.");
        preview.submitted_pose=preview.pose;preview.camera_changed=now;
        expect_preview_frame(state);
        // Keep the last completed image while the native update consumes the
        // edit. This does not label a queued pose as an already-rendered pose.
        return;
    }
    copy_preview_frame(state,generator,now,phase);
}
bool guarded_preview(Runtime* state) {
    __try { update_preview(*state); return true; }
    __except (GetExceptionCode() == 0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
