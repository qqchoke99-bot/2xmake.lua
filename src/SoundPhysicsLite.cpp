#include "SoundPhysicsLite.hpp"

SoundPhysicsLite& SoundPhysicsLite::instance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite() : mSelf(*ll::mod::NativeMod::current()) {}

bool SoundPhysicsLite::findPeMap(PeMap& out) {
    out = {};
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) {
        SPL_LOGW("cannot open /proc/self/maps");
        return false;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // need executable mapping of libminecraftpe.so
        if (!strstr(line, "libminecraftpe.so")) continue;
        if (!strstr(line, "r-x") && !strstr(line, "r-xp")) continue;
        uintptr_t a = 0, b = 0;
        if (sscanf(line, "%lx-%lx", &a, &b) == 2 && a && b > a) {
            out.start = a;
            out.end = b;
            fclose(f);
            SPL_LOGE("pe r-x map %p-%p", (void*)a, (void*)b);
            return true;
        }
    }
    fclose(f);
    return false;
}

bool SoundPhysicsLite::addrInMap(uintptr_t addr, const PeMap& m) {
    return m.start && addr >= m.start && addr < m.end;
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

int SoundPhysicsLite::detour_PlaySound(void* sys, void* sound, void* group, int paused, void** outChannel) {
    auto& self = SoundPhysicsLite::instance();
    FN_PlaySound orig = self.mOrigPlaySound;
    if (!orig) return 0;
    // Call original once
    int r = orig(sys, sound, group, paused, outChannel);
    if (r == 0 && outChannel && *outChannel) {
        self.applyFx(*outChannel);
        static std::atomic<int> n{0};
        int c = ++n;
        if (c <= 20) {
            SPL_LOGE("FX #%d ch=%p res=%d", c, *outChannel, r);
        }
    }
    return r;
}

bool SoundPhysicsLite::installHook() {
    if (mHooked.load()) return true;

    PeMap map{};
    if (!findPeMap(map)) {
        SPL_LOGW("pe map not ready");
        return false;
    }

    // Runtime VA = map start is NOT always file base if first mapping is not offset 0.
    // Prefer dl_iterate_phdr load bias for correct base+RVA.
    uintptr_t bias = 0;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            if (!info->dlpi_name) return 0;
            if (strstr(info->dlpi_name, "libminecraftpe.so")) {
                *reinterpret_cast<uintptr_t*>(data) = (uintptr_t)info->dlpi_addr;
                return 1;
            }
            return 0;
        },
        &bias);

    if (!bias) {
        SPL_LOGW("pe bias 0");
        return false;
    }

    uintptr_t target = bias + pe::kPltPlaySound;
    if (!addrInMap(target, map)) {
        // PLT may sit in a different segment; scan maps for any pe segment containing target
        FILE* f = fopen("/proc/self/maps", "r");
        bool ok = false;
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (!strstr(line, "libminecraftpe.so")) continue;
                uintptr_t a = 0, b = 0;
                if (sscanf(line, "%lx-%lx", &a, &b) == 2 && target >= a && target < b) {
                    ok = true;
                    break;
                }
            }
            fclose(f);
        }
        if (!ok) {
            SPL_LOGW("target %p not in any pe mapping (bias=%p)", (void*)target, (void*)bias);
            return false;
        }
    }

    SPL_LOGE("hook try target=%p bias=%p plt_off=0x%lx", (void*)target, (void*)bias,
             (unsigned long)pe::kPltPlaySound);

    void* orig = nullptr;
    // CRITICAL: never call hook on invalid address — that was the crash
    const bool ok = pl::memory::hook(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&SoundPhysicsLite::detour_PlaySound),
        &orig,
        pl::memory::HookPriority::Normal);

    if (!ok || !orig) {
        SPL_LOGW("hook failed ok=%d orig=%p", (int)ok, orig);
        return false;
    }

    mOrigPlaySound = reinterpret_cast<FN_PlaySound>(orig);
    mHooked.store(true);
    SPL_LOGE("HOOKED PLT playSound target=%p orig=%p", (void*)target, orig);
    return true;
}

void* SoundPhysicsLite::retryThreadMain(void* arg) {
    auto* self = static_cast<SoundPhysicsLite*>(arg);
    SPL_LOGE("retry thread start (safe)");
    for (int i = 0; i < 40 && !self->mStopRetry.load(); ++i) {
        if (self->mHooked.load()) break;
        self->loadFmod();
        if (self->installHook()) {
            SPL_LOGE("retry ok attempt %d", i + 1);
            break;
        }
        sleep(2);
    }
    if (!self->mHooked.load()) SPL_LOGW("retry gave up (no crash attempted on bad addr)");
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
    SPL_LOGE("SPL v0.11-safe LOAD (PLT playSound, no body hook)");
    loadFmod();
    // Do not force hook in load if pe missing — retry thread handles it
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
    if (mHooked.load() && mOrigPlaySound) {
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
        if (bias) {
            void* target = reinterpret_cast<void*>(bias + pe::kPltPlaySound);
            pl::memory::unhook(target, reinterpret_cast<void*>(&detour_PlaySound));
        }
        mHooked.store(false);
        mOrigPlaySound = nullptr;
        SPL_LOGI("unhooked");
    }
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
