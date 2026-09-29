#pragma once

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <android/log.h>

#define SPL_TAG "SoundPhysicsLite"
#define SPL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPL_TAG, __VA_ARGS__)
#define SPL_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPL_TAG, __VA_ARGS__)
#define SPL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SPL_TAG, __VA_ARGS__)

namespace pe {
    // PLT: FMOD::ChannelControl::setVolume(float) — confirmed in libminecraftpe.so
    constexpr uintptr_t kPltSetVolume = 0x1298F5B0;
}

// AArch64: x0 = ChannelControl*, s0 = volume → FMOD_RESULT in w0
using FN_SetVolume = int (*)(void* channel, float volume);
using FN_SetLowPassGain = int (*)(void* channel, float gain);
using FN_SetReverbProps = int (*)(void* channel, int instance, float wet);

class SoundPhysicsLite {
public:
    static SoundPhysicsLite& instance();
    SoundPhysicsLite();
    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
    std::atomic<bool> mHooked{false};
    std::atomic<bool> mStopRetry{false};
    pthread_t mRetryThread{};
    bool mRetryStarted = false;

    // Mild — must not kill sound (v0.11 playSound hook did)
    float mLowpassGain = 0.80f;
    float mReverbWet = 0.25f;
    int mReverbInstance = 0;
    bool mEnableLowpass = true;
    bool mEnableReverb = true;

    void* mFmod = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;
    FN_SetReverbProps mSetReverb = nullptr;
    FN_SetVolume mOrigSetVolume = nullptr;

    static uintptr_t peBias();
    static bool targetInPeMaps(uintptr_t addr);
    bool loadFmod();
    bool installHook();
    void startRetryThread();
    static void* retryThreadMain(void* arg);
    void applyFx(void* channel);
    static int detour_SetVolume(void* channel, float volume);
};
