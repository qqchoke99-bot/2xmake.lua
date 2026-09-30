#include "SoundPhysicsLite.hpp"

SoundPhysicsLite& SoundPhysicsLite::instance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite() : mSelf(*ll::mod::NativeMod::current()) {}

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
    if (mFmod && (mSetLPGain || mSetReverb || mSetOcclusion)) return true;
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
    SPL_LOGE("FMOD handle=%p LP=%p REV=%p OCC=%p", mFmod, (void*)mSetLPGain,
             (void*)mSetReverb, (void*)mSetOcclusion);
    return true;
}

void SoundPhysicsLite::applyFx(void* channel, float volume) {
    if (!channel) return;
    // Skip silent / fully faded channels
    if (volume <= 0.001f) return;

    if (mEnableLowpass && mSetLPGain) {
        mSetLPGain(channel, mLowpassGain);
    }
    if (mEnableReverb && mSetReverb) {
        mSetReverb(channel, mReverbInstance, mReverbWet);
        // Try a couple more instances — some builds only wet on non-zero slot
        mSetReverb(channel, 1, mReverbWet * 0.7f);
        mSetReverb(channel, 2, mReverbWet * 0.4f);
    }
    if (mEnableOcclusion && mSetOcclusion) {
        mSetOcclusion(channel, mOcclusionDirect, mOcclusionReverb);
    }
}

int SoundPhysicsLite::detour_SetVolume(void* channel, float volume) {
    auto& self = SoundPhysicsLite::instance();
    int r = 0;
    if (self.mOrigSetVolume) {
        r = self.mOrigSetVolume(channel, volume);
    }
    if (channel) {
        self.applyFx(channel, volume);
        static std::atomic<int> n{0};
        int c = ++n;
        if (c <= 40 && volume > 0.001f) {
            SPL_LOGE("FX HEAVY #%d ch=%p vol=%.3f r=%d lp=%.2f wet=%.2f", c, channel, volume, r,
                     self.mLowpassGain, self.mReverbWet);
        }
    }
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

    const char* names[] = {
        "_ZN4FMOD14ChannelControl9setVolumeEf",
        "FMOD_Channel_SetVolume",
        "FMOD5_Channel_SetVolume",
        "_ZN4FMOD7Channel9setVolumeEf",
        nullptr,
    };

    for (int i = 0; names[i]; ++i) {
        void* p = dlsym(mFmod, names[i]);
        if (!p) {
            SPL_LOGI("skip missing %s", names[i]);
            continue;
        }
        if (!addrInAnyMap(reinterpret_cast<uintptr_t>(p))) {
            SPL_LOGW("skip not-in-maps %s %p", names[i], p);
            continue;
        }
        if (tryHookAt(p, names[i])) {
            return true;
        }
        mOrigSetVolume = nullptr;
        mHooked.store(false);
    }

    SPL_LOGW("all setVolume hook candidates failed");
    return false;
}

void* SoundPhysicsLite::retryThreadMain(void* arg) {
    auto* self = static_cast<SoundPhysicsLite*>(arg);
    SPL_LOGE("retry thread start");
    for (int i = 0; i < 45 && !self->mStopRetry.load(); ++i) {
        if (self->mHooked.load()) break;
        if (self->installHook()) {
            SPL_LOGE("retry ok attempt %d", i + 1);
            break;
        }
        sleep(2);
    }
    if (!self->mHooked.load()) SPL_LOGW("retry gave up");
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
    SPL_LOGE("SPL v0.15-HEAVY LOAD lp=0.35 wet=0.85 occ=0.45");
    loadFmod();
    installHook();
    startRetryThread();
    return true;
}

bool SoundPhysicsLite::enable() {
    SPL_LOGE("SPL ENABLE HEAVY");
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
        SPL_LOGI("unhooked %s", mSetVolumeName ? mSetVolumeName : "?");
    }
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
