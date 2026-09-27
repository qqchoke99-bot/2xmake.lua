#include "SoundPhysicsLite.hpp"

SoundPhysicsLite& SoundPhysicsLite::instance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite() : mSelf(*ll::mod::NativeMod::current()) {}

static uintptr_t getPeBase() {
    static uintptr_t base = 0;
    if (base) return base;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            if (info->dlpi_name && strstr(info->dlpi_name, "libminecraftpe.so")) {
                *reinterpret_cast<uintptr_t*>(data) = static_cast<uintptr_t>(info->dlpi_addr);
                return 1;
            }
            return 0;
        },
        &base);
    return base;
}

static void* g_orig = nullptr;

static void hook_PlayPath(void* self) {
    using Fn = void (*)(void*);
    if (g_orig) reinterpret_cast<Fn>(g_orig)(self);
    if (!self) return;
    void* channel =
        *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + pe::kOffChannel);
    if (channel) SoundPhysicsLite::instance().onChannelReady(channel);
}

bool SoundPhysicsLite::load() {
    SPL_LOGI("Sound Physics Lite v0.9-reverb load");
    SPL_LOGI("hook 0x%lx  channel+0x%lx  lp=%.2f  reverb_wet=%.2f",
             (unsigned long)pe::kFnPlayPath, (unsigned long)pe::kOffChannel,
             mLowpassGain, mReverbWet);
    loadFmod();
    return true;
}

bool SoundPhysicsLite::enable() { return installHook(); }

bool SoundPhysicsLite::disable() {
    if (mHooked) {
        void* target = reinterpret_cast<void*>(getPeBase() + pe::kFnPlayPath);
        pl::memory::unhook(target, reinterpret_cast<void*>(&hook_PlayPath));
        mHooked = false;
        SPL_LOGI("unhooked");
    }
    return true;
}

bool SoundPhysicsLite::loadFmod() {
    mFmod = dlopen("libfmod.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mFmod) mFmod = dlopen("libfmod.so", RTLD_NOW);
    if (!mFmod) {
        SPL_LOGW("libfmod.so not found");
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

    SPL_LOGI("SetLowPassGain=%p SetReverbProperties=%p",
             (void*)mSetLPGain, (void*)mSetReverb);
    return mSetLPGain != nullptr || mSetReverb != nullptr;
}

void SoundPhysicsLite::onChannelReady(void* channel) {
    if (!mEnabled || !channel) return;

    if (mEnableLowpass && mSetLPGain) {
        mSetLPGain(channel, mLowpassGain);
    }

    // MVP reverb: wet mix on channel reverb instance 0
    // (Needs FMOD system reverb slot to be active; if silent, SetReverb may no-op)
    if (mEnableReverb && mSetReverb) {
        FMOD_RESULT r = mSetReverb(channel, mReverbInstance, mReverbWet);
        if (mEnabled) {
            SPL_LOGI("fx ch=%p lp=%.2f reverb_wet=%.2f inst=%d res=%d",
                     channel, mLowpassGain, mReverbWet, mReverbInstance, (int)r);
        }
    } else if (mEnableLowpass) {
        SPL_LOGI("fx ch=%p lp=%.2f (no reverb API)", channel, mLowpassGain);
    }
}

bool SoundPhysicsLite::installHook() {
    if (mHooked) return true;
    uintptr_t base = getPeBase();
    if (!base) {
        SPL_LOGW("libminecraftpe base not found");
        return false;
    }
    void* target = reinterpret_cast<void*>(base + pe::kFnPlayPath);
    const bool ok = pl::memory::hook(
        target, reinterpret_cast<void*>(&hook_PlayPath), &g_orig,
        pl::memory::HookPriority::Normal);
    if (!ok) {
        SPL_LOGW("hook failed %p", target);
        return false;
    }
    mHooked = true;
    SPL_LOGI("hooked abs=%p orig=%p", target, g_orig);
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
