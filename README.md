# Custom Buffers

An open-source native mod for Minecraft Bedrock on Android, made for [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid). It gives materials extra textures of the frame being drawn, and can render shadow maps.

## Features

- `s_ScreenBuffer`: copy of the colour buffer drawn so far (half resolution when the game is not multisampled)
- `s_SceneDepthBuffer`: copy of the depth buffer drawn so far (raw depth in `.r`, 0 to 1)
- `s_ShadowMap`, `s_ShadowAll`, `s_ShadowColor`: shadow maps, including coloured shadows from glass and water
- Every buffer is only copied or drawn when a shader actually uses it

## Requirements

- Android 9 or newer, 64-bit ARM (`arm64-v8a`)
- [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid)
- Minecraft Bedrock **26.52**

## Installation

1. Install LeviLauncher.
2. Import `CustomBuffers.levipack` in the launcher's mod manager and enable it.
3. Launch Minecraft through LeviLauncher.

## Screen and depth buffers

Declare the samplers in the pass that needs them. The copy holds everything drawn before that pass.

```glsl
SAMPLER2D_AUTOREG(s_ScreenBuffer);
SAMPLER2D_AUTOREG(s_SceneDepthBuffer);

vec2 uv = gl_FragCoord.xy / vec2(textureSize(s_ScreenBuffer, 0));
vec2 duv = gl_FragCoord.xy / vec2(textureSize(s_SceneDepthBuffer, 0));
vec3 screen = texture2D(s_ScreenBuffer, uv).rgb;
float depth = texture2D(s_SceneDepthBuffer, duv).r;
```

- Keep the depth sampler on **Point** filtering.
- Without the mod, the colour sampler is white and the depth sampler reads 1.0.
- When there is no copy for a draw (a world closing, a thumbnail render), the mod binds a 1x1 white texture. `textureSize(s_ScreenBuffer, 0).x < 8` tells a material to skip its effect.

## Shadows

The mod redraws the world from the sun into a depth map and gives it back to materials. The material decides what casts and what receives. A complete working pack is in [`example/shadow/`](example/shadow/), and [docs/shadows.md](docs/shadows.md) explains it step by step.

| Uniform / sampler | Declared by | Meaning |
| --- | --- | --- |
| `ShadowPass` (`vec4`) | opaque casters | `x = 1` while the mod redraws that program into the map |
| `ShadowTransparentPass` (`vec4`) | transparent casters (glass, water) | same, into the coloured all-casters map |
| `s_ShadowMap`, `s_ShadowAll`, `s_ShadowColor` | receivers | opaque depth, all-casters depth, colour of the nearest caster |
| `ShadowRes` (`vec4`) | casters | `x` = texels per side per cascade (256-4096, default 1024 if unset), `y` = depth bits (16 default, 24, 32 = float) |
| `ShadowCull` (`vec4`) | terrain casters | `x` = map radius in blocks (default 64), `y` = depth range (default 400), `z = 1` skips chunk draws outside the map |

- Every caster draw is issued into the shadow atlas; chunks outside the map volume are culled automatically.
- Receivers read the map finished in the **previous** frame, so use `LastCameraPosition` to line up with it.
- 2-Cascade Shadow Mapping (CSM) uses stabilized linear orthographic projection (Filament / MJP standard) with a 2-tile atlas:
  - Cascade 0 (near, 16 blocks radius): 32 texels/block at 1024 resolution for razor-sharp contact shadows.
  - Cascade 1 (far, 64 blocks radius): 8 texels/block covering the full 64-block view distance.
- Soft shadow edges use 4-tap PCF with smooth cascade blending and dynamic slope bias.
- The depth format fallback and polygon-offset bias are constants at the top of `src/GlHooks.cpp`.

## Build

Requires Android NDK r28c, Python 3, and xmake or CMake 3.22+.

```sh
# xmake
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c --ndk_sdkver=28
xmake -y

# CMake
cmake -S . -B build-cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk-r28c/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel
```

Both produce `CustomBuffers.so` and `CustomBuffers.levipack`.
