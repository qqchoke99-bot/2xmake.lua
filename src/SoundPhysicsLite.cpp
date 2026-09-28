#include "SoundPhysicsLite.hpp"
#include <unistd.h>

SoundPhysicsLite& SoundPhysicsLite::instance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite() : mSelf(*ll::mod::NativeMod::current()) {}

static uintptr_t scanPeBase() {
    uintptr_t base = 0;

    // 1) Already loaded?
    void* h = dlopen("libminecraftpe.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) {
        // bias via dl_iterate still needed; keep handle
        dlclose(h);
    }

    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            if (!info->dlpi_name) return 0;
            const char* n = info->dlpi_name;
            // path may be full path; match substring
            if (strstr(n, "libminecraftpe.so") || strstr(n, "minecraftpe")) {
                *reinterpret_cast<uintptr_t*>(data) = static_cast<uintptr_t>(info->dlpi_addr);
                SPL_LOGI("found pe name='%s' base=%p", n, (void*)info->dlpi_addr);
                return 1;
            }
            return 0;
        },
        &base);

    return base;
}

static void* g_orig = nullptr;
static std::atomic<int> g_fxCount{0};

static void hook_PlayPath(void* self) {
    using Fn = void (*)(void*);
    if (g_orig) reinterpret_cast<Fn>(g_orig)(self);
    if (!self) return;
    void* channel =
        *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + pe::kOffChannel);
    if (channel) SoundPhysicsLite::instance().onChannelReady(channel);
}

bool SoundPhysicsLite::load() {
    SPL_LOGE("SPL v0.10-retry LOAD (method C)");
    SPL_LOGI("target offset=0x%lx channel+0x%lx lp=%.2f wet=%.2f",
             (unsigned long)pe::kFnPlayPath, (unsigned long)pe::kOffChannel,
             mLowpassGain, mReverbWet);
    loadFmod();
    // Try once early; pe may not exist yet
    installHook();
    startRetryThread();
    return true;
}

bool SoundPhysicsLite::enable() {
    SPL_LOGE("SPL ENABLE");
    loadFmod();
    if (!installHook()) {
        SPL_LOGW("enable: hook not ready, retry thread will keep trying");
    }
    startRetryThread();
    return true; // don't fail mod load if pe not ready yet
}

bool SoundPhysicsLite::disable() {
    mStopRetry.store(true);
    if (mHooked.load()) {
        uintptr_t base = scanPeBase();
        if (base) {
            void* target = reinterpret_cast<void*>(base + pe::kFnPlayPath);
            pl::memory::unhook(target, reinterpret_cast<void*>(&hook_PlayPath));
            SPL_LOGI("unhooked");
        }
        mHooked.store(false);
    }
    return true;
}

bool SoundPhysicsLite::loadFmod() {
    if (mSetLPGain || mSetReverb) return true;

    mFmod = dlopen("libfmod.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mFmod) mFmod = dlopen("libfmod.so", RTLD_NOW);
    if (!mFmod) {
        SPL_LOGW("libfmod.so not found (will retry)");
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

    SPL_LOGE("FMOD ready LP=%p REV=%p", (void*)mSetLPGain, (void*)mSetReverb);
    return mSetLPGain != nullptr || mSetReverb != nullptr;
}

void SoundPhysicsLite::onChannelReady(void* channel) {
    if (!mEnabled || !channel) return;
    loadFmod();

    if (mEnableLowpass && mSetLPGain) {
        mSetLPGain(channel, mLowpassGain);
    }
    if (mEnableReverb && mSetReverb) {
        mSetReverb(channel, mReverbInstance, mReverbWet);
    }

    int n = ++g_fxCount;
    // Log first 30 hits so it's obvious when effect path runs
    if (n <= 30) {
        SPL_LOGE("FX #%d ch=%p lp=%.2f wet=%.2f", n, channel, mLowpassGain, mReverbWet);
    }
}

bool SoundPhysicsLite::installHook() {
    if (mHooked.load()) return true;

    uintptr_t base = scanPeBase();
    if (!base) {
        SPL_LOGW("pe base not found yet");
        return false;
    }

    void* target = reinterpret_cast<void*>(base + pe::kFnPlayPath);
    SPL_LOGI("try hook target=%p (base=%p + 0x%lx)", target, (void*)base,
             (unsigned long)pe::kFnPlayPath);

    void* orig = nullptr;
    const bool ok = pl::memory::hook(
        target, reinterpret_cast<void*>(&hook_PlayPath), &orig,
        pl::memory::HookPriority::Normal);
    if (!ok || !orig) {
        // some builds return bool; orig may still be set
        if (!ok) {
            SPL_LOGW("pl::memory::hook returned false target=%p", target);
            return false;
        }
    }
    g_orig = orig;
    mHooked.store(true);
    SPL_LOGE("HOOKED OK target=%p orig=%p", target, g_orig);
    return true;
}

void* SoundPhysicsLite::retryThreadMain(void* arg) {
    auto* self = static_cast<SoundPhysicsLite*>(arg);
    SPL_LOGE("retry thread start");
    // ~60s of retries: every 2s
    for (int i = 0; i < 30 && !self->mStopRetry.load(); ++i) {
        if (self->mHooked.load()) break;
        self->loadFmod();
        if (self->installHook()) {
            SPL_LOGE("retry success at attempt %d", i + 1);
            break;
        }
        sleep(2);
    }
    if (!self->mHooked.load()) {
        SPL_LOGW("retry gave up — pe never found or hook failed");
    }
    SPL_LOGI("retry thread end");
    return nullptr;
}

void SoundPhysicsLite::startRetryThread() {
    if (mRetryStarted || mHooked.load()) return;
    mRetryStarted = true;
    mStopRetry.store(false);
    if (pthread_create(&mRetryThread, nullptr, &SoundPhysicsLite::retryThreadMain, this) != 0) {
        SPL_LOGW("pthread_create failed");
        mRetryStarted = false;
        return;
    }
    pthread_detach(mRetryThread);
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
