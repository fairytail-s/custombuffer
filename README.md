# Custom Buffers

Custom Buffers is an open-source native mod for Minecraft Bedrock on Android, made for [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid).

It gives materials extra textures of the frame being drawn.

## Features

- `s_ScreenBuffer`: a copy of the colour buffer drawn so far (half resolution when the game is not multisampled)
- `s_SceneDepthBuffer`: a copy of the depth buffer drawn so far (raw depth in `.r`, 0 to 1)
- Each buffer is only copied when a shader actually uses it

## Requirements

- Android 9 or newer and a 64-bit ARM device (`arm64-v8a`)
- [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) to load the mod
- Minecraft Bedrock **26.52**

## Installation

1. Install LeviLauncher.
2. Import `CustomBuffers.levipack` through the launcher's mod manager and enable it.
3. Launch Minecraft through LeviLauncher.

## Use in a material

Add the buffers you need as `texture2D` (see `example/RenderChunk/buffers`: `ScreenBuffer` is slot 3), then declare the samplers in the pass that needs them:

```glsl
SAMPLER2D_AUTOREG(s_ScreenBuffer);
SAMPLER2D_AUTOREG(s_SceneDepthBuffer);

vec2 uv = gl_FragCoord.xy / vec2(textureSize(s_ScreenBuffer, 0));
vec2 duv = gl_FragCoord.xy / vec2(textureSize(s_SceneDepthBuffer, 0));
vec3 screen = texture2D(s_ScreenBuffer, uv).rgb;
float depth = texture2D(s_SceneDepthBuffer, duv).r;
```

The copy holds everything drawn before the pass that declares the sampler. Keep the depth buffer's sampler filter on **Point**. Without the mod the colour sampler is white and the depth sampler reads 1.0. When the mod has no copy for a draw (for example while a world is closing or its thumbnail is rendered), it binds a 1x1 white texture instead, so `textureSize(s_ScreenBuffer, 0).x < 8` tells a material to skip its effect rather than sample whatever the game left on that unit.

## Build

Requires Android NDK r28c, Python 3, and either xmake or CMake 3.22 or newer.

### xmake

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c --ndk_sdkver=28
xmake -y
```

### CMake

```sh
cmake -S . -B build-cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk-r28c/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel
```

Both builds produce `CustomBuffers.so` and `CustomBuffers.levipack`.
