# shadow-simple

```
RenderChunk/             terrain: casts + receives (opaque, alpha-tested and transparent)
Actor/                   entities: casts + receives (opaque)
utils/shadow.h           the only shadow code: settings, sun, map space, distortion, soft-edge lookup
```

The folder that contains `utils/` (this folder, `example/shadow/`) must be on the shader include path,
so `#include <utils/shadow.h>` resolves. Settings (`SHADOW_RADIUS`, `SHADOW_BRIGHTNESS`, bias, taps, `SHADOW_COLORED`, ...)
are at the top of `utils/shadow.h`; the map resolution is the default in `uniforms/ShadowRes.json`, distortion is `SHADOW_DISTORT`.

## Coloured shadows and depth format (needs the updated CustomBuffers mod)

Same method as Iris: `s_ShadowMap` = opaque depth (shadowtex1), `s_ShadowAll` = depth of everything (shadowtex0),
`s_ShadowColor` = colour + alpha of the nearest caster (shadowcolor0). Stained glass, water and ice cast tinted light.
Transparent terrain passes use the uniform `ShadowTransparentPass`; the buffers are `buffers/ShadowAll.json` (slot 6) and
`buffers/ShadowColor.json` (slot 7). Set `SHADOW_COLORED 0` in `utils/shadow.h` to turn it off.

`uniforms/ShadowRes.json` is a `vec4`: `x` = map resolution, `y` = depth bits of the maps (16 default, 24, or 32 = 32 bit float).


## Performance notes

- `uniforms/ShadowCull.json` holds the map radius and depth range (`SHADOW_RADIUS`, `SHADOW_DEPTH_RANGE` read from it) and
  turns on chunk culling in the mod (`z = 1`, terrain only). Keep `x` and `y` identical in `RenderChunk` and `Actor`.
- The shadow pass of vertex shaders returns early (no fog / lightmap / receiver work).
- `SHADOW_TRANSPARENT_RECEIVE 0` stops glass and water from receiving shadows (they still cast).
- Needs the rebuilt CustomBuffers mod for culling; without it the pack works as before.

> **Known issue** The sun direction and the shadow movement while the sun is moving are still a bit buggy: shadows can flicker, jump or shift slightly as the time of day changes. Hard sun steps (`SHADOW_SUN_STEP`, `SHADOW_SUN_GLIDE = 0`) reduce it but do not remove it. `SHADOW_DEBUG_FREEZE_SUN` freezes the sun for testing.
