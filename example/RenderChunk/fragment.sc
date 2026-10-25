$input v_texcoord0, v_color0, v_fog, v_lightmapUV, v_water

#include <bgfx_shader.sh>

#ifndef DEPTH_ONLY_OPAQUE
  SAMPLER2D_AUTOREG(s_LightMapTexture);
  SAMPLER2D_AUTOREG(s_MatTexture);

  #if defined(SEASONS) && (defined(ALPHA_TEST) || defined(OPAQUE))
    SAMPLER2D_AUTOREG(s_SeasonsTexture);
  #endif
  #ifdef TRANSPARENT
    // buffer
//    SAMPLER2D_AUTOREG(s_ScreenBuffer);
    SAMPLER2D_AUTOREG(s_SceneDepthBuffer);
  #endif
#endif

// water shoreline
#define FOAM_WIDTH 0.6
#define FOAM_COLOR vec3(0.55, 0.75, 0.75)

// depth buffer (0..1) -> view-space distance in blocks.
float sceneDepthLinear(float d) {
  float ndc = d * 2.0 - 1.0;
  return u_proj[3][2] / (ndc + u_proj[2][2]);
}

void main() {
  #ifndef DEPTH_ONLY_OPAQUE
    vec4 diffuse = texture2D(s_MatTexture, v_texcoord0);

    #ifdef ALPHA_TEST
      if (diffuse.a < 0.5) { discard; }
    #endif
    
    highp vec4 color = v_color0;
    #if defined(SEASONS) && (defined(ALPHA_TEST) || defined(OPAQUE))
      diffuse.rgb *= mix(vec3_splat(1.0), 2.0 * texture2D(s_SeasonsTexture, color.xy).rgb, color.y);
      diffuse.rgb *= color.aaa;
    #else
      diffuse *= color;
    #endif

    #ifdef TRANSPARENT
      bool isWater = v_water > 0.5;
      if (isWater) {
        vec2 depthUV = gl_FragCoord.xy / vec2(textureSize(s_SceneDepthBuffer, 0));
        float behind = sceneDepthLinear(texture2D(s_SceneDepthBuffer, depthUV).r);
        float thickness = max(behind - sceneDepthLinear(gl_FragCoord.z), 0.0);
        float foam = 1.0 - smoothstep(0.0, FOAM_WIDTH, thickness);
        diffuse.rgb = mix(diffuse.rgb, FOAM_COLOR, foam);
        diffuse.a = max(diffuse.a, foam);
      }
    #endif

    diffuse.rgb *= texture2D(s_LightMapTexture, v_lightmapUV).xyz;

    diffuse.rgb = mix(diffuse.rgb, v_fog.rgb, v_fog.a);

    gl_FragColor = diffuse;
  #else
    gl_FragColor = vec4_splat(0.0);
  #endif
}

