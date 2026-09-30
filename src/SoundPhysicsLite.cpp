#include "SoundPhysicsLite.hpp"

SoundPhysicsLite& SoundPhysicsLite::instance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite() : mSelf(*ll::mod::NativeMod::current()) {}

float SoundPhysicsLite::clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float SoundPhysicsLite::lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

bool SoundPhysicsLite::addrInAnyMap(uintptr_t addr) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t a = 0, b = 0;
        if (sscanf(line, "%lx-%lx", &a, &b) == 2 && addr >= a && addr < b) {
            ok = true;
            break;
        }
    }
    fclose(f);
    return ok;
}

bool SoundPhysicsLite::loadFmod() {
    if (!mFmod) {
        mFmod = dlopen("libfmod.so", RTLD_NOW | RTLD_NOLOAD);
        if (!mFmod) mFmod = dlopen("libfmod.so", RTLD_NOW);
    }
    if (!mFmod) {
        SPL_LOGW("libfmod.so not loaded yet");
        return false;
    }
    auto R = [&](const char* a, const char* b) -> void* {
        void* p = dlsym(mFmod, a);
        return p ? p : (b ? dlsym(mFmod, b) : nullptr);
    };

    if (!mSetLPGain)
        mSetLPGain = reinterpret_cast<FN_SetLowPassGain>(
            R("FMOD_Channel_SetLowPassGain", "FMOD5_Channel_SetLowPassGain"));
    if (!mSetReverb)
        mSetReverb = reinterpret_cast<FN_SetReverbProps>(
            R("FMOD_Channel_SetReverbProperties", "FMOD5_Channel_SetReverbProperties"));
    if (!mSetOcclusion)
        mSetOcclusion = reinterpret_cast<FN_Set3DOcclusion>(
            R("FMOD_Channel_Set3DOcclusion", "FMOD5_Channel_Set3DOcclusion"));
    if (!mGet3DAttr)
        mGet3DAttr = reinterpret_cast<FN_Get3DAttributes>(
            R("FMOD_Channel_Get3DAttributes", "FMOD5_Channel_Get3DAttributes"));
    if (!mGetSystem)
        mGetSystem = reinterpret_cast<FN_GetSystemObject>(
            R("FMOD_Channel_GetSystemObject", "FMOD5_Channel_GetSystemObject"));
    if (!mGetListener)
        mGetListener = reinterpret_cast<FN_Get3DListenerAttributes>(
            R("FMOD_System_Get3DListenerAttributes", "FMOD5_System_Get3DListenerAttributes"));

    SPL_LOGE("FMOD h=%p LP=%p REV=%p OCC=%p G3D=%p GSys=%p GLis=%p", mFmod, (void*)mSetLPGain,
             (void*)mSetReverb, (void*)mSetOcclusion, (void*)mGet3DAttr, (void*)mGetSystem,
             (void*)mGetListener);
    return true;
}

void SoundPhysicsLite::applyFx(void* channel, float volume) {
    if (!channel || volume <= 0.001f) return;

    float dist = -1.0f;
    float t = 0.35f; // default mid if positions unavailable

    if (mGet3DAttr) {
        FMOD_VECTOR pos{}, vel{};
        if (mGet3DAttr(channel, &pos, &vel) == 0) {
            FMOD_VECTOR lpos{};
            bool gotListener = false;
            if (mGetSystem && mGetListener) {
                void* sys = nullptr;
                if (mGetSystem(channel, &sys) == 0 && sys) {
                    FMOD_VECTOR lvel{}, fwd{}, up{};
                    if (mGetListener(sys, 0, &lpos, &lvel, &fwd, &up) == 0) {
                        gotListener = true;
                    }
                }
            }
            if (gotListener) {
                float dx = pos.x - lpos.x;
                float dy = pos.y - lpos.y;
                float dz = pos.z - lpos.z;
                dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                t = clampf(dist / mMaxDist, 0.0f, 1.0f);
            }
        }
    }

    const float lp = lerpf(mLowpassNear, mLowpassFar, t);
    const float wet = lerpf(mReverbNear, mReverbFar, t);
    const float occD = lerpf(mOccDirectNear, mOccDirectFar, t);
    const float occR = lerpf(mOccReverbNear, mOccReverbFar, t);

    if (mSetLPGain) mSetLPGain(channel, lp);
    if (mSetReverb) {
        mSetReverb(channel, 0, wet);
        mSetReverb(channel, 1, wet * 0.65f);
    }
    if (mSetOcclusion) mSetOcclusion(channel, occD, occR);

    static std::atomic<int> n{0};
    int c = ++n;
    if (c <= 35) {
        SPL_LOGE("FX v16 #%d vol=%.2f dist=%.1f t=%.2f lp=%.2f wet=%.2f occ=%.2f/%.2f", c, volume,
                 dist, t, lp, wet, occD, occR);
    }
}

int SoundPhysicsLite::detour_SetVolume(void* channel, float volume) {
    auto& self = SoundPhysicsLite::instance();
    int r = 0;
    if (self.mOrigSetVolume) {
        r = self.mOrigSetVolume(channel, volume);
    }
    if (channel) self.applyFx(channel, volume);
    return r;
}

bool SoundPhysicsLite::tryHookAt(void* target, const char* name) {
    void* orig = nullptr;
    const bool ok = pl::memory::hook(
        target,
        reinterpret_cast<void*>(&SoundPhysicsLite::detour_SetVolume),
        &orig,
        pl::memory::HookPriority::Normal);

    SPL_LOGE("hook attempt name=%s target=%p ok=%d orig=%p", name, target, (int)ok, orig);
    if (!orig) {
        SPL_LOGW("no trampoline for %s", name);
        return false;
    }
    mOrigSetVolume = reinterpret_cast<FN_SetVolume>(orig);
    mSetVolumeTarget = target;
    mSetVolumeName = name;
    mHooked.store(true);
    SPL_LOGE("HOOKED %s target=%p orig=%p", name, target, orig);
    return true;
}

bool SoundPhysicsLite::installHook() {
    if (mHooked.load()) return true;
    if (!loadFmod()) return false;

    // Entry point only — do NOT hook playSound / 0x110CB1D0
    const char* names[] = {
        "_ZN4FMOD14ChannelControl9setVolumeEf",
        "FMOD_Channel_SetVolume",
        "FMOD5_Channel_SetVolume",
        nullptr,
    };

    for (int i = 0; names[i]; ++i) {
        void* p = dlsym(mFmod, names[i]);
        if (!p || !addrInAnyMap(reinterpret_cast<uintptr_t>(p))) continue;
        if (tryHookAt(p, names[i])) return true;
        mOrigSetVolume = nullptr;
        mHooked.store(false);
    }
    SPL_LOGW("setVolume hook failed");
    return false;
}

void* SoundPhysicsLite::retryThreadMain(void* arg) {
    auto* self = static_cast<SoundPhysicsLite*>(arg);
    for (int i = 0; i < 45 && !self->mStopRetry.load(); ++i) {
        if (self->mHooked.load()) break;
        if (self->installHook()) break;
        sleep(2);
    }
    return nullptr;
}

void SoundPhysicsLite::startRetryThread() {
    if (mRetryStarted || mHooked.load()) return;
    mRetryStarted = true;
    mStopRetry.store(false);
    if (pthread_create(&mRetryThread, nullptr, &retryThreadMain, this) != 0) {
        mRetryStarted = false;
        return;
    }
    pthread_detach(mRetryThread);
}

bool SoundPhysicsLite::load() {
    SPL_LOGE("SPL v0.16 LOAD (distance occlusion MVP, no playSound hook)");
    loadFmod();
    installHook();
    startRetryThread();
    return true;
}

bool SoundPhysicsLite::enable() {
    SPL_LOGE("SPL ENABLE v16");
    loadFmod();
    installHook();
    startRetryThread();
    return true;
}

bool SoundPhysicsLite::disable() {
    mStopRetry.store(true);
    if (mHooked.load() && mSetVolumeTarget && mOrigSetVolume) {
        pl::memory::unhook(mSetVolumeTarget, reinterpret_cast<void*>(&detour_SetVolume));
        mHooked.store(false);
        mOrigSetVolume = nullptr;
    }
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
