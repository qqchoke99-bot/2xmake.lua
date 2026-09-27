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

// Entry of function at 0x110CB1D0: x0 = this (object that stores Channel* at +0x78)
static void hook_PlayPath(void* self) {
    using Fn = void (*)(void*);
    if (g_orig) {
        reinterpret_cast<Fn>(g_orig)(self);
    }
    if (!self) return;
    void* channel =
        *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + pe::kOffChannel);
    if (channel) {
        SoundPhysicsLite::instance().onChannelReady(channel);
    }
}

bool SoundPhysicsLite::load() {
    SPL_LOGI("Sound Physics Lite v0.8-MVP load");
    SPL_LOGI("hook body offset 0x%lx channel field +0x%lx", (unsigned long)pe::kFnPlayPath,
             (unsigned long)pe::kOffChannel);
    loadFmod();
    return true;
}

bool SoundPhysicsLite::enable() {
    return installHook();
}

bool SoundPhysicsLite::disable() {
    if (mHooked) {
        void* target = reinterpret_cast<void*>(getPeBase() + pe::kFnPlayPath);
        // pl::memory::unhook(target, detour) per CameraOverhaul / preloader API
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
    mSetLPGain = reinterpret_cast<FN_SetLowPassGain>(
        dlsym(mFmod, "FMOD_Channel_SetLowPassGain"));
    if (!mSetLPGain) {
        mSetLPGain = reinterpret_cast<FN_SetLowPassGain>(
            dlsym(mFmod, "FMOD5_Channel_SetLowPassGain"));
    }
    SPL_LOGI("SetLowPassGain=%p", (void*)mSetLPGain);
    return mSetLPGain != nullptr;
}

void SoundPhysicsLite::onChannelReady(void* channel) {
    if (!mEnabled || !channel || !mSetLPGain) return;
    mSetLPGain(channel, mLowpassGain);
    SPL_LOGI("lowpass ch=%p gain=%.2f", channel, mLowpassGain);
}

bool SoundPhysicsLite::installHook() {
    if (mHooked) return true;
    uintptr_t base = getPeBase();
    if (!base) {
        SPL_LOGW("libminecraftpe.so base not found");
        return false;
    }
    void* target = reinterpret_cast<void*>(base + pe::kFnPlayPath);
    void* detour = reinterpret_cast<void*>(&hook_PlayPath);
    // HookPriority::Normal = 200 (CameraOverhaul used 0xc8)
    const auto pri = pl::memory::HookPriority::Normal;
    const bool ok = pl::memory::hook(target, detour, &g_orig, pri);
    if (!ok) {
        SPL_LOGW("pl::memory::hook failed target=%p", target);
        return false;
    }
    mHooked = true;
    SPL_LOGI("hooked 0x110CB1D0 abs=%p orig=%p", target, g_orig);
    return true;
}

PL_REGISTER_MOD(SoundPhysicsLite, SoundPhysicsLite::instance())
