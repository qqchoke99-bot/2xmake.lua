#pragma once

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

#include <cstdint>
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
    constexpr uintptr_t kFnPlayPath = 0x110CB1D0; // function body, not PLT
    constexpr uintptr_t kOffChannel = 0x78;        // Channel* after playSound
}

using FMOD_RESULT = int;
using FN_SetLowPassGain = FMOD_RESULT (*)(void* channel, float gain);
// FMOD_Channel_SetReverbProperties(channel, instance, wet)
using FN_SetReverbProps = FMOD_RESULT (*)(void* channel, int instance, float wet);

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

    // Softer than 0.35 so near sounds are less "dying"
    float mLowpassGain = 0.65f;
    bool  mEnableLowpass = true;
    bool  mEnableReverb = true;
    int   mReverbInstance = 0;   // FMOD reverb slot
    float mReverbWet = 0.35f;    // 0 = dry, 1 = full wet

    void* mFmod = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;
    FN_SetReverbProps mSetReverb = nullptr;

    bool loadFmod();
    bool installHook();
};
