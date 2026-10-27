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
6. [Shadow distortion](#shadow-distortion)
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
| **The mod** (`src/GlHooks.cpp`) | Owns two depth textures (16 bit by default). After every real draw of a *caster* it draws the same geometry again into the shadow texture. It hands the finished map back to materials as `s_ShadowMap`. |
| **The material** (`vertex.sc`, `fragment.sc`, `utils/shadow.h`) | Decides *what* casts, and *where* the sun is. In the caster draw it outputs vertices in the sun's view; in a normal draw it reads the map. |

The mod never looks at your shader maths. The contract between them is a handful of names (the first three below are the basics; the rest are optional extras covered later):

| Name | Kind | Meaning |
| --- | --- | --- |
| `ShadowPass` | `vec4` uniform | Declared by every program that should cast. The mod sets `x = 1` while it redraws that program into the shadow map and `0` otherwise. |
| `s_ShadowMap` | sampler | Declared by every program that should receive. The mod binds the finished map to it. |
| `ShadowRes` | `vec4` uniform | `x` = map resolution (the mod reads it and resizes the map), `y` = depth bits of the maps: `0` (default) or 16 = 16 bit, 24 = 24 bit, 32 = 32 bit float. Distortion is a constant in the shaders (`SHADOW_DISTORT`). |
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
  utils/shadow.h              all shadow code and settings: sun, map space, distortion, bias, soft-edge lookup
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
  // caster pass: position the vertex in the sun's view instead of the camera's
  gl_Position = shadowCaster(worldPos, sunDirection());   // shadowClip + shadowDistort
} else {
  // receiver: compute where this vertex lands in the map
}
```

Opaque and alpha-tested geometry cast into the opaque map. Transparent geometry (water, stained glass) casts into a second map, see [Transparent shadows](#transparent-shadows-coloured-shadows-from-glass-and-water).

What the mod does for each caster draw, in order:

1. Binds the shadow framebuffer, sets the viewport to the map size, turns off scissor, enables depth test and
   polygon offset.
2. On the first caster draw of a frame, clears the depth to 1.0.
3. Sets `ShadowPass.x = 1` and re-issues the same draw (same buffers, same uniforms).
4. Sets `ShadowPass.x = 0` and restores every piece of GL state it touched.

### Skipping the shading work

Only depth matters in the second draw, so the fragment shader leaves immediately. Alpha-tested geometry still has to
run its cut-out test, otherwise leaves would cast solid squares:

```glsl
#ifdef SHADOW
  if (ShadowPass.x > 0.5) {
    gl_FragColor = vec4(1.0, 1.0, 1.0, 0.0);   // colour is ignored here
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
  float t = fract(GameTimeOfDay.x) * steps;
  float f = floor(t + 0.001);                     // hold the sun on a step
  float a = 2.0 * pi * (f / steps);
  vec3 s = vec3(-sin(a), cos(a), 0.0);
  return (s.y > 0.0) ? s : -s;                    // moon takes over below the horizon
}
```

`+y` is up and `+x` is east. At noon the vector is `(0, 1, 0)`. Below the horizon it is flipped, so the light always
comes from above and the moon casts shadows too. Everything else in this guide calls the result `L`: **the direction
from the surface towards the light.**

### Stepping the sun

> **Known issue** Sun direction and shadow movement while the sun is moving are still a bit buggy: shadows can flicker, jump or shift slightly as the time of day changes.

The map is re-rendered every frame. If the sun moves a tiny amount each frame, every shadow edge falls on slightly
different texels each frame and the edges crawl. So the sun is held on a step and only jumps to the next one:

| Setting | Default | Effect |
| --- | --- | --- |
| `SHADOW_SUN_STEP` | 0.5 | Degrees per sun update (0.5 = one step about every 1.7 s of game time). |
| `SHADOW_SUN_GLIDE` | 0.0 | Share of each step spent easing to the next. `0` = hard steps. Above 0 the sun moves during the glide and the edges crawl again. |
| `SHADOW_DEBUG_FREEZE_SUN` | -1 | Diagnostic: `0..1` freezes the sun at that time of day, `-1` = off. |

Because receivers read the map from the previous frame, each step is a one-frame error that grows with the step size and
the distance from the camera. Smaller steps make a smaller pop; larger steps make it rarer. The `+ 0.001` stops float
noise in the time uniform from toggling a step on an exact boundary.

---

## Shadow space

The shadow map is an **orthographic** picture of a square around the camera, looking along `-L`. Three things define
it:

- a basis: `right`, `up` and the light direction `L`
- a radius: `SHADOW_RADIUS` blocks from the camera to the edge of the map (`ShadowCull.x`, default 64)
- a depth range: `SHADOW_DEPTH_RANGE` blocks along `L` (`ShadowCull.y`, default 400, covers ±400)

```glsl
vec4 shadowClip(vec3 q, vec3 camAbs, vec3 L) {
  vec3 right = vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(L.y, -L.x, 0.0);
  float texel = 2.0 * SHADOW_RADIUS / SHADOW_RES;
  vec2 snap = fract(vec2(dot(camAbs, right), dot(camAbs, up)) / texel) * texel;
  vec2 xy = vec2(dot(q, right), dot(q, up)) + snap;
  return vec4(xy / SHADOW_RADIUS, -dot(q, L) / SHADOW_DEPTH_RANGE, 1.0);   // z: -1 = nearest the sun
}
```

`q` is the position **relative to the camera** (the vertex shader's `worldPos` already is). Because `L.z` is always 0,
`(right, up, L)` is a right-handed orthonormal basis, so projecting is just three dot products.

### Texel snapping

If the map's centre followed the camera exactly, every shadow edge would slide across the texel grid as you walk and
shimmer. The code instead shifts the map by `snap`, the camera's remainder within a texel, so the map's centre always
sits on a whole texel in world space. Moving less than one texel changes nothing; crossing a texel boundary moves the
whole map by exactly one texel.

### Receivers and last frame's camera

A receiver reads the map from the previous frame, which was centred on the camera of that frame. So it projects with
`LastCameraPosition` and shifts the position by how far the camera has moved since:

```glsl
vec3 qL = worldPos + (CameraPosition.xyz - LastCameraPosition.xyz);
vec4 sc = shadowClip(qL, LastCameraPosition.xyz, L);
```

The result goes through a varying as `v_shadow = vec4(sc.xyz * 0.5 + 0.5, strength)`: map `uv` in `xy`, depth in `z`, and
a strength in `w` that fades shadows out when the sun is near the horizon (this also hides the day/night flip of `L`):

```glsl
float strength = smoothstep(SHADOW_SUN_FADE.x, SHADOW_SUN_FADE.y, L.y);   // 0.10 .. 0.35
```

---

## Shadow distortion

With a plain orthographic map, a texel covers the same area everywhere: 128 blocks across 1024 texels is about 0.125
blocks per texel, far too coarse right next to the player and wasted on the far edge. Distortion stretches the map
around the camera so the centre gets many more texels.

```glsl
vec2 shadowDistort(vec2 p) {
  return p * ((1.0 + SHADOW_DISTORT) / (length(p) + SHADOW_DISTORT));
}
```

`p` is in map coordinates (-1 to 1) and `k` is the constant `SHADOW_DISTORT`. The factor divides by the distance from the centre,
so near the centre positions are pushed outward (more texels per block) and near the edge they barely move. The
`(1 + k)` in the numerator keeps the map edge at exactly 1, so the whole map is used and geometry beyond the radius
stays outside it instead of being squeezed in.

| `k` | Texel size at the centre | Texel size at the edge |
| --- | --- | --- |
| off (`0`) | 1× | 1× |
| 0.25 | about 0.2× | about 5× |
| 0.10 (default) | about 0.09× | about 11× |
| 0.05 | about 0.05× | about 21× |

*(relative to an undistorted map; approximate.)*

> **Warning** Distortion is applied in two places that must agree: the caster applies it to the vertex position, and the
> receiver applies it to the `uv` it samples. The vertex shader deliberately sends the **undistorted** coordinates
> through the varying and the fragment shader distorts them. Interpolating distorted values across a triangle would be
> wrong, because the distortion is not linear.

> **Note** Distortion bends straight lines, and a GPU draws straight triangles. On very large triangles close to the
> centre this can leave blobs. Terrain is made of 1-block quads, so it is rarely visible.

---

## Reading the shadow map

Declare the sampler and the settings in the receiver's fragment shader:

```glsl
SAMPLER2D_AUTOREG(s_ShadowMap);
uniform vec4 ShadowRes;
```

and describe the texture in `buffers/ShadowMap.json` (slot 5, **point** filtering, **clamp** wrapping, default
texture `white`). Depth is stored in the red channel as `0..1`. Until the first frame has been rendered, the mod binds
a white texture, which reads as depth 1.0: everything lit.

### The comparison

The map stores the depth of the surface nearest the sun. Our depth `z` is the same quantity for the current point. If
ours is larger, something is closer to the sun and we are shadowed. `step(a, b)` returns 1 when `b >= a`:

```glsl
float lit = step(z, texture2D(s_ShadowMap, uv).r);   // 1 = lit, 0 = shadowed
```

Before sampling, the undistorted map coordinates `p = s.xy * 2 - 1` are distorted and mapped back to 0..1:

```glsl
vec2 uv = shadowDistort(p) * 0.5 + 0.5;
```

Anything outside the map (`|p| >= 1`) is treated as lit, apart from the face check below, and the shadow fades out
toward the map border:

```glsl
vec2 c = abs(p);
float edge = 1.0 - smoothstep(SHADOW_EDGE_FADE, 1.0, max(c.x, c.y));   // SHADOW_EDGE_FADE = 0.85
```

---

## Shadow bias

Taken literally, the comparison above makes surfaces shadow *themselves*: the depth stored for a texel is the depth at
the texel's centre, but the point you are shading is somewhere else on the same tilted surface, a hair deeper. Half
the surface passes the test and half fails, which draws stripes. This is **shadow acne**.

The fix is to push the point towards the sun before comparing. The example adds up three terms.

**1. A constant** (`SHADOW_BIAS`, 0.12 blocks). Covers rounding errors.

**2. A slope term.** How much a surface can disagree with the texel centre depends on how tilted it is against the
sun and on how big a texel is on that surface:

```glsl
float texelBlocks = 2.0 * SHADOW_RADIUS / ShadowRes.x * growth;
float slope = clamp(sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1), 0.0, 6.0);
float z = s.z - (SHADOW_BIAS + texelBlocks * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);
```

- `ndl` is the cosine between the face normal and `L`. `sqrt(1 - ndl²) / ndl` is the tangent of the angle: 0 for a face
  looking straight at the sun, large for a face almost parallel to it.
- `growth` is how much bigger a texel is here than in an undistorted map (`(r + k)² / (k (1 + k))`, clamped to 6). Far texels are stretched, so they need more bias.
- Dividing by `2 * SHADOW_DEPTH_RANGE` converts blocks into the 0..1 depth the map stores.

**3. A polygon offset in the mod.** While drawing into the map the mod enables `GL_POLYGON_OFFSET_FILL` with
`kShadowPolySlope = 2.0` and `kShadowPolyUnits = 4.0` (constants at the top of `src/GlHooks.cpp`). The GPU then pushes
the *stored* depth slightly away from the sun in proportion to each triangle's slope, which is the hardware version
of term 2.

| Symptom | Change |
| --- | --- |
| Stripes or speckles on lit surfaces | Raise `SHADOW_BIAS` or `SHADOW_SLOPE_BIAS`. |
| Shadows start away from the object that casts them ("peter panning") | Lower them. |

---

## Faces turned away from the sun

A face pointing away from the sun is always in shadow, and the map is the wrong tool to find that out, because it
compares the face against itself. So the example computes the face normal in the fragment shader and treats such faces
as shadowed without sampling the map at all.

Chunk vertices have no normals, so the normal comes from how the camera-relative position changes from one pixel to the
next:

```glsl
highp vec3 pdx = dFdx(v_rel) * 64.0;
highp vec3 pdy = dFdy(v_rel) * 64.0;
highp vec3 n = cross(pdx, pdy);
highp float nLen = length(n);
n = (nLen > 0.0) ? n / nLen : vec3(0.0, 1.0, 0.0);
n = (dot(n, v_rel) > 0.0) ? -n : n;   // turn it towards the camera
float ndl = dot(n, normalize(v_sun));
```

- `v_rel` is the camera-relative position, passed from the vertex shader. The cross product of its two screen-space
  derivatives is perpendicular to the surface.
- The `* 64.0` and `highp` keep the tiny per-pixel steps from underflowing on devices with low-precision floats.
- A visible surface always faces the camera, so the sign is chosen to make `dot(n, v_rel) < 0`.

The face factor then fades smoothly from lit to shadowed as the face turns away:

```glsl
float face = mix(1.0, smoothstep(0.0, SHADOW_FACE_FADE, ndl), s.w);
if (face <= 0.0) { return 0.0; }             // no map lookups at all
...
return min(mix(1.0, lit, edge * s.w), face); // also applies outside the map
```

Because `face` is also applied outside the map, surfaces turned away from the sun stay dark at any distance; only the
cast shadows of lit surfaces stop at the border.

---

## Soft edges

A single shadow-map sample gives hard, jagged edges because the map has limited resolution. Real shadows also have a
soft edge (the penumbra). Averaging several samples around the point (percentage-closer filtering) fixes both. This is
what gives the soft grass shadows in the Iris tutorial's last screenshot.

The example takes `SHADOW_TAPS` samples (4 by default) on a spiral (Vogel) disk and rotates the whole disk by a different angle
for every pixel, so the pattern never lines up into visible bands. To keep it cheap, most pixels use far fewer:

1. **Stage 1** takes every second sample (`SHADOW_TAPS / 2`). If they all agree, the pixel is fully lit or
   fully shadowed and it stops. This is what most pixels do, near and far.
2. Only if they disagree (the pixel is on a shadow edge) **stage 2** takes the remaining samples.

The blur is never narrower than about 1.2 texels, so far-away edges, where texels are large, are filtered as well instead
of staying hard and blocky.

```glsl
// simplified from shadowLit(): sum of the first half of the taps
for (int j = 0; j < SHADOW_TAPS / 2; j++) { sum += shadowTap(uv + dir * (d * R), z); /* rotate dir, next d */ }
if (sum is neither 0 nor SHADOW_TAPS / 2) {      // shadow edge: take the other half
  ...
  lit = sum / float(SHADOW_TAPS);
} else {
  lit = sum / float(SHADOW_TAPS / 2);
}
```

Each tap offsets `uv` along a rotating direction at radius `sqrt((i + 0.5) / SHADOW_TAPS)` and compares depths:

```glsl
return step(z, texture2D(s_ShadowMap, uv + dir * (d * R)).r);   // with colour: see Transparent shadows
```

The rotation angle comes from *interleaved gradient noise*, a cheap function of the pixel coordinate, so no noise
texture is needed.

### Blur size in blocks

The penumbra is defined in **blocks**, not texels, so it looks the same near the player and far away even though
distortion makes texels very different sizes:

```glsl
float blurTexels = clamp(SHADOW_BLUR * SHADOW_RES / (2.0 * SHADOW_RADIUS * stretch), 1.2, 6.0);
```

`stretch` is the (unclamped) size of a texel here compared to an undistorted map. Close to the camera a texel is tiny,
so the same blur needs many texels; far away a texel is already large, so it needs about one. The result is clamped between
1.2 and 6 texels.

The bias grows with the blur, because samples at the edge of the disk read the map from a different point on a tilted
surface (see [Shadow bias](#shadow-bias)).

| Setting | Default | Effect |
| --- | --- | --- |
| `SHADOW_TAPS` | 4 | **Maximum** samples per pixel (even number). `1` = hard edge, `8`-`16` = smooth. Typical pixels use half of it. |
| `SHADOW_BLUR` | 0.12 | Penumbra radius in blocks. Larger = softer, more "overcast". |

> **Note** With few taps the per-pixel rotation shows up as fine grain instead of banding. Raising `SHADOW_TAPS` is the
> fix; there is no frame-to-frame jitter, so it does not shimmer.

The sampler must stay on **Point** filtering; comparing filtered depths gives wrong results.

---

## Putting the shadow on the surface

The fragment shader multiplies the lit colour by a factor between `SHADOW_BRIGHTNESS` (inside shadow) and 1 (lit):

```glsl
float skyExposure = clamp(v_lightmapUV.y * 1.2, 0.0, 1.0);
diffuse.rgb *= mix(1.0, mix(SHADOW_BRIGHTNESS, 1.0, lit), skyExposure);
```

`v_lightmapUV.y` is the game's sky light level for the vertex. Scaling the shadow by it keeps caves and covered areas
from being darkened a second time: they get no extra shadow where the sun cannot reach anyway. `SHADOW_BRIGHTNESS`
defaults to 0.55.

---

## Entities

Entities live in a different material (`Actor`) and are drawn after the terrain. They cast, and receive like the terrain. Include the same
`utils/shadow.h` and add one branch for the caster pass:

```glsl
if (ShadowPass.x > 0.5) {
  if (ui) {
    position = vec4(0.0, 0.0, 2.0, 1.0);   // inventory previews never enter the world map
  } else {
    position = shadowCaster(worldPosition - ViewPositionAndTime.xyz, sunDirection());
  }
}
```

Points to know:

- `ui` (`u_proj[3][3] > 0.5`) identifies inventory and paper-doll draws. Without this check they would be drawn into
  the world shadow map in screen-space coordinates.
- The map is written this frame but read next frame, so a moving entity's shadow trails it by one frame.
- The fragment shader uses the same early-out as terrain, keeping the alpha test for cut-out textures.
- The terrain already reads the shared map, so entity shadows land on the ground without any change to `RenderChunk`.
- Entities also receive shadows (`shadowLit` in `Actor/fragment.sc`), from the same map.

---

## Settings reference

### `ShadowRes` (uniform default in `uniforms/ShadowRes.json`)

| Component | Default | Meaning |
| --- | --- | --- |
| `x` | 512 | Shadow map resolution per side. The mod reads it once per frame, clamps it to 256-4096 and recreates the map when it changes. `0`/unset (no `ShadowRes` uniform, or `x` below 1) = 2048. |
| `y` | 16 | Depth bits of the maps: 16 (default), 24, or 32 (float). `0`/unset = 16. |

Distortion strength is the constant `SHADOW_DISTORT` (default 0.10, smaller = crisper near you, blurrier far away, `0` = off).

### Constants in the shaders (`utils/shadow.h`)

| Constant | Default | Meaning |
| --- | --- | --- |
| `SHADOW_RADIUS` | `ShadowCull.x` (64) | Blocks from the camera to the map edge. Set in `uniforms/ShadowCull.json`; the mod culls with the same value. |
| `SHADOW_DEPTH_RANGE` | `ShadowCull.y` (400) | Blocks along `L`. |
| `SHADOW_RES` | `ShadowRes.x` | Map resolution (uniform). |
| `SHADOW_DISTORT` | 0.10 | Distortion strength, `0` = off. |
| `SHADOW_BRIGHTNESS` | 0.55 | Brightness inside shadow (1 = no shadows). |
| `SHADOW_BIAS` | 0.12 | Constant bias in blocks. |
| `SHADOW_SLOPE_BIAS` | 0.6 | Slope-scaled bias strength. |
| `SHADOW_TAPS` | 4 | Maximum samples per pixel. |
| `SHADOW_BLUR` | 0.12 | Penumbra radius in blocks. |
| `SHADOW_FACE_FADE` | 0.15 | How quickly a face darkens as it turns from the sun. |
| `SHADOW_EDGE_FADE` | 0.85 | Where, as a fraction of the radius, the shadow fades out. |
| `SHADOW_GLASS_TINT` / `SHADOW_GLASS_DARK` | 0.85 / 0.30 | Strength of the colour and the constant darkening of light through glass or water. |
| `SHADOW_TRANSPARENT_RECEIVE` | 0 | `1` = glass and water also receive shadows (more lookups). |
| `SHADOW_COLORED` | 1 | `0` turns coloured shadows off (saves two samplers). |
| `SHADOW_SUN_STEP`, `SHADOW_SUN_GLIDE` | 0.5, 0 | Sun stepping (see [Stepping the sun](#stepping-the-sun)). |
| `SHADOW_SUN_FADE` | `vec2(0.10, 0.35)` | Sun height where shadows fade in. |

### Constants in the mod (`src/GlHooks.cpp`, needs a rebuild)

| Constant | Default | Meaning |
| --- | --- | --- |
| `kShadowSize` | 2048 | Fallback resolution when the material has no `ShadowRes`. |
| `kShadowSizeMin` / `kShadowSizeMax` | 256 / 4096 | Clamp for `ShadowRes.x`. |
| `kShadowFormat` | `GL_DEPTH_COMPONENT16` | Depth texture format. |
| `kShadowPolySlope` / `kShadowPolyUnits` | 2.0 / 4.0 | Polygon offset while rendering the map. |

---

## Performance

- Every caster draw is issued twice. This is the main cost; the second draw is depth-only and the fragment shader
  returns immediately, so it is mostly vertex work.
- Memory is two depth textures: `2 × size² × 2` bytes at 16 bit (the default), about 4 MB at 1024 and 16 MB at 2048 (double at 24 bit and at 32 bit float, which is the same size as 24 bit).
- Lower `ShadowRes.x` before anything else; distortion keeps the area near the player sharp even at 1024.
- Faces turned away from the sun skip the map lookups completely. Each tap is one texture read. Lit or fully shadowed pixels use `SHADOW_TAPS / 2`, and only shadow edges use all of them.
- Only chunks the game already draws this frame cast, so shadows from off-screen geometry are missing.

---

## Troubleshooting

| Problem | Likely cause and fix |
| --- | --- |
| No shadows at all | The material has no `ShadowPass` in the opaque pass (nothing casts), or `s_ShadowMap` is not declared in the receiver. |
| Shadows only appear after a moment | The map is white until the first frame is ready. Expected. |
| Stripes on surfaces | Shadow acne. Raise `SHADOW_BIAS` or `SHADOW_SLOPE_BIAS`. |
| Stripes on sides facing away from the sun | The face normal (from screen-space derivatives) is not working on that device. |
| Shadows float away from objects | Bias is too high. Lower `SHADOW_BIAS` or `SHADOW_SLOPE_BIAS`. |
| Terrain and entity shadows disagree | `ShadowCull.json` (radius, depth range) or `ShadowRes.json` differ between the two materials. |
| Shadows shimmer when walking | Snapping broke: check that `ShadowRes.x` is the same everywhere and that `LastCameraPosition` is mapped in `material.json`. |
| Shadow edges crawl while the sun moves | `SHADOW_SUN_GLIDE` is above 0. Set it to 0 for hard steps. |
| A moving entity's shadow trails slightly | The map is read one frame late; expected. |
| Blobs or wobbling shapes near the player | Distortion is too strong (`SHADOW_DISTORT` too small) for the geometry. Raise `SHADOW_DISTORT`. |
| Shadows grainy | Too few taps for the blur size. Raise `SHADOW_TAPS` or lower `SHADOW_BLUR`. |
| Shadows too blurry far away | Distortion trades far for near. Raise `SHADOW_DISTORT` or the resolution. |
| Inventory preview shadows appear in the world | The `ui` check is missing from the entity caster branch. |


## Transparent shadows (coloured shadows from glass and water)

The same split Iris/OptiFine use (`shadowtex0` / `shadowtex1` / `shadowcolor0`). Three samplers, all optional:

| Sampler | Contents | Iris name |
| --- | --- | --- |
| `s_ShadowMap` | depth of **opaque** casters only | `shadowtex1` |
| `s_ShadowAll` | depth of **all** casters, opaque + transparent | `shadowtex0` |
| `s_ShadowColor` | RGBA written by the nearest of those casters (`RGBA8`) | `shadowcolor0` |

A program that declares the uniform `ShadowTransparentPass` (`vec4`, see `ShadowTransparentPass.json`) is a **transparent caster**.
The mod redraws it with `ShadowTransparentPass.x = 1` into the all-casters map only. Its fragment shader should write
`texture * vertexColor` (alpha = how much light it blocks) and discard nearly invisible texels, like `shadow.fsh` in Iris.
Opaque casters (`ShadowPass`) keep working as before; their colour output is ignored, write `vec4(1, 1, 1, 0)`.

How it works:

- The first time a transparent caster is drawn in a frame, the mod copies the opaque depth into the all-casters depth
  (one `glBlitFramebuffer`) and clears its colour to white with alpha 0. Opaque casters are therefore not drawn a third time.
- An opaque caster that is drawn after that point is added to both maps, so the all-casters depth stays complete.
- Transparent casters use depth test `LESS` with depth writes, so each texel keeps the nearest caster's colour. Blending and the
  colour mask are switched off while drawing and restored afterwards.
- Receivers read the previous frame's maps like before. In a frame without transparent casters `s_ShadowAll` is bound to the
  opaque map (they are identical) and `s_ShadowColor` to white, so opaque shadows never disappear.

Receiver logic (per sample, same as the Iris guide):

```glsl
if (step(z, texture2D(s_ShadowAll, uv).r) > 0.5) return vec3(1.0);   // nothing in the way
if (step(z, texture2D(s_ShadowMap, uv).r) < 0.5) return vec3(0.0);   // opaque in the way
vec4 c = texture2D(s_ShadowColor, uv);                               // only glass / water in the way
vec3 hue = c.rgb / max(max(c.r, max(c.g, c.b)), 0.001);              // the example uses only the hue, not per-pixel alpha
return mix(vec3(1.0), hue, SHADOW_GLASS_TINT) * (1.0 - SHADOW_GLASS_DARK);
```

Memory: the all-casters map adds an `RGBA8` colour texture and a depth texture (16 bit by default, see `ShadowRes.y`) per buffer, twice
(6 bytes per texel x 2 at 16 bit, 8 at 24 bit). At 1024 and 16 bit that is 12 MB, at 2048 it is 48 MB. It is only allocated when a program declares
`ShadowTransparentPass`. If creating it fails, opaque shadows keep working and only the coloured shadows are lost.


## Cost of a caster draw

Every caster draw is issued twice (the second one into the shadow map), so the shadow pass costs roughly one more
geometry pass plus a framebuffer switch per draw. What the mod does to keep the overhead of that small:

- It only changes GL state that differs from what the game has (usually just viewport, depth function and polygon offset)
  and restores exactly that. During chunk draws this brings a caster draw from 35 to 25 GL calls (measured with the fake
  GL in `tests/fake_gl_test.cpp`, including the game's own draw and the sampler binds).
- It no longer queries the read framebuffer per draw (only the once-per-frame depth copy touches it) and no longer calls
  `glGetError` after every draw (only for the first 240 casts after the maps are created).
- Draws with no vertices or no instances are not repeated.
- `ShadowRes.y = 16` (the default) halves the memory and bandwidth of every shadow texture compared to 32 bit (at 2048: 8 MB instead of 16 MB each).
  Depth steps become 1/65536 of `SHADOW_DEPTH_RANGE * 2` (about 0.012 blocks), so keep `SHADOW_BIAS` at 0.12 or higher.
- The all-casters map is only created when a transparent caster exists, and the opaque depth is copied into it once per
  frame instead of drawing the opaque casters a third time.

What it cannot remove: the second geometry pass and the framebuffer switch. Cutting those needs the map to be rebuilt only
every few frames (the receiver would then need the camera and sun direction of the frame that built the map).


## Chunk culling (ShadowCull)

Like OptiFine's shadow frustum, the mod skips the second (shadow) draw of a terrain chunk that lies completely outside the
shadow map. The hardware would clip all of its triangles anyway, but only after the vertex shader ran for every vertex.

Opt-in per material with the uniform `ShadowCull` (`vec4`, see `uniforms/ShadowCull.json`):

| Component | Meaning |
| --- | --- |
| `x` | map radius in blocks. Use it in the shader as `SHADOW_RADIUS`, so mod and shader always agree |
| `y` | depth range in blocks (`SHADOW_DEPTH_RANGE`) |
| `z` | `1` = cull this program's draws (terrain), `0` = off (entities: the mod does not know their size) |

How it decides: it reads the chunk's `u_model` translation (relative to the camera, like the caster maths), builds the sun
direction from `GameTimeOfDay`, and tests the chunk's box against the map square in light space with a margin of
`16 + 0.1 * radius` blocks (texel snapping, sun stepping up to about 5 degrees, chunk size). Instanced draws, programs
without `u_model`, and matrices that are not a plain translation are never culled. Cost: one `glGetUniformfv` per chunk
draw, plus two per program per frame.
