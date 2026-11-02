$input v_texcoord0, v_color0, v_fog, v_lightmapUV, v_shadow, v_rel, v_sun

#include <bgfx_shader.sh>

#ifndef DEPTH_ONLY_OPAQUE
  SAMPLER2D_AUTOREG(s_LightMapTexture);
  SAMPLER2D_AUTOREG(s_MatTexture);

  #if defined(SEASONS) && (defined(ALPHA_TEST) || defined(OPAQUE))
    SAMPLER2D_AUTOREG(s_SeasonsTexture);
  #endif
#endif

#define SHADOW_RECEIVER 1
#include <utils/shadow.h>

highp vec3 faceNormal(highp vec3 rel) {
  highp vec3 n = cross(dFdx(rel) * 64.0, dFdy(rel) * 64.0);
  highp float len = length(n);
  n = (len > 0.0) ? n / len : vec3(0.0, 1.0, 0.0);
  return (dot(n, rel) > 0.0) ? -n : n;
}

void main() {
  #ifndef DEPTH_ONLY_OPAQUE
    #if defined(SHADOW) && !defined(ALPHA_TEST)
      if (ShadowPass.x > 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 0.0);
        return;
      }
    #endif

    vec4 diffuse = texture2D(s_MatTexture, v_texcoord0);

    #ifdef ALPHA_TEST
      if (diffuse.a < 0.5) { discard; }
    #endif

    #if defined(SHADOW) && defined(ALPHA_TEST)
      if (ShadowPass.x > 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 0.0);
        return;
      }
    #endif

    #ifdef SHADOW_TRANSPARENT
      if (ShadowTransparentPass.x > 0.5) {
        vec4 caster = diffuse * v_color0;
        if (caster.a < 0.1) { discard; }
        gl_FragColor = caster;
        return;
      }
    #endif

    highp vec4 color = v_color0;
    #if defined(SEASONS) && (defined(ALPHA_TEST) || defined(OPAQUE))
      diffuse.rgb *= mix(vec3_splat(1.0), 2.0 * texture2D(s_SeasonsTexture, color.xy).rgb, color.y);
      diffuse.rgb *= color.aaa;
    #else
      diffuse *= color;
    #endif

    diffuse.rgb *= texture2D(s_LightMapTexture, v_lightmapUV).xyz;

    #ifdef SHADOW_RECEIVE
      float skyExposure = clamp(v_lightmapUV.y * 1.2, 0.0, 1.0);
      if (skyExposure > 0.0 && v_shadow.w > 0.0) {
        highp vec3 sunlight = shadowLit(v_shadow, dot(faceNormal(v_rel), normalize(v_sun)));
        diffuse.rgb *= mix(vec3_splat(1.0), mix(vec3_splat(SHADOW_BRIGHTNESS), vec3_splat(1.0), sunlight), skyExposure);
      }
    #endif

    diffuse.rgb = mix(diffuse.rgb, v_fog.rgb, v_fog.a);
    gl_FragColor = diffuse;
  #else
    gl_FragColor = vec4_splat(0.0);
  #endif
}
