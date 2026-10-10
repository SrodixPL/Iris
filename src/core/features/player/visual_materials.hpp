#pragma once
#include <string>
namespace iris::visuals::native {
inline std::string material_text(unsigned style,bool hidden) {
    constexpr const char* prefix="<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n{";
    constexpr const char* shaders[]{"csgo_unlitgeneric.vfx","csgo_effects.vfx","csgo_effects.vfx","solidcolor.vfx"};
    constexpr const char* extras[]{
        " F_PAINT_VERTEX_COLORS=1",
        " F_ADDITIVE_BLEND=1 g_flColorBoost=4.0 g_flOpacityScale=1.0 g_flFresnelExponent=1.5 g_flFresnelFalloff=5.0 g_flFresnelMin=1.0 g_flFresnelMax=0.0",
        " F_ADDITIVE_BLEND=1 g_flColorBoost=2.25 g_flOpacityScale=0.65 g_flFresnelExponent=0.75 g_flFresnelFalloff=1.0 g_flFresnelMin=0.0 g_flFresnelMax=1.0",
        " F_PAINT_VERTEX_COLORS=1"};
    constexpr const char* common=" F_RENDER_BACKFACES=1 F_TRANSLUCENT=1 F_BLEND_MODE=1 F_DISABLE_Z_WRITE=1 F_DISABLE_Z_PREPASS=1 g_vColorTint=[1.0,1.0,1.0,1.0] g_tColor=resource:\"materials/dev/primary_white_color_tga_21186c76.vtex\" g_tMask1=resource:\"materials/default/default_mask_tga_fde710a5.vtex\" g_tMask2=resource:\"materials/default/default_mask_tga_fde710a5.vtex\" g_tMask3=resource:\"materials/default/default_mask_tga_fde710a5.vtex\"";
    style=style<4?style:0;
    return std::string(prefix)+"shader=\""+shaders[style]+"\""+common+extras[style]+" F_DISABLE_Z_BUFFERING="+(hidden?"1":"0")+" }";
}
}
