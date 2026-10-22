$input a_color0, a_position, a_texcoord0, a_texcoord1
#ifdef INSTANCING
    $input i_data1, i_data2, i_data3
#endif
$output v_color0, v_fog, v_texcoord0, v_lightmapUV, v_water

#include <bgfx_shader.sh>
#include <MinecraftRenderer.Materials/FogUtil.dragonh>

uniform vec4 FogAndDistanceControl;
uniform vec4 RenderChunkFogAlpha;
uniform vec4 ViewPositionAndTime;
uniform vec4 FogColor;

void main() {
  #ifdef INSTANCING
    mat4 model = mtxFromCols(i_data1, i_data2, i_data3, vec4(0.0, 0.0, 0.0, 1.0));
  #else
    mat4 model = u_model[0];
  #endif
  vec3 worldPos = mul(model, vec4(a_position, 1.0)).xyz;
  vec4 position = mul(u_viewProj, vec4(worldPos, 1.0));

  vec4 color;
  #ifdef RENDER_AS_BILLBOARDS
    worldPos += vec3(0.5, 0.5, 0.5);
    vec3 viewDir = normalize(worldPos - ViewPositionAndTime.xyz);
    vec3 boardPlane = normalize(vec3(viewDir.z, 0.0, -viewDir.x));
    worldPos -= (viewDir.yzx*boardPlane.zxy - viewDir.zxy*boardPlane.yzx)*(a_color0.z - 0.5) + boardPlane*(a_color0.x - 0.5);
    color = vec4_splat(1.0);
  #else
    color = a_color0;
  #endif

    vec3 modelCamPos = (ViewPositionAndTime.xyz - worldPos);
    float camDis = length(modelCamPos);
    float fogIntensity = calculateFogIntensityFaded(camDis, FogAndDistanceControl.z, FogAndDistanceControl.x, FogAndDistanceControl.y, RenderChunkFogAlpha.x);
    vec4 fog = vec4(FogColor.rgb, fogIntensity);

  #ifdef TRANSPARENT
    if (a_color0.a < 0.95) {
      color.a = mix(a_color0.a, 1.0, clamp((camDis / FogAndDistanceControl.w), 0.0, 1.0));
    };
    float water = 0.0;
    if (a_color0.r != a_color0.g || a_color0.g != a_color0.b) { water = 1.0; }
    v_water = water;
  #endif

  vec2 uv0 = 2.0*a_texcoord0.xy;
  uv0 = fract(uv0) + ((floor(uv0)-0.5)/16384.0);

  vec2 uv1 = fract(a_texcoord1.y*vec2(256.0, 4096.0));

  v_color0 = color;
  v_texcoord0 = uv0;
  v_lightmapUV = uv1;
  v_fog = fog;

  gl_Position = position;
}
