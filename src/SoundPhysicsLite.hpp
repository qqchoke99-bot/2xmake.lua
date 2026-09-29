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
    // File RVA (not runtime VA). From RE of libminecraftpe.so 1.26.51.
    // PLT stub for FMOD::System::playSound — safe ABI (documented FMOD).
    constexpr uintptr_t kPltPlaySound = 0x1298F5A0;
    // Body of play-path (caller). DO NOT hook until signature fully RE'd.
    // constexpr uintptr_t kFnPlayPath = 0x110CB1D0;
    // Channel* stored at object+0x78 after playSound in that function.
    // constexpr uintptr_t kOffChannel = 0x78;
}

// FMOD_RESULT System::playSound(Sound*, ChannelGroup*, bool, Channel**)
// AArch64: x0=this(System*), x1=Sound*, x2=ChannelGroup*, w3=paused, x4=Channel**
using FN_PlaySound = int (*)(void* sys, void* sound, void* group, int paused, void** outChannel);
using FN_SetLowPassGain = int (*)(void* channel, float gain);
using FN_SetReverbProps = int (*)(void* channel, int instance, float wet);

struct PeMap {
    uintptr_t start = 0;
    uintptr_t end = 0;
};

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

    float mLowpassGain = 0.45f;
    float mReverbWet = 0.40f;
    int mReverbInstance = 0;
    bool mEnableLowpass = true;
    bool mEnableReverb = true;

    void* mFmod = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;
    FN_SetReverbProps mSetReverb = nullptr;
    FN_PlaySound mOrigPlaySound = nullptr;

    static bool findPeMap(PeMap& out);
    static bool addrInMap(uintptr_t addr, const PeMap& m);
    bool loadFmod();
    bool installHook();
    void startRetryThread();
    static void* retryThreadMain(void* arg);
    void applyFx(void* channel);
    static int detour_PlaySound(void* sys, void* sound, void* group, int paused, void** outChannel);
};
