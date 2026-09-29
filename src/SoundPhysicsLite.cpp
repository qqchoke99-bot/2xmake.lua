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
    if (mFmod) return true;
    mFmod = dlopen("libfmod.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mFmod) mFmod = dlopen("libfmod.so", RTLD_NOW);
    if (!mFmod) {
        SPL_LOGW("libfmod.so not loaded yet");
        return false;
    }
    auto R = [&](const char* a, const char* b) -> void* {
        void* p = dlsym(mFmod, a);
        return p ? p : (b ? dlsym(mFmod, b) : nullptr);
    };
    mSetLPGain = reinterpret_cast<FN_SetLowPassGain>(
        R("FMOD_Channel_SetLowPassGain", "FMOD5_Channel_SetLowPassGain"));
    mSetReverb = reinterpret_cast<FN_SetReverbProps>(
        R("FMOD_Channel_SetReverbProperties", "FMOD5_Channel_SetReverbProperties"));
    SPL_LOGE("FMOD handle=%p LP=%p REV=%p", mFmod, (void*)mSetLPGain, (void*)mSetReverb);
    return true;
}

bool SoundPhysicsLite::resolveSetVolume() {
    if (mSetVolumeTarget) return true;
    if (!loadFmod()) return false;

    // Prefer C API then C++ mangled — runtime resolve, no file offset
    const char* names[] = {
        "FMOD_Channel_SetVolume",
        "FMOD5_Channel_SetVolume",
        "_ZN4FMOD14ChannelControl9setVolumeEf",
        nullptr,
    };
    void* p = nullptr;
    const char* used = nullptr;
    for (int i = 0; names[i]; ++i) {
        p = dlsym(mFmod, names[i]);
        if (p) {
            used = names[i];
            break;
        }
    }
    if (!p) {
        SPL_LOGW("setVolume symbol not found in libfmod");
        return false;
    }
    if (!addrInAnyMap(reinterpret_cast<uintptr_t>(p))) {
        SPL_LOGW("setVolume %p not in maps", p);
        return false;
    }
    mSetVolumeTarget = p;
    SPL_LOGE("resolved setVolume %s @ %p", used, p);
    return true;
}

void SoundPhysicsLite::applyFx(void* channel) {
    if (!channel) return;
    if (mEnableLowpass && mSetLPGain) {
        mSetLPGain(channel, mLowpassGain);
    }
    if (mEnableReverb && mSetReverb) {
        mSetReverb(channel, mReverbInstance, mReverbWet);
    }
}

int SoundPhysicsLite::detour_SetVolume(void* channel, float volume) {
    auto& self = SoundPhysicsLite::instance();
    int r = 0;
    if (self.mOrigSetVolume) {
        r = self.mOrigSetVolume(channel, volume);
    }
    if (channel) {
        self.applyFx(channel);
        static std::atomic<int> n{0};
        int c = ++n;
        if (c <= 15) {
            SPL_LOGE("FX setVol #%d ch=%p vol=%.2f r=%d", c, channel, volume, r);
        }
    }
    return r;
}

bool SoundPhysicsLite::installHook() {
    if (mHooked.load()) return true;
    if (!resolveSetVolume()) return false;

    void* target = mSetVolumeTarget;
    SPL_LOGE("hook try libfmod setVolume target=%p", target);

    void* orig = nullptr;
    const bool ok = pl::memory::hook(
        target,
        reinterpret_cast<void*>(&SoundPhysicsLite::detour_SetVolume),
        &orig,
        pl::memory::HookPriority::Normal);

    // Some preloader builds set orig even when bool is false — accept non-null orig
    if (!orig) {
        SPL_LOGW("hook failed ok=%d orig=null", (int)ok);
        return false;
    }
    if (!ok) {
        SPL_LOGW("hook ok=0 but orig=%p — using orig anyway", orig);
    }

    mOrigSetVolume = reinterpret_cast<FN_SetVolume>(orig);
    mHooked.store(true);
    SPL_LOGE("HOOKED libfmod setVolume target=%p orig=%p", target, orig);
    return true;
}

void* SoundPhysicsLite::retryThreadMain(void* arg) {
    auto* self = static_cast<SoundPhysicsLite*>(arg);
    SPL_LOGE("retry thread start");
    for (int i = 0; i < 40 && !self->mStopRetry.load(); ++i) {
        if (self->mHooked.load()) break;
        if (self->installHook()) {
            SPL_LOGE("retry ok %d", i + 1);
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
    SPL_LOGE("SPL v0.13-fmod LOAD (hook libfmod setVolume, not pe PLT)");
    loadFmod();
    installHook();
    startRetryThread();
    return true;
}

bool SoundPhysicsLite::enable() {
    SPL_LOGE("SPL ENABLE");
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
        SPL_LOGI("unhooked");
    }
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
