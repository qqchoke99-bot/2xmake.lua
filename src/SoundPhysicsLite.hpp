#pragma once

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

#include <atomic>
#include <cmath>
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

struct FMOD_VECTOR {
    float x, y, z;
};

using FN_SetVolume = int (*)(void* channel, float volume);
using FN_SetLowPassGain = int (*)(void* channel, float gain);
using FN_SetReverbProps = int (*)(void* channel, int instance, float wet);
using FN_Set3DOcclusion = int (*)(void* channel, float direct, float reverb);
using FN_Get3DAttributes = int (*)(void* channel, FMOD_VECTOR* pos, FMOD_VECTOR* vel);
using FN_GetSystemObject = int (*)(void* channel, void** system);
using FN_Get3DListenerAttributes = int (*)(void* system, int listener, FMOD_VECTOR* pos,
                                           FMOD_VECTOR* vel, FMOD_VECTOR* forward, FMOD_VECTOR* up);

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

    // Base FX + distance-driven occlusion (no block raycast yet)
    float mLowpassNear = 0.90f;
    float mLowpassFar = 0.25f;
    float mReverbNear = 0.20f;
    float mReverbFar = 0.90f;
    float mMaxDist = 48.0f;
    float mOccDirectNear = 0.0f;
    float mOccDirectFar = 0.70f;
    float mOccReverbNear = 0.0f;
    float mOccReverbFar = 0.40f;

    void* mFmod = nullptr;
    void* mSetVolumeTarget = nullptr;
    const char* mSetVolumeName = nullptr;
    FN_SetVolume mOrigSetVolume = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;
    FN_SetReverbProps mSetReverb = nullptr;
    FN_Set3DOcclusion mSetOcclusion = nullptr;
    FN_Get3DAttributes mGet3DAttr = nullptr;
    FN_GetSystemObject mGetSystem = nullptr;
    FN_Get3DListenerAttributes mGetListener = nullptr;

    bool loadFmod();
    static bool addrInAnyMap(uintptr_t addr);
    bool tryHookAt(void* target, const char* name);
    bool installHook();
    void startRetryThread();
    static void* retryThreadMain(void* arg);
    static float clampf(float v, float lo, float hi);
    static float lerpf(float a, float b, float t);
    void applyFx(void* channel, float volume);
    static int detour_SetVolume(void* channel, float volume);
};
