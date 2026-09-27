#pragma once

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

#include <cstdint>
#include <string>
#include <dlfcn.h>
#include <link.h>
#include <android/log.h>
#include <cmath>
#include <algorithm>
#include <cstring>

#define SPL_TAG "SoundPhysicsLite"
#define SPL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPL_TAG, __VA_ARGS__)
#define SPL_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPL_TAG, __VA_ARGS__)

namespace pe {
    // Function body in libminecraftpe.so (NOT PLT)
    constexpr uintptr_t kFnPlayPath = 0x110CB1D0;
    constexpr uintptr_t kOffChannel = 0x78; // [this+0x78] = FMOD::Channel* after playSound
}

using FMOD_RESULT = int;
using FN_SetLowPassGain = FMOD_RESULT (*)(void* channel, float gain);

class SoundPhysicsLite {
public:
    static SoundPhysicsLite& instance();

    SoundPhysicsLite();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

    void onChannelReady(void* channel);

private:
    ll::mod::NativeMod& mSelf;
    bool mEnabled = true;
    bool mHooked = false;
    float mLowpassGain = 0.35f; // stronger muffling for MVP test

    void* mFmod = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;

    bool loadFmod();
    bool installHook();
};
