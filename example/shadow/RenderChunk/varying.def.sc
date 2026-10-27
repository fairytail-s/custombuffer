vec4 a_color0    : COLOR0;
vec2 a_texcoord1 : TEXCOORD1;
vec3 a_position  : POSITION;
vec2 a_texcoord0 : TEXCOORD0;

vec4 i_data1 : TEXCOORD7;
vec4 i_data2 : TEXCOORD6;
vec4 i_data3 : TEXCOORD5;

vec4 v_color0 : COLOR0;
vec4 v_fog : COLOR2;
vec2 v_lightmapUV : TEXCOORD1;
centroid vec2 v_texcoord0  : TEXCOORD0;

vec4 v_shadow : COLOR1;
vec3 v_rel : TEXCOORD2;   // camera-relative position, for the face normal in the fragment shader
vec3 v_sun : TEXCOORD3;   // direction to the sun (or moon)
