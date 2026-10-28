$input a_position, a_color0, a_texcoord0, a_indices, a_normal
#ifdef INSTANCING
  $input i_data0, i_data1, i_data2
#endif

$output v_color0, v_fog, v_light, v_texcoord0, v_shadow, v_sun, v_normal

#include <bgfx_shader.sh>
#include <MinecraftRenderer.Materials/DynamicUtil.dragonh>
#include <MinecraftRenderer.Materials/TAAUtil.dragonh>
#include <MinecraftRenderer.Materials/FogUtil.dragonh>
#include <utils/shadow.h>

uniform vec4 OverlayColor;
uniform vec4 TileLightColor;
uniform vec4 FogColor;
uniform vec4 FogControl;
uniform vec4 UVAnimation;
uniform mat4 Bones[8];
uniform vec4 ViewPositionAndTime;

void main() {
  mat4 World = u_model[0];

  World = mul(World, Bones[int(a_indices)]);

  vec2 texcoord0 = a_texcoord0;
  texcoord0 = applyUvAnimation(texcoord0, UVAnimation);

  vec3 worldPosition;
  #ifdef INSTANCING
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, vec4(0.0, 0.0, 0.0, 1.0));
    worldPosition = instMul(model, vec4(a_position, 1.0)).xyz;
  #else
    worldPosition = mul(World, vec4(a_position, 1.0)).xyz;
  #endif

  vec4 position = jitterVertexPosition(worldPosition);

  bool ui = u_proj[3][3] > 0.5;

  #ifdef SHADOW
    if (ShadowPass.x > 0.5) {
      v_texcoord0 = texcoord0;
      v_color0 = a_color0;
      gl_Position = ui ? vec4(0.0, 0.0, 2.0, 1.0)
                       : shadowCaster(worldPosition - ViewPositionAndTime.xyz, sunDirection(), ShadowPass.y);
      return;
    }
  #endif
  #if !(defined(DEPTH_ONLY_OPAQUE) || defined(DEPTH_ONLY) || defined(INSTANCING))

    float lightIntensity = calculateLightIntensity(World, vec4(a_normal.xyz, 0.0), TileLightColor);
    lightIntensity += OverlayColor.a * 0.35;
    vec4 light = vec4(lightIntensity * TileLightColor.rgb, 1.0);
    
    vec3 modelCamPos = (ViewPositionAndTime.xyz - worldPosition);
    float camDis = length(modelCamPos);
    float fogIntensity = ui ? 0.0 : calculateFogIntensity(camDis, FogControl.z, FogControl.x, FogControl.y);
    vec4 fog = vec4(FogColor.rgb, fogIntensity);

    v_texcoord0 = texcoord0;
    v_color0 = a_color0;
    v_fog = fog;
    v_light = light;

    v_shadow = vec4(0.0, 0.0, 1.0, 0.0);
    v_sun = vec3(0.0, 1.0, 0.0);
    v_normal = vec3(0.0, 1.0, 0.0);
    #ifdef SHADOW
      v_sun = sunDirection();
      v_normal = normalize(mul(World, vec4(a_normal.xyz, 0.0)).xyz);
      if (!ui && ShadowPass.x < 0.5) {
        v_shadow = shadowReceiver(worldPosition - ViewPositionAndTime.xyz, v_sun);
      }
    #endif
  #endif

  gl_Position = position;
}
