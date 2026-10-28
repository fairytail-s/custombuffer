# Shadows

This guide explains how shadows work in Custom Buffers, step by step, using the example pack in
`example/shadow/` (`RenderChunk/` for terrain, `Actor/` for entities, `utils/shadow.h` for the shared code). It follows the same path as a classic shadow-mapping
tutorial: render the world from the sun, read that picture back, compare depths, then fix the artifacts.

> **Note** Code excerpts below are taken from the example pack (`utils/shadow.h` and the two vertex/fragment shaders), shortened where noted. Because the shared code lives in one header, terrain and entities always use the same constants.

## Contents

1. [Overview](#overview)
2. [Files](#files)
3. [The caster pass](#the-caster-pass)
4. [The sun direction](#the-sun-direction)
5. [Shadow space](#shadow-space)
6. [Cascaded shadow mapping](#cascaded-shadow-mapping)
7. [Reading the shadow map](#reading-the-shadow-map)
8. [Shadow bias](#shadow-bias)
9. [Faces turned away from the sun](#faces-turned-away-from-the-sun)
10. [Soft edges](#soft-edges)
11. [Putting the shadow on the surface](#putting-the-shadow-on-the-surface)
12. [Entities](#entities)
13. [Settings reference](#settings-reference)
14. [Performance](#performance)
15. [Troubleshooting](#troubleshooting)

---

## Overview

A shadow map is a picture of the world seen from the sun, where every pixel stores how far the nearest surface is from
the sun. To shade a point on screen you ask: "is something closer to the sun than me, in my direction?" If yes, the
point is in shadow.

Bedrock materials cannot create render targets or run extra passes by themselves, so the work is split:

| Part | Does what |
| --- | --- |
| **The mod** (`src/GlHooks.cpp`) | Owns two depth textures (16 bit by default). After every real draw of a *caster* it draws the same geometry again into the shadow texture atlas. It hands the finished map back to materials as `s_ShadowMap`. |
| **The material** (`vertex.sc`, `fragment.sc`, `utils/shadow.h`) | Decides *what* casts, and *where* the sun is. In the caster draw it outputs vertices in the sun's view; in a normal draw it reads the map. |

The mod never looks at your shader maths. The contract between them is a handful of names:

| Name | Kind | Meaning |
| --- | --- | --- |
| `ShadowPass` | `vec4` uniform | Declared by every program that should cast. The mod sets `x = 1` while it redraws that program into the shadow map and `0` otherwise; `y` indicates the cascade index (0 for near, 1 for far). |
| `s_ShadowMap` | sampler | Declared by every program that should receive. The mod binds the finished map atlas to it. |
| `ShadowRes` | `vec4` uniform | `x` = map resolution per cascade (the mod reads it and resizes the map, default 1024), `y` = depth bits: 16 (default), 24, 32 (float). |
| `ShadowTransparentPass`, `s_ShadowAll`, `s_ShadowColor` | uniform / samplers | Optional: coloured shadows from glass and water, see [Transparent shadows](#transparent-shadows-coloured-shadows-from-glass-and-water). |
| `ShadowCull` | `vec4` uniform | Optional: lets the mod skip terrain draws outside the map, see [Chunk culling](#chunk-culling-shadowcull). |

### One frame of delay

The map is built *during* a frame, after the geometry that uses it has already been drawn. The mod therefore keeps two
textures: it writes to one while receivers read the one finished in the **previous** frame, and swaps them at
`eglSwapBuffers`. Everything below that talks about "last frame" comes from this.

---

## Files

```
example/shadow/
  utils/shadow.h              all shadow code and settings: sun, 2-cascade map space, bias, soft-edge lookup
  RenderChunk/                terrain: casts and receives (opaque, alpha-tested and transparent)
    config.json               from the base material
    material.json             uniform_overrides (CameraPosition, LastCameraPosition, GameTimeOfDay)
    vertex.sc                 caster / receiver branch
    fragment.sc               caster output, shading with the shadow
    varying.def.sc            v_shadow, v_rel, v_sun
    buffers/                  ShadowMap, ShadowAll, ShadowColor textures (slots 5, 6, 7, point filtered)
    uniforms/                 ShadowPass, ShadowTransparentPass, ShadowRes, ShadowCull, GameTimeOfDay, CameraPosition, LastCameraPosition
  Actor/                      entities: casts and receives (same layout, opaque only)
```

`material.json` maps three uniforms to values the game already computes:

```json
{
  "uniform_overrides": {
    "CameraPosition": "BUILTIN_MinecraftCameraPosition",
    "LastCameraPosition": "BUILTIN_MinecraftLastCameraPosition",
    "GameTimeOfDay": "BUILTIN_MinecraftTimeOfDay"
  }
}
```

---

## The caster pass

Any program whose compiled shader still contains the uniform `ShadowPass` is a caster. The shader compiler removes
unused uniforms, so to limit casting to the passes you want, only *use* `ShadowPass` there:

```glsl
#if (defined(OPAQUE) || defined(ALPHA_TEST)) && !defined(DEPTH_ONLY) && !defined(DEPTH_ONLY_OPAQUE)
  #define SHADOW 1
#endif
```

Inside `#ifdef SHADOW` the vertex shader branches on the flag:

```glsl
if (ShadowPass.x > 0.5) {
  // caster pass: position the vertex in the sun's view for the active cascade
  gl_Position = shadowCaster(worldPos, sunDirection(), ShadowPass.x > 0.5 ? ShadowPass.y : ShadowTransparentPass.y);
  return;
}
```

Opaque and alpha-tested geometry cast into the opaque map. Transparent geometry (water, stained glass) casts into a second map, see [Transparent shadows](#transparent-shadows-coloured-shadows-from-glass-and-water).

What the mod does for each caster draw, in order:

1. Binds the shadow framebuffer, turns off scissor, enables depth test and polygon offset.
2. On the first caster draw of a frame, clears the depth atlas to 1.0.
3. Sets viewport to `(0, 0, size, size)` and `ShadowPass.y = 0` to render Cascade 0 (near cascade).
4. Sets viewport to `(size, 0, size, size)` and `ShadowPass.y = 1` to render Cascade 1 (far cascade).
5. Restores original viewport and GL state.

### Skipping the shading work

Only depth matters in caster draws, so the fragment shader leaves immediately:

```glsl
#ifdef SHADOW
  if (ShadowPass.x > 0.5) {
    gl_FragColor = vec4(1.0, 1.0, 1.0, 0.0);
    return;
  }
#endif
```

This is placed after the `discard` for alpha testing, so cut-out pixels still do not write depth.

---

## The sun direction

`GameTimeOfDay.x` is the game's time of day as a fraction of a day. The sun direction is the point on a circle in the
x-y plane (east-west arc, no north-south tilt):

```glsl
vec3 sunDirection() {
  float steps = 360.0 / SHADOW_SUN_STEP;
  float t = ((SHADOW_DEBUG_FREEZE_SUN >= 0.0) ? SHADOW_DEBUG_FREEZE_SUN : fract(GameTimeOfDay.x)) * steps;
  float f = floor(t + 0.001);
  if (SHADOW_SUN_GLIDE > 0.0) { f += smoothstep(1.0 - SHADOW_SUN_GLIDE, 1.0, fract(t)); }
  float a = 2.0 * pi * (f / steps);
  vec3 s = vec3(-sin(a), cos(a), 0.0);
  return (s.y > 0.0) ? s : -s;                    // moon takes over below the horizon
}
```

`+y` is up and `+x` is east. At noon the vector is `(0, 1, 0)`. Below the horizon it is flipped, so the light always
comes from above and the moon casts shadows too. Everything else in this guide calls the result `L`: **the direction
from the surface towards the light.**

---

## Shadow space

The shadow map is an **orthographic** picture of a square around the camera, looking along `-L`. Three things define
it:

- a basis: `right`, `up` and the light direction `L`
- a radius: `radius` blocks from the camera to the edge of the cascade
- a depth range: `SHADOW_DEPTH_RANGE` blocks along `L` (`ShadowCull.y`, default 400, covers ±400)

```glsl
vec4 shadowClip(vec3 q, vec3 camAbs, vec3 L, float radius) {
  vec3 right = vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(L.y, -L.x, 0.0);
  float texel = 2.0 * radius / SHADOW_RES;
  vec2 snap = fract(vec2(dot(camAbs, right), dot(camAbs, up)) / texel) * texel;
  vec2 xy = vec2(dot(q, right), dot(q, up)) + snap;
  return vec4(xy / radius, -dot(q, L) / SHADOW_DEPTH_RANGE, 1.0);
}
```

`q` is the position **relative to the camera** (the vertex shader's `worldPos` already is). Because `L.z` is always 0,
`(right, up, L)` is a right-handed orthonormal basis, so projecting is just three dot products.

### Texel snapping

If the map's centre followed the camera continuously, every shadow edge would slide across the texel grid as you walk and
shimmer. The code shifts the map by `snap`, the camera's remainder within a texel, so the map's centre always
sits on a whole texel in world space. Moving less than one texel changes nothing; crossing a texel boundary moves the
whole map by exactly one texel.

Both cascades share the outer cascade's texel snap size (`2.0 * SHADOW_RADIUS / SHADOW_RES`). Because `SHADOW_RADIUS` (64 blocks) is an exact integer multiple (4×) of `SHADOW_RADIUS_NEAR` (16 blocks), snapping to the outer grid also snaps to an exact integer multiple of Cascade 0 texels (every 4 texels), eliminating shimmer in both cascades simultaneously. Cascade 0 receiver coordinates derive directly by scaling without requiring fragment-stage camera uniforms:

```glsl
highp vec2 p0 = p1 * (SHADOW_RADIUS / SHADOW_RADIUS_NEAR);
```

### Receivers and last frame's camera

A receiver reads the map from the previous frame, which was centred on the camera of that frame. So it projects with
`LastCameraPosition` and shifts the position by how far the camera has moved since:

```glsl
vec4 shadowReceiver(vec3 rel, vec3 L) {
  vec3 q = rel + (CameraPosition.xyz - LastCameraPosition.xyz);
  vec4 sc = shadowClip(q, LastCameraPosition.xyz, L, SHADOW_RADIUS);
  return vec4(sc.xyz * 0.5 + 0.5, smoothstep(SHADOW_SUN_FADE.x, SHADOW_SUN_FADE.y, L.y));
}
```

---

## Cascaded shadow mapping

Following the industry standard used in Google Filament and MJP's Shadows sample, Custom Buffers implements a **2-Cascade Shadow Map (CSM)** with a stabilized linear orthographic atlas:

- **Cascade 0 (Near)**: Covers radius $R_{	ext{near}} = 16	ext{ blocks}$ ($32	ext{ m}$ diameter). At 1024 resolution, this yields **32 texels per block** ($3.1	ext{ cm/texel}$), delivering razor-sharp contact shadows for the player, mobs, and immediate terrain.
- **Cascade 1 (Far)**: Covers radius $R_{	ext{far}} = 64	ext{ blocks}$ ($128	ext{ m}$ diameter). At 1024 resolution, this yields **8 texels per block**, providing broad shadow coverage across the full view distance.
- **Shadow Atlas Layout**: The depth texture has dimensions $2048 	imes 1024$ (width $2 	imes S$, height $S$). Cascade 0 occupies the left tile ($X \in [0.0, 0.5]$), and Cascade 1 occupies the right tile ($X \in [0.5, 1.0]$).
- **Seamless Blending**: As distance $d_0$ approaches the edge of Cascade 0 ($[0.80, 1.0]$ in normalized cascade space, roughly 12.8m to 16m), the receiver smoothly blends between Cascade 0 and Cascade 1 using `smoothstep(0.80, 1.0, d0)`, completely eliminating cascade boundary seams.

---

## Reading the shadow map

Declare the sampler and the settings in the receiver's fragment shader:

```glsl
SAMPLER2D_AUTOREG(s_ShadowMap);
uniform vec4 ShadowRes;
```

Depth is stored in the red channel as `0..1`. Until the first frame has been rendered, the mod binds
a white texture, which reads as depth 1.0: everything lit.

---

## Shadow bias

Shadow mapping tests whether a surface point is behind the depth stored in the map. Because a surface rasterized in
the shadow map has finite resolution and precision, surface fragments can mistakenly self-shadow, creating striped
artifacts known as "shadow acne".

Custom Buffers uses **dynamic slope-scaled depth bias** proportional to each cascade's texel footprint with a stable constant floor:

`glsl
highp float slope = clamp(sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1), 0.0, 5.0);
highp float z0 = s.z - (SHADOW_BIAS + texel0 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);
highp float z1 = s.z - (SHADOW_BIAS + texel1 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);
`

SHADOW_BIAS (0.12 blocks) provides a solid constant floor that safely covers the ~10 quantization levels of a 16-bit depth buffer over the 800-block depth range, while SHADOW_SLOPE_BIAS (0.6) scales with each cascade's texel size to prevent acne on slanted geometry.

---

## Faces turned away from the sun

A face pointing away from the sun is always in shadow, and the map is the wrong tool to find that out, because it
compares the face against itself. So the example computes the face normal in the fragment shader and treats such faces
as shadowed without sampling the map at all.

Chunk vertices have no normals, so the normal comes from screen-space derivatives of the camera-relative position:

```glsl
highp vec3 faceNormal(highp vec3 rel) {
  highp vec3 n = cross(dFdx(rel) * 64.0, dFdy(rel) * 64.0);
  highp float len = length(n);
  n = (len > 0.0) ? n / len : vec3(0.0, 1.0, 0.0);
  return (dot(n, rel) > 0.0) ? -n : n;
}
```

- In `RenderChunk/vertex.sc`, `v_rel = worldPos;` supplies the camera-relative vector to the fragment (`u_model` is already camera-relative in Minecraft Bedrock chunk rendering).
- Using camera-relative coordinates ensures `dot(n, rel) > 0.0` reliably turns back-facing normals toward the camera across the entire world, preventing normal flips across moving camera coordinates.
- The face factor fades smoothly from lit to shadowed as the face turns away:

```glsl
float face = mix(1.0, smoothstep(0.0, SHADOW_FACE_FADE, ndl), s.w);
if (face <= 0.0) { return vec3_splat(0.0); }
```

---

## Soft edges

Real shadows feature soft penumbra boundaries. Custom Buffers uses a 4-tap PCF filter with cascade boundary clamping to prevent atlas bleeding:

`glsl
highp vec3 sampleCascadePCF(highp vec2 uvBase, highp float z, highp vec2 uvMin, highp vec2 uvMax) {
  highp vec2 stepUV = vec2(0.25 / SHADOW_RES, 0.5 / SHADOW_RES);
  highp vec3 sum = shadowTap(clamp(uvBase + vec2(-stepUV.x,  stepUV.y), uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2( stepUV.x, -stepUV.y), uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2(-stepUV.x, -stepUV.y), uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2( stepUV.x,  stepUV.y), uvMin, uvMax), z);
  return sum * 0.25;
}
`

---

## Putting the shadow on the surface

The fragment shader multiplies the lit colour by a factor between `SHADOW_BRIGHTNESS` (inside shadow) and 1 (lit):

```glsl
float skyExposure = clamp(v_lightmapUV.y * 1.2, 0.0, 1.0);
if (skyExposure > 0.0 && v_shadow.w > 0.0) {
  highp vec3 sunlight = shadowLit(v_shadow, dot(faceNormal(v_rel), normalize(v_sun)));
  diffuse.rgb *= mix(vec3_splat(1.0), mix(vec3_splat(SHADOW_BRIGHTNESS), vec3_splat(1.0), sunlight), skyExposure);
}
```

`skyExposure > 0.0` ensures shadows smoothly fade out with the sky lightmap and avoids shadow work when unexposed to the sky.

---

## Entities

Entities live in a different material (`Actor`) and are drawn after the terrain. They cast and receive like the terrain. Include the same
`utils/shadow.h` with `#define SHADOW_RECEIVER 1`.

### Caster pass (`Actor/vertex.sc`)

```glsl
#ifdef SHADOW
  if (ShadowPass.x > 0.5) {
    v_texcoord0 = texcoord0;
    v_color0 = a_color0;
    gl_Position = ui ? vec4(0.0, 0.0, 2.0, 1.0)
                     : shadowCaster(worldPosition - ViewPositionAndTime.xyz, sunDirection(), ShadowPass.y);
    return;
  }
#endif
```

### Receiver pass (`Actor/vertex.sc` & `Actor/fragment.sc`)

In `Actor/vertex.sc`, the normal is transformed into world space and `shadowReceiver()` is called with `v_sun`:

```glsl
#ifdef SHADOW
  v_sun = sunDirection();
  v_normal = normalize(mul(World, vec4(a_normal.xyz, 0.0)).xyz);
  if (!ui && ShadowPass.x < 0.5) {
    v_shadow = shadowReceiver(worldPosition - ViewPositionAndTime.xyz, v_sun);
  }
#endif
```

In `Actor/fragment.sc`, the normal is passed directly into `shadowLit()`:

```glsl
#ifdef SHADOW
  highp vec3 n = normalize(v_normal);
  highp vec3 L = normalize(v_sun);
  albedo.rgb *= mix(vec3_splat(SHADOW_BRIGHTNESS), vec3_splat(1.0), shadowLit(v_shadow, dot(n, L)));
#endif
```

---

## Settings reference

### `ShadowRes` (uniform default in `uniforms/ShadowRes.json`)

| Component | Default | Meaning |
| --- | --- | --- |
| `x` | 1024 | Shadow map resolution per cascade (clamped 256–4096). |
| `y` | 16 | Depth bits of the maps: 16 (default), 24, or 32 (float). |

### `ShadowCull` (uniform default in `uniforms/ShadowCull.json`)

| Component | Default | Meaning |
| --- | --- | --- |
| `x` | 64.0 | Far map radius in blocks (`SHADOW_RADIUS`). |
| `y` | 400.0 | Depth range in blocks (`SHADOW_DEPTH_RANGE`, covers ±400 along `L`). |
| `z` | 1.0 (`RenderChunk`), 0.0 (`Actor`) | `1` enables chunk culling in the mod, `0` disables. |

### Constants in the shaders (`utils/shadow.h`)

| Constant | Default | Meaning |
| --- | --- | --- |
| `SHADOW_RADIUS_NEAR` | 16.0 | Near cascade radius in blocks (Cascade 0). |
| `SHADOW_RADIUS` | `ShadowCull.x` (64) | Far cascade radius in blocks (Cascade 1). |
| `SHADOW_DEPTH_RANGE` | `ShadowCull.y` (400) | Blocks along `L`. |
| `SHADOW_RES` | `ShadowRes.x` | Map resolution per cascade (uniform, default 1024). |
| `SHADOW_BRIGHTNESS` | 0.55 | Brightness inside shadow. |
| `SHADOW_BIAS` | 0.06 | Depth bias floor in blocks. |
| `SHADOW_SLOPE_BIAS` | 0.5 | Dynamic slope-scaled bias multiplier. |
| `SHADOW_TAPS` | 4 | Poisson disk sample count per pixel. |
| `SHADOW_PENUMBRA` | 0.15 | Penumbra base scale in blocks. |
| `SHADOW_FACE_FADE` | 0.15 | Darkening rate on back-facing slopes. |
| `SHADOW_EDGE_FADE` | 0.85 | Fraction of radius where shadow fades out. |
| `SHADOW_GLASS_TINT` / `SHADOW_GLASS_DARK` | 0.85 / 0.30 | Colour strength and constant glass dimming. |
| `SHADOW_TRANSPARENT_RECEIVE` | 0 | `1` = glass and water also receive shadows. |
| `SHADOW_COLORED` | 1 | `0` turns coloured shadows off. |
| `SHADOW_SUN_STEP`, `SHADOW_SUN_GLIDE` | 0.5, 0.0 | Degrees per sun step and transition easing. |
| `SHADOW_DEBUG_FREEZE_SUN` | -1.0 | Fixed sun angle for debugging (-1 = normal time of day). |
| `SHADOW_SUN_FADE` | `vec2(0.10, 0.35)` | Sun height where shadows fade in. |

### Constants in the mod (`src/GlHooks.cpp`)

| Constant | Default | Meaning |
| --- | --- | --- |
| `kShadowSize` | 1024 | Fallback resolution per cascade when material omits `ShadowRes`. |
| `kShadowSizeMin` / `kShadowSizeMax` | 256 / 4096 | Clamping bounds for resolution. |
| `kShadowFormat` | `GL_DEPTH_COMPONENT16` | Default depth format. |
| `kShadowPolySlope` / `kShadowPolyUnits` | 2.0 / 4.0 | Caster polygon offset. |

---

## Performance

- 2-Cascade Shadow Mapping provides 32 texels/block resolution near the camera while using a compact $2048 	imes 1024$ atlas.
- 16-bit depth textures (`GL_DEPTH_COMPONENT16`) require only 4 MB VRAM total for both cascades, minimizing memory bandwidth.
- Chunk culling tests chunk bounding boxes against the shadow map volume, skipping terrain draws outside the map before vertex execution.
- `skyExposure > 0.0` and `v_shadow.w > 0.0` skip shadow evaluation entirely when underground or outside map boundaries.

---

## Troubleshooting

| Problem | Likely cause and fix |
| --- | --- |
| No shadows at all | Check that `ShadowPass` exists in the opaque pass and `s_ShadowMap` is bound. |
| Shadows only appear after a moment | The map is double-buffered; the first frame initializes the buffers. |
| Half of the world flickers dark | Ensure `v_rel = worldPos;` in `RenderChunk/vertex.sc` is camera-relative (`u_model` is camera-relative in Bedrock) so `faceNormal` doesn't invert across moving player coordinates. |
| Stripes or self-shadowing acne | Raise `SHADOW_SLOPE_BIAS` (default 0.6) or `SHADOW_BIAS` (default 0.12). |
| Shadows float away from objects | `SHADOW_BIAS` is too high. Lower it towards 0.12. |
| Shadows shimmer when walking | Snapping mismatch: verify that `ShadowCull.x` matches between materials and `LastCameraPosition` is mapped in `material.json`. |
| Shadows grainy | Check `SHADOW_TAPS`. Default 4-tap Poisson provides clean, grain-free filtering. |

---

## Transparent shadows (coloured shadows from glass and water)

The same split Iris/OptiFine use (`shadowtex0` / `shadowtex1` / `shadowcolor0`):

| Sampler | Contents | Iris name |
| --- | --- | --- |
| `s_ShadowMap` | depth of **opaque** casters only | `shadowtex1` |
| `s_ShadowAll` | depth of **all** casters, opaque + transparent | `shadowtex0` |
| `s_ShadowColor` | RGBA written by the nearest of those casters (`RGBA8`) | `shadowcolor0` |

A program that declares `ShadowTransparentPass` is a **transparent caster**. The mod redraws it into the all-casters map.

```glsl
if (step(z, texture2D(s_ShadowAll, uv).r) > 0.5) { return vec3_splat(1.0); }
if (step(z, texture2D(s_ShadowMap, uv).r) < 0.5) { return vec3_splat(0.0); }
highp vec4 c = texture2D(s_ShadowColor, uv);
highp vec3 hue = c.rgb / max(max(c.r, max(c.g, c.b)), 0.001);
return mix(vec3_splat(1.0), hue, SHADOW_GLASS_TINT) * (1.0 - SHADOW_GLASS_DARK);
```

---

## Chunk culling (ShadowCull)

The mod skips the shadow redraw of terrain chunks whose bounding box lies completely outside the shadow volume.

Opt-in per material via the uniform `ShadowCull` (`vec4`):

| Component | Default | Meaning |
| --- | --- | --- |
| `x` | 64.0 | Map radius in blocks (`SHADOW_RADIUS`). |
| `y` | 400.0 | Depth range in blocks (`SHADOW_DEPTH_RANGE`, covers ±400 along `L`). |
| `z` | 1.0 (`RenderChunk`), 0.0 (`Actor`) | `1` = cull terrain draws outside the map volume, `0` = off. |
| `w` | 0.0 | Unused. |

The mod tests the chunk's box against the map square in light space with a margin of `16 + 0.1 * radius` blocks:
- $cx = z + 8.0$ (light-space right)
- $cy = (x + 8.0) \cdot L_y - (y + 8.0) \cdot L_x$ (light-space up)

Chunks outside the map volume limit c.limit ( + 16 + 6.4 \approx 86.4\text{ blocks}$) are skipped before issuing vertex shading. Chunks within the volume are drawn into both cascade viewports, where GPU hardware viewport clipping efficiently discards out-of-bounds triangles without caster pop-in.
