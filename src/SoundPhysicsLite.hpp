#pragma once

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <dlfcn.h>
#include <link.h>
#include <android/log.h>
#include <cstring>

#define SPL_TAG "SoundPhysicsLite"
#define SPL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPL_TAG, __VA_ARGS__)
#define SPL_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPL_TAG, __VA_ARGS__)
#define SPL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SPL_TAG, __VA_ARGS__)

namespace pe {
    // Body offset for 1.26.51 (re-verify if game version changes)
    constexpr uintptr_t kFnPlayPath = 0x110CB1D0;
    constexpr uintptr_t kOffChannel = 0x78;
}

using FMOD_RESULT = int;
using FN_SetLowPassGain = FMOD_RESULT (*)(void* channel, float gain);
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
    std::atomic<bool> mHooked{false};
    std::atomic<bool> mStopRetry{false};
    pthread_t mRetryThread{};
    bool mRetryStarted = false;

    // Stronger so A/B listen test is easier to hear
    float mLowpassGain = 0.40f;
    bool  mEnableLowpass = true;
    bool  mEnableReverb = true;
    int   mReverbInstance = 0;
    float mReverbWet = 0.55f;

    void* mFmod = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;
    FN_SetReverbProps mSetReverb = nullptr;

    bool loadFmod();
    bool installHook();
    void startRetryThread();
    static void* retryThreadMain(void* arg);
};
