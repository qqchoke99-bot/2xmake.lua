# Sound Physics Lite (LeviLauncher ARM64)

MVP: hook `libminecraftpe.so` function body `0x110CB1D0`, read `Channel*` at `this+0x78`, call `FMOD_Channel_SetLowPassGain`.

## GitHub Actions
Push to GitHub → workflow **Build SPL.levipack** → download artifact **SPL-levipack-arm64**.

## Local build
```bash
export ANDROID_NDK_HOME=/path/to/ndk
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared
cmake --build build --parallel
# output: build/SPL.levipack
```

## Dependencies (FetchContent)
- https://github.com/LiteLDev/preloader-android @ 0.2.2
- nlohmann/json, fmt

Public headers only: `pl/Mod.hpp`, `pl/memory/Hook.hpp`
