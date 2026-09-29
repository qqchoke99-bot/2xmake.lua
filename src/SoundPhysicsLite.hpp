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
#include <android/log.h>

#define SPL_TAG "SoundPhysicsLite"
#define SPL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPL_TAG, __VA_ARGS__)
#define SPL_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPL_TAG, __VA_ARGS__)
#define SPL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SPL_TAG, __VA_ARGS__)

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

    float mLowpassGain = 0.75f;
    float mReverbWet = 0.30f;
    int mReverbInstance = 0;
    bool mEnableLowpass = true;
    bool mEnableReverb = true;

    void* mFmod = nullptr;
    void* mSetVolumeTarget = nullptr;
    const char* mSetVolumeName = nullptr;
    FN_SetVolume mOrigSetVolume = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;
    FN_SetReverbProps mSetReverb = nullptr;

    bool loadFmod();
    bool resolveSetVolume();
    static bool addrInAnyMap(uintptr_t addr);
    bool tryHookAt(void* target, const char* name);
    bool installHook();
    void startRetryThread();
    static void* retryThreadMain(void* arg);
    void applyFx(void* channel);
    static int detour_SetVolume(void* channel, float volume);
};
