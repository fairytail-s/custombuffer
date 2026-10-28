# shadow-simple

```
RenderChunk/             terrain: casts + receives (opaque, alpha-tested and transparent)
Actor/                   entities: casts + receives (opaque)
utils/shadow.h           the only shadow code: settings, sun, 2-cascade map space, soft-edge lookup
```

The folder that contains `utils/` (this folder, `example/shadow/`) must be on the shader include path,
so `#include <utils/shadow.h>` resolves. Settings (`SHADOW_RADIUS_NEAR`, `SHADOW_RADIUS`, `SHADOW_BRIGHTNESS`, `SHADOW_BIAS`, `SHADOW_SLOPE_BIAS`, `SHADOW_TAPS`, `SHADOW_COLORED`, ...)
are at the top of `utils/shadow.h`; the map resolution is the default in `uniforms/ShadowRes.json`.

## Coloured shadows and depth format (needs the updated CustomBuffers mod)

Same method as Iris: `s_ShadowMap` = opaque depth (shadowtex1), `s_ShadowAll` = depth of everything (shadowtex0),
`s_ShadowColor` = colour + alpha of the nearest caster (shadowcolor0). Stained glass, water and ice cast tinted light.
Transparent terrain passes use the uniform `ShadowTransparentPass`; the buffers are `buffers/ShadowAll.json` (slot 6) and
`buffers/ShadowColor.json` (slot 7). Set `SHADOW_COLORED 0` in `utils/shadow.h` to turn it off.

`uniforms/ShadowRes.json` is a `vec4`: `x` = map resolution per cascade (default 1024), `y` = depth bits of the maps (16 default, 24, or 32 = 32 bit float).


## Performance notes

- `uniforms/ShadowCull.json` holds the map radius and depth range (`SHADOW_RADIUS`, `SHADOW_DEPTH_RANGE` read from it) and
  turns on chunk culling in the mod (`z = 1`, terrain only). Keep `x` and `y` identical in `RenderChunk` and `Actor`.
- 2-Cascade Shadow Mapping (CSM) uses stabilized linear orthographic projection (Filament / MJP standard) with a 2-tile shadow atlas:
  - Cascade 0 (near, 16 blocks radius): 32 texels/block at 1024 resolution for razor-sharp contact shadows.
  - Cascade 1 (far, 64 blocks radius): 8 texels/block covering the full 64-block view distance.
- Chunks outside the outer map bounds are culled before shadow redraws, while hardware viewport clipping discards geometry outside each cascade.
- Smooth hermite cascade blending between 12.8m and 16m eliminates cascade transition seams.
- Dynamic slope-scaled depth bias eliminates shadow acne on slanted surfaces without peter-panning.
- Soft shadow filtering uses 4-tap PCF with cascade clamping.
- The shadow pass of vertex shaders returns early (no fog / lightmap / receiver work).
- `SHADOW_TRANSPARENT_RECEIVE 0` stops glass and water from receiving shadows (they still cast).
- Needs the rebuilt CustomBuffers mod for culling; without it the pack works as before.
