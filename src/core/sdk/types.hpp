#pragma once
#include <cstdint>
#include <array>
namespace iris::sdk {
using Address = std::uintptr_t;
using GloveUpdate = void(__fastcall*)(void*);
using FrameStage = void(__fastcall*)(void*, int);
using Command = void(__fastcall*)();
using SetModel = void(__fastcall*)(void*, const char*);
using SubclassChanged = void(__fastcall*)(void*);
using MakeToken = void*(__cdecl*)(std::uint32_t*, const char*);
using RebuildMaterials = void(__fastcall*)(void*, bool);
using ItemInteger = int(__fastcall*)(void*);
using ItemWear = float(__fastcall*)(void*, float);
using IterateAttributes = bool(__fastcall*)(void*, void*);
using DefaultHandModel = bool(__fastcall*)(void*);
using CameraThink = void(__fastcall*)(void*, int);
using MovementInput = void(__fastcall*)(void*,int,float*);
using ViewmodelSettings = void(__fastcall*)(std::array<float,3>*,float*,bool);
using PawnViewmodelSettings = void(__fastcall*)(void*,std::array<float,3>*,float*);
using AcceptMatch = bool(__fastcall*)(void*,const char*);
using ParticleCreate = std::uint32_t*(__fastcall*)(void*,std::uint32_t*,const void*);
using ParticleControl = void(__fastcall*)(void*,std::uint32_t,int,const std::array<float,3>*);
using ParticleDestroy = void(__fastcall*)(void*,std::uint32_t,bool);
using ParticleRelease = void(__fastcall*)(void*,std::uint32_t);
using HudUpdate = void(__fastcall*)(void*, void*);
using ItemName = const char*(__fastcall*)(void*, bool);
using NameContext = void*(__fastcall*)();
using RebuildName = void(__fastcall*)(void*, void*, void*, void*);
struct Fields {
    int pawn, health, services, weapons, owner, team, flags, move_type;
    int attributes, item, definition, id_high, paint, seed, wear, stattrak, name;
    int scene, model, mesh, model_name, subclass;
    int gloves, glove_reapply, initialized, quality;
};
}
