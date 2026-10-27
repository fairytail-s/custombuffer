// utils/shadow.h  - the only shadow file; included by materials/RenderChunk and materials/Actor
//   vertex shaders:   #include <utils/shadow.h>
//   fragment shaders: #define SHADOW_RECEIVER 1   (before the include, adds shadowLit())
// Transparent shadows work like Iris/OptiFine: ShadowMap = opaque depth (shadowtex1), ShadowAll = depth of everything
// (shadowtex0), ShadowColor = colour + alpha of the nearest caster (shadowcolor0).
#ifndef UTILS_SHADOW_H
#define UTILS_SHADOW_H

uniform vec4 CameraPosition;       // camera world position
uniform vec4 LastCameraPosition;   // camera world position of the previous frame
uniform vec4 GameTimeOfDay;        // x = time of day (fraction of a day)
uniform vec4 ShadowPass;           // x = 1 while the mod redraws this OPAQUE geometry into the shadow map
uniform vec4 ShadowTransparentPass;// x = 1 while the mod redraws this TRANSPARENT geometry (glass, water) into the shadow map
uniform vec4 ShadowRes;            // x = map resolution (texels per side), y = depth bits of the map (16 default, 24, 32). z, w unused. (bgfx has no vec2 uniforms)
uniform vec4 ShadowCull;           // x = map radius (blocks), y = depth range (blocks), z = 1: the mod skips chunk draws outside the map (uniforms/ShadowCull.json)

// ---- settings ----
#define SHADOW_RADIUS      ShadowCull.x   // blocks from the camera to the map edge (set in uniforms/ShadowCull.json, the mod culls with the same value)
#define SHADOW_DEPTH_RANGE ShadowCull.y   // blocks along the sun direction (+/-)
#define SHADOW_BRIGHTNESS  0.55    // light level inside shadow (1 = no shadows)
#define SHADOW_BIAS        0.12    // blocks. Raise for acne, lower for peter panning
#define SHADOW_SLOPE_BIAS  0.6     // extra bias on faces nearly parallel to the sun
#define SHADOW_TAPS        4       // samples per pixel: 1 = hard edge, 8-16 = soft edge
#define SHADOW_BLUR        0.12    // soft edge radius in blocks
#define SHADOW_FACE_FADE   0.15    // how fast a face goes dark as it turns away from the sun
#define SHADOW_EDGE_FADE   0.85    // where (fraction of the radius) the shadow fades out
#define SHADOW_GLASS_TINT  0.85    // how strongly glass/water colours the light (0 = none, 1 = full colour)
#define SHADOW_GLASS_DARK  0.30    // constant darkening of light passing through glass (same everywhere, ignores texture alpha)
#define SHADOW_TRANSPARENT_RECEIVE 0  // 1 = glass/water also receive shadows (extra lookups on every overdrawn layer), 0 = faster
#define SHADOW_COLORED     1       // 1 = transparent things (stained glass, water) tint the light, 0 = off (saves two samplers)
#define SHADOW_RES         ShadowRes.x        // map resolution (default set in uniforms/ShadowRes.json, the mod reads the same value)
#define SHADOW_DISTORT     0.10    // 0 = even texels, small = more texels near the player (0.05-0.2)
#define SHADOW_SUN_STEP    0.5     // degrees per sun update. The map is read one frame late, so each step is a one-frame error of (step x distance); smaller = smaller pop, more frequent
#define SHADOW_SUN_GLIDE   0.0     // 0 = hard steps (recommended: no moving sun between steps, so no edge crawl and no map/lookup mismatch). >0 = ease share of each step (reintroduces crawl)
#define SHADOW_DEBUG_FREEZE_SUN -1.0  // DIAGNOSTIC: 0..1 freezes the sun at that time of day (0.25 = afternoon). -1 = off. If flicker stays with the sun frozen it is NOT the sun
#define SHADOW_SUN_FADE    vec2(0.10, 0.35)  // sun height (L.y) where shadows fade in. Wide, so the day/night flip of L happens while shadows are invisible
#define pi 3.14159265359

// Which passes take part. The caster uniforms must only be USED in the pass they belong to:
// unused uniforms are removed by the compiler, and the mod only redraws programs that still contain them.
//   SHADOW              opaque / alpha-tested: casts into the opaque map (ShadowPass) and receives
//   SHADOW_TRANSPARENT  transparent: casts its colour into the all-casters map (ShadowTransparentPass) and receives
//   SHADOW_RECEIVE      either of the two
#if !defined(DEPTH_ONLY) && !defined(DEPTH_ONLY_OPAQUE)
  #if defined(OPAQUE) || defined(ALPHA_TEST)
    #define SHADOW 1
    #define SHADOW_RECEIVE 1
  #elif defined(TRANSPARENT)
    #define SHADOW_TRANSPARENT 1
    #if SHADOW_TRANSPARENT_RECEIVE
      #define SHADOW_RECEIVE 1
    #endif
  #endif
#endif

// Direction from the surface towards the light (sun by day, moon by night), held in SHADOW_SUN_STEP degree steps
// so shadow edges don't crawl across the texels.
vec3 sunDirection() {
  float steps = 360.0 / SHADOW_SUN_STEP;
  float t = ((SHADOW_DEBUG_FREEZE_SUN >= 0.0) ? SHADOW_DEBUG_FREEZE_SUN : fract(GameTimeOfDay.x)) * steps;
  // +0.001 keeps float noise in the time uniform from toggling the step on an exact boundary
  float f = floor(t + 0.001);
  if (SHADOW_SUN_GLIDE > 0.0) { f += smoothstep(1.0 - SHADOW_SUN_GLIDE, 1.0, fract(t)); }   // constant-folded when 0
  float a = 2.0 * pi * (f / steps);
  vec3 s = vec3(-sin(a), cos(a), 0.0);
  return (s.y > 0.0) ? s : -s;
}

// Shadow distortion: more texels near the player (distort() in the tutorial).
highp vec2 shadowDistort(highp vec2 p) {
  return p * ((1.0 + SHADOW_DISTORT) / (length(p) + SHADOW_DISTORT));
}

// Orthographic map around camera camAbs; q = position relative to that camera. Snapped to whole texels (no shimmer).
vec4 shadowClip(vec3 q, vec3 camAbs, vec3 L) {
  vec3 right = vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(L.y, -L.x, 0.0);
  float texel = 2.0 * SHADOW_RADIUS / SHADOW_RES;
  vec2 snap = fract(vec2(dot(camAbs, right), dot(camAbs, up)) / texel) * texel;
  vec2 xy = vec2(dot(q, right), dot(q, up)) + snap;
  return vec4(xy / SHADOW_RADIUS, -dot(q, L) / SHADOW_DEPTH_RANGE, 1.0);
}

// Caster: clip position of a vertex in the shadow map. rel = position relative to the camera.
vec4 shadowCaster(vec3 rel, vec3 L) {
  vec4 p = shadowClip(rel, CameraPosition.xyz, L);
  p.xy = shadowDistort(p.xy);
  return p;
}

// Receiver, vertex side: map uv in xy, depth in z, strength in w (0 = no shadow). Send through a varying.
// The map was rendered last frame around LAST frame's camera. Left undistorted so it interpolates exactly.
vec4 shadowReceiver(vec3 rel, vec3 L) {
  vec3 q = rel + (CameraPosition.xyz - LastCameraPosition.xyz);
  vec4 sc = shadowClip(q, LastCameraPosition.xyz, L);
  return vec4(sc.xyz * 0.5 + 0.5, smoothstep(SHADOW_SUN_FADE.x, SHADOW_SUN_FADE.y, L.y));   // fade out when the sun is low
}

// ===================== receiver, fragment side =====================
#if defined(SHADOW_RECEIVE) && defined(SHADOW_RECEIVER)

SAMPLER2D_AUTOREG(s_ShadowMap);       // opaque casters only            buffers/ShadowMap.json   (Iris: shadowtex1)
#if SHADOW_COLORED
SAMPLER2D_AUTOREG(s_ShadowAll);       // opaque + transparent casters   buffers/ShadowAll.json   (Iris: shadowtex0)
SAMPLER2D_AUTOREG(s_ShadowColor);     // colour of the nearest caster   buffers/ShadowColor.json (Iris: shadowcolor0)
#endif

// One map sample: how much sunlight reaches depth z at uv, per colour channel (Iris' getShadow()).
highp vec3 shadowTap(highp vec2 uv, highp float z) {
  #if SHADOW_COLORED
    if (step(z, texture2D(s_ShadowAll, uv).r) > 0.5) { return vec3_splat(1.0); }   // nothing in the way
    if (step(z, texture2D(s_ShadowMap, uv).r) < 0.5) { return vec3_splat(0.0); }   // something opaque in the way
    highp vec4 c = texture2D(s_ShadowColor, uv);                                     // only transparent things in the way
    // even shadow: use only the HUE of the glass, not its per-pixel alpha / frame / highlight pixels
    highp vec3 hue = c.rgb / max(max(c.r, max(c.g, c.b)), 0.001);
    return mix(vec3_splat(1.0), hue, SHADOW_GLASS_TINT) * (1.0 - SHADOW_GLASS_DARK);
  #else
    return vec3_splat(step(z, texture2D(s_ShadowMap, uv).r));
  #endif
}

// Sunlight reaching the pixel: 1 = fully lit, 0 = in shadow, in between / coloured = shadow through glass or water.
// s = receiver varying, ndl = dot(surface normal, sun direction)
highp vec3 shadowLit(highp vec4 s, highp float ndl) {
  if (s.w <= 0.0) { return vec3_splat(1.0); }

  // faces turned away from the sun are always shadowed, no map lookup needed
  highp float face = mix(1.0, smoothstep(0.0, SHADOW_FACE_FADE, ndl), s.w);
  if (face <= 0.0) { return vec3_splat(0.0); }

  highp vec2 p = s.xy * 2.0 - 1.0;                              // undistorted map coordinates
  if (abs(p.x) >= 1.0 || abs(p.y) >= 1.0) { return vec3_splat(face); }

  // same distortion as the caster, and how much bigger a texel got
  highp float k = SHADOW_DISTORT;
  highp vec2 uv = shadowDistort(p) * 0.5 + 0.5;
  highp float rk = length(p) + k;
  highp float stretch = (k > 0.0001) ? rk * rk / (k * (1.0 + k)) : 1.0;
  highp float texelBlocks = 2.0 * SHADOW_RADIUS / SHADOW_RES * clamp(stretch, 1.0, 6.0);

  // bias: constant + slope (bigger texels on tilted faces need more)
  highp float slope = clamp(sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1), 0.0, 6.0);
  highp float z = s.z - (SHADOW_BIAS + texelBlocks * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);

  // soft edge: spiral of taps, rotated per pixel
  highp float blurTexels = clamp(SHADOW_BLUR * SHADOW_RES / (2.0 * SHADOW_RADIUS * stretch), 1.2, 6.0);   // never below ~1 texel, so far edges get filtered too
  highp vec3 lit;
  if (SHADOW_TAPS < 2) {
    lit = shadowTap(uv, z);                                // hard edge requested
  } else {
    // Spiral of taps rotated per pixel. One cos/sin per pixel, each tap is then a cheap 2D rotation.
    highp float spin = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    highp vec2 dir0 = vec2(cos(spin), sin(spin));
    highp float R = blurTexels / SHADOW_RES;
    highp float halfTaps = float(SHADOW_TAPS / 2);
    highp vec3 sum = vec3_splat(0.0);
    // stage 1: every second tap. If they all agree the pixel is fully lit / shadowed and we stop
    highp vec2 dir = dir0;
    for (int j = 0; j < SHADOW_TAPS / 2; j++) {
      highp float d = sqrt((float(2 * j) + 0.5) / float(SHADOW_TAPS));
      sum += shadowTap(uv + dir * (d * R), z);
      dir = vec2(dir.x * 0.08742573 - dir.y * -0.99617104, dir.x * -0.99617104 + dir.y * 0.08742573);
    }
    highp float agree = dot(sum, vec3_splat(0.3333333));   // 0 or halfTaps: all shadowed / all fully lit
    if (agree > 0.001 && agree < halfTaps - 0.001) {
      // stage 2: shadow edge (or tinted light): take the other half
      dir = vec2(dir0.x * -0.73736888 - dir0.y * 0.67549029, dir0.x * 0.67549029 + dir0.y * -0.73736888);
      for (int j = 0; j < SHADOW_TAPS / 2; j++) {
        highp float d = sqrt((float(2 * j + 1) + 0.5) / float(SHADOW_TAPS));
        sum += shadowTap(uv + dir * (d * R), z);
        dir = vec2(dir.x * 0.08742573 - dir.y * -0.99617104, dir.x * -0.99617104 + dir.y * 0.08742573);
      }
      lit = sum / float(SHADOW_TAPS);
    } else {
      lit = sum / halfTaps;
    }
  }

  highp vec2 c = abs(p);
  highp float edge = 1.0 - smoothstep(SHADOW_EDGE_FADE, 1.0, max(c.x, c.y));
  return min(mix(vec3_splat(1.0), lit, edge * s.w), vec3_splat(face));
}

#endif // SHADOW_RECEIVE && SHADOW_RECEIVER
#endif // UTILS_SHADOW_H
