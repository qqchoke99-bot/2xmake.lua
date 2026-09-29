#include "SoundPhysicsLite.hpp"

SoundPhysicsLite& SoundPhysicsLite::instance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite() : mSelf(*ll::mod::NativeMod::current()) {}

uintptr_t SoundPhysicsLite::peBias() {
    uintptr_t bias = 0;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            if (info->dlpi_name && strstr(info->dlpi_name, "libminecraftpe.so")) {
                *reinterpret_cast<uintptr_t*>(data) = (uintptr_t)info->dlpi_addr;
                return 1;
            }
            return 0;
        },
        &bias);
    return bias;
}

bool SoundPhysicsLite::targetInPeMaps(uintptr_t addr) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libminecraftpe.so")) continue;
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
    if (mSetLPGain || mSetReverb) return true;
    mFmod = dlopen("libfmod.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mFmod) mFmod = dlopen("libfmod.so", RTLD_NOW);
    if (!mFmod) {
        SPL_LOGW("libfmod.so missing");
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
    SPL_LOGE("FMOD LP=%p REV=%p", (void*)mSetLPGain, (void*)mSetReverb);
    return mSetLPGain || mSetReverb;
}

void SoundPhysicsLite::applyFx(void* channel) {
    if (!channel) return;
    loadFmod();
    if (mEnableLowpass && mSetLPGain) {
        mSetLPGain(channel, mLowpassGain);
    }
    if (mEnableReverb && mSetReverb) {
        mSetReverb(channel, mReverbInstance, mReverbWet);
    }
}

int SoundPhysicsLite::detour_SetVolume(void* channel, float volume) {
    auto& self = SoundPhysicsLite::instance();
    FN_SetVolume orig = self.mOrigSetVolume;
    // ALWAYS call original first so sound is not silenced
    int r = 0;
    if (orig) {
        r = orig(channel, volume);
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

    uintptr_t bias = peBias();
    if (!bias) {
        SPL_LOGW("pe bias not ready");
        return false;
    }

    uintptr_t target = bias + pe::kPltSetVolume;
    if (!targetInPeMaps(target)) {
        SPL_LOGW("setVolume PLT %p not in pe maps (bias=%p)", (void*)target, (void*)bias);
        return false;
    }

    SPL_LOGE("hook try setVolume PLT target=%p", (void*)target);

    void* orig = nullptr;
    const bool ok = pl::memory::hook(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&SoundPhysicsLite::detour_SetVolume),
        &orig,
        pl::memory::HookPriority::Normal);

    if (!ok || !orig) {
        SPL_LOGW("hook failed ok=%d orig=%p", (int)ok, orig);
        return false;
    }

    mOrigSetVolume = reinterpret_cast<FN_SetVolume>(orig);
    mHooked.store(true);
    SPL_LOGE("HOOKED setVolume PLT target=%p orig=%p", (void*)target, orig);
    return true;
}

void* SoundPhysicsLite::retryThreadMain(void* arg) {
    auto* self = static_cast<SoundPhysicsLite*>(arg);
    SPL_LOGE("retry thread start");
    for (int i = 0; i < 40 && !self->mStopRetry.load(); ++i) {
        if (self->mHooked.load()) break;
        self->loadFmod();
        if (self->installHook()) {
            SPL_LOGE("retry ok %d", i + 1);
            break;
        }
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
    SPL_LOGE("SPL v0.12-setVolume LOAD (sound-first)");
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
    if (mHooked.load() && mOrigSetVolume) {
        uintptr_t bias = peBias();
        if (bias) {
            void* target = reinterpret_cast<void*>(bias + pe::kPltSetVolume);
            pl::memory::unhook(target, reinterpret_cast<void*>(&detour_SetVolume));
        }
        mHooked.store(false);
        mOrigSetVolume = nullptr;
        SPL_LOGI("unhooked");
    }
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
