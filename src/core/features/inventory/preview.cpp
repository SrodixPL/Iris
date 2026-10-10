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
    if (get<Address>(generator + iris::sdk::binding_values::layout_preview_map) || get<Address>(generator + iris::sdk::binding_values::layout_preview_item) == reinterpret_cast<Address>(preview.item.data()) ||
        get<int>(generator + iris::sdk::binding_values::layout_preview_pending) != -1) return;
    if (preview.constructed)
        reinterpret_cast<void(__fastcall*)(void*)>(state.client + bindings::item_destroy)(preview.item.data());
    preview.constructed = preview.retiring = false;
    preview.job = 0;
    preview.rendering.reset();
    publish_finishes(state);
}
void update_preview(Runtime& state) {
    auto& preview = state.preview;
    const auto now = GetTickCount64();
    if (preview.failed) { clear_preview(state); return; }
    if (state.stopping || now - preview.touched > 1500) { clear_preview(state); return; }
    if (!preview.requested || now - preview.changed < 200) return;
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
        using Enable = void(__fastcall*)(void*, bool);
        using Queue = std::uint64_t(__fastcall*)(void*, void*, void*, void*, void*);
        reinterpret_cast<Enable>(state.client + bindings::icon_preview_enable)(reinterpret_cast<void*>(generator), true);
        preview.modality = true;
        preview.job = reinterpret_cast<Queue>(state.client + bindings::icon_preview_queue)(reinterpret_cast<void*>(generator), preview.item.data(), nullptr, nullptr, nullptr);
        if (!preview.job) throw std::runtime_error("Preview scene request was rejected.");
        preview.started = now;
        preview.sampled = preview.diagnostic = 0;
        char message[170]{};
        std::snprintf(message,sizeof(message),"Preview scene queued: job=%llu definition=%d paint=%d wear=%.3f seed=%d.",preview.job,preview.rendering->definition,preview.rendering->paint,preview.rendering->wear,preview.rendering->seed);
        overlay_log(message);
        preview.image = PreviewImage{*preview.rendering, preview.image.revision + 1, 0, 0, {}, "Rendering preview"};
    }
    const int phase = get<int>(generator + iris::sdk::binding_values::layout_preview_phase);
    const bool owned = get<Address>(generator + iris::sdk::binding_values::layout_preview_item) == reinterpret_cast<Address>(preview.item.data()) &&
        get<std::uint64_t>(generator + iris::sdk::binding_values::layout_preview_job) == preview.job;
    if (!owned || (phase != 8 && phase != 9)) {
        if (now - preview.started > 10000 && now - preview.diagnostic > 5000) {
            preview.diagnostic = now;
            char message[140]{};
            std::snprintf(message,sizeof(message),"Preview scene waiting: phase=%d owned=%d map=%p.",phase,owned,reinterpret_cast<void*>(get<Address>(generator + iris::sdk::binding_values::layout_preview_map)));
            overlay_log(message);
        }
        return;
    }
    if (now - preview.sampled < 500) return;
    preview.sampled = now;
    const auto texture = get<Address>(generator + iris::sdk::binding_values::layout_preview_texture);
    const int width = get<int>(generator + iris::sdk::binding_values::layout_preview_width), height = get<int>(generator + iris::sdk::binding_values::layout_preview_height);
    const auto device = get<Address>(state.client + bindings::render_device);
    if (!texture || !device || width <= 0 || height <= 0 || width > 2048 || height > 2048) return;
    const auto table = get<Address>(device);
    // IRenderDevice::ReadTexturePixels, reviewed at the native icon-cache
    // writer: texture, source rectangle, mip, slice, destination rectangle,
    // RGBA8 buffer, flags, row pitch. No borrowed GPU resources enter ImGui.
    using ReadPixels = bool(__fastcall*)(void*, void*, const int*, int, int, const int*, void*, int, int);
    const auto read_pixels = reinterpret_cast<ReadPixels>(get<Address>(table + iris::sdk::binding_values::layout_texture_read_pixels_slot));
    const std::array<int,4> rectangle{0,0,width,height};
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
    if (!read_pixels(reinterpret_cast<void*>(device),reinterpret_cast<void*>(texture),rectangle.data(),0,0,rectangle.data(),pixels.data(),0,width*4)) return;
    bool visible = false;
    for (std::size_t i = 3; i < pixels.size(); i += 4) if (pixels[i]) { visible = true; break; }
    if (!visible) return;
    const bool first = preview.image.rgba.empty();
    preview.image.rgba = std::move(pixels);
    preview.image.width = width; preview.image.height = height;
    preview.image.detail.clear(); ++preview.image.revision;
    if (first) overlay_log("Embedded preview pixels received from the native scene render target.");
}
bool guarded_preview(Runtime* state) {
    __try { update_preview(*state); return true; }
    __except (GetExceptionCode() == 0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
