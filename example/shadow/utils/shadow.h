#ifndef UTILS_SHADOW_H
#define UTILS_SHADOW_H

uniform vec4 CameraPosition;
uniform vec4 LastCameraPosition;
uniform vec4 GameTimeOfDay;
uniform vec4 ShadowPass;
uniform vec4 ShadowTransparentPass;
uniform vec4 ShadowRes;
uniform vec4 ShadowCull;

#define SHADOW_RADIUS_NEAR 36.0
#define SHADOW_RADIUS_MID  72.0
#define SHADOW_RADIUS      ShadowCull.x
#define SHADOW_DEPTH_RANGE ShadowCull.y
#define SHADOW_BRIGHTNESS  0.55
#define SHADOW_BIAS        0.14
#define SHADOW_SLOPE_BIAS  2.0
#define SHADOW_TAPS        4
#define SHADOW_BLUR        0.12
#define SHADOW_FACE_FADE   0.15
#define SHADOW_EDGE_FADE   0.85
#define SHADOW_GLASS_TINT  0.85
#define SHADOW_GLASS_DARK  0.30
#define SHADOW_TRANSPARENT_RECEIVE 0
#define SHADOW_COLORED     1
#define SHADOW_RES         ShadowRes.x
#define SHADOW_SUN_STEP    0.5
#define SHADOW_SUN_GLIDE   0.0
#define SHADOW_DEBUG_FREEZE_SUN -1.0
#define SHADOW_SUN_FADE    vec2(0.10, 0.35)
#define pi 3.14159265359

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

vec3 sunDirection() {
  float steps = 360.0 / SHADOW_SUN_STEP;
  float t = ((SHADOW_DEBUG_FREEZE_SUN >= 0.0) ? SHADOW_DEBUG_FREEZE_SUN : fract(GameTimeOfDay.x)) * steps;
  float f = floor(t + 0.001);
  if (SHADOW_SUN_GLIDE > 0.0) { f += smoothstep(1.0 - SHADOW_SUN_GLIDE, 1.0, fract(t)); }
  float a = 2.0 * pi * (f / steps);
  vec3 s = vec3(-sin(a), cos(a), 0.0);
  return (s.y > 0.0) ? s : -s;
}

vec4 shadowClip(vec3 q, vec3 camAbs, vec3 L, float radius) {
  vec3 right = vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(L.y, -L.x, 0.0);
  float texel = 2.0 * SHADOW_RADIUS / SHADOW_RES;
  vec2 snap = fract(vec2(dot(camAbs, right), dot(camAbs, up)) / texel) * texel;
  vec2 xy = vec2(dot(q, right), dot(q, up)) + snap;
  return vec4(xy / radius, -dot(q, L) / SHADOW_DEPTH_RANGE, 1.0);
}

vec4 shadowCaster(vec3 rel, vec3 L, float cascade) {
  float radius = (cascade < 0.5) ? SHADOW_RADIUS_NEAR : ((cascade < 1.5) ? SHADOW_RADIUS_MID : SHADOW_RADIUS);
  return shadowClip(rel, CameraPosition.xyz, L, radius);
}

vec4 shadowReceiver(vec3 rel, vec3 L) {
  vec3 q = rel + (CameraPosition.xyz - LastCameraPosition.xyz);
  vec4 sc = shadowClip(q, LastCameraPosition.xyz, L, SHADOW_RADIUS);
  return vec4(sc.xyz * 0.5 + 0.5, smoothstep(SHADOW_SUN_FADE.x, SHADOW_SUN_FADE.y, L.y));
}

#if defined(SHADOW_RECEIVE) && defined(SHADOW_RECEIVER)

SAMPLER2D_AUTOREG(s_ShadowMap);
#if SHADOW_COLORED
SAMPLER2D_AUTOREG(s_ShadowAll);
SAMPLER2D_AUTOREG(s_ShadowColor);
#endif

highp vec3 shadowTap(highp vec2 uv, highp float z) {
  #if SHADOW_COLORED
    if (step(z, texture2D(s_ShadowAll, uv).r) > 0.5) { return vec3_splat(1.0); }
    if (step(z, texture2D(s_ShadowMap, uv).r) < 0.5) { return vec3_splat(0.0); }
    highp vec4 c = texture2D(s_ShadowColor, uv);
    highp vec3 hue = c.rgb / max(max(c.r, max(c.g, c.b)), 0.001);
    return mix(vec3_splat(1.0), hue, SHADOW_GLASS_TINT) * (1.0 - SHADOW_GLASS_DARK);
  #else
    return vec3_splat(step(z, texture2D(s_ShadowMap, uv).r));
  #endif
}

highp vec3 sampleCascadePCF(highp vec2 uvBase, highp float z, highp vec2 uvMin, highp vec2 uvMax) {
  highp vec2 stepUV = vec2(1.0 / (3.0 * SHADOW_RES), 1.0 / SHADOW_RES);
  highp vec3 sum = shadowTap(clamp(uvBase + vec2(-0.38, -0.18) * stepUV, uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2( 0.38,  0.18) * stepUV, uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2(-0.18,  0.38) * stepUV, uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2( 0.18, -0.38) * stepUV, uvMin, uvMax), z);
  return sum * 0.25;
}

highp vec3 shadowLit(highp vec4 s, highp float ndl) {
  if (s.w <= 0.0) { return vec3_splat(1.0); }

  highp float face = mix(1.0, smoothstep(0.0, SHADOW_FACE_FADE, ndl), s.w);
  if (face <= 0.0) { return vec3_splat(0.0); }

  highp vec2 p2 = s.xy * 2.0 - 1.0;
  if (abs(p2.x) >= 1.0 || abs(p2.y) >= 1.0) { return vec3_splat(face); }

  highp vec2 p1 = p2 * (SHADOW_RADIUS / SHADOW_RADIUS_MID);
  highp vec2 p0 = p2 * (SHADOW_RADIUS / SHADOW_RADIUS_NEAR);

  float texel0 = 2.0 * SHADOW_RADIUS_NEAR / SHADOW_RES;
  float texel1 = 2.0 * SHADOW_RADIUS_MID / SHADOW_RES;
  float texel2 = 2.0 * SHADOW_RADIUS / SHADOW_RES;

  highp float slope = clamp(sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1), 0.0, 5.0);
  highp float z0 = s.z - (SHADOW_BIAS + texel0 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);
  highp float z1 = s.z - (SHADOW_BIAS + texel1 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);
  highp float z2 = s.z - (SHADOW_BIAS + texel2 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);

  highp vec2 uv0 = vec2((p0.x * 0.5 + 0.5) * (1.0 / 3.0), p0.y * 0.5 + 0.5);
  highp vec2 uv1 = vec2((p1.x * 0.5 + 0.5) * (1.0 / 3.0) + (1.0 / 3.0), p1.y * 0.5 + 0.5);
  highp vec2 uv2 = vec2((p2.x * 0.5 + 0.5) * (1.0 / 3.0) + (2.0 / 3.0), p2.y * 0.5 + 0.5);

  highp float halfTexelU = 0.5 / (3.0 * SHADOW_RES);
  highp float halfTexelV = 0.5 / SHADOW_RES;

  highp vec2 uvMin0 = vec2(halfTexelU, halfTexelV);
  highp vec2 uvMax0 = vec2((1.0 / 3.0) - halfTexelU, 1.0 - halfTexelV);
  highp vec2 uvMin1 = vec2((1.0 / 3.0) + halfTexelU, halfTexelV);
  highp vec2 uvMax1 = vec2((2.0 / 3.0) - halfTexelU, 1.0 - halfTexelV);
  highp vec2 uvMin2 = vec2((2.0 / 3.0) + halfTexelU, halfTexelV);
  highp vec2 uvMax2 = vec2(1.0 - halfTexelU, 1.0 - halfTexelV);

  highp float d0 = max(abs(p0.x), abs(p0.y));
  highp float d1 = max(abs(p1.x), abs(p1.y));
  highp vec3 lit;
  if (d0 < 1.0) {
    if (d0 > 0.85) {
      highp float blend0 = smoothstep(0.85, 1.0, d0);
      highp float zBlend0 = mix(z0, z1, blend0);
      highp vec3 lit0 = sampleCascadePCF(uv0, zBlend0, uvMin0, uvMax0);
      highp vec3 lit1 = sampleCascadePCF(uv1, zBlend0, uvMin1, uvMax1);
      lit = mix(lit0, lit1, blend0);
    } else {
      lit = sampleCascadePCF(uv0, z0, uvMin0, uvMax0);
    }
  } else if (d1 < 1.0) {
    if (d1 > 0.85) {
      highp float blend1 = smoothstep(0.85, 1.0, d1);
      highp float zBlend1 = mix(z1, z2, blend1);
      highp vec3 lit1 = sampleCascadePCF(uv1, zBlend1, uvMin1, uvMax1);
      highp vec3 lit2 = sampleCascadePCF(uv2, zBlend1, uvMin2, uvMax2);
      lit = mix(lit1, lit2, blend1);
    } else {
      lit = sampleCascadePCF(uv1, z1, uvMin1, uvMax1);
    }
  } else {
    lit = sampleCascadePCF(uv2, z2, uvMin2, uvMax2);
  }

  highp vec2 c = abs(p2);
  highp float edge = 1.0 - smoothstep(SHADOW_EDGE_FADE, 1.0, max(c.x, c.y));
  return min(mix(vec3_splat(1.0), lit, edge * s.w), vec3_splat(face));
}

#endif
#endif
