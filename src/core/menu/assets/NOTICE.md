The bundled preview geometry, baked diffuse vertex colors, and cstrike hitboxes
derive from Valve's Counter-Strike 2 SAS player asset
`agents/models/ctm_sas/ctm_sas.vmdl`, extracted from the locally installed game.
These game assets remain copyright Valve Corporation. They are used only for
the local player preview; no native memory layouts or bindings are derived here.

Exported using Source 2 Viewer CLI 20.0 (ValveResourceFormat, MIT license):
https://github.com/ValveResourceFormat/ValveResourceFormat
Only third-person body and default gloves are included. The mesh uses its
authored rifle idle pose and hitbox bounds, transformed together. Materials are baked into vertex colors;
no chams, animation system, or game scene callbacks are part of this fallback.
