/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// Throttles the APPListFPS thread of the stock mtkpower_applist service.
//
// The service dlopen()s lib_power_applist.so and runs
//     while (!_checkFPSupdate(info)) {}
// on a dedicated thread, relying on the FPSGO GET_FPS ioctl to block until the
// foreground app's frame rate changes. FPSGO only learns about frames from
// MediaTek's SurfaceFlinger hooks, which this ROM does not have, so the ioctl
// returns at once (pid -1, fps 0) and the thread spins a whole CPU core while
// flooding logd with about 2000 lines a second.
//
// The service binary is patched to dlopen this library instead. It loads the
// real library and forwards the four symbols the service looks up, pausing
// _checkFPSupdate whenever there is no update.

#define LOG_TAG "applist_shim"

#include <dlfcn.h>
#include <log/log.h>
#include <unistd.h>

#include <cstdint>

namespace {

constexpr char kRealLibrary[] = "lib_power_applist.so";
constexpr useconds_t kNoUpdateDelayUs = 1000 * 1000;

// Every forwarded symbol takes integer or pointer arguments only, so passing the
// eight AArch64 argument registers through unchanged preserves any signature.
using Forward = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t);

void* realLibrary() {
    static void* handle = [] {
        void* h = dlopen(kRealLibrary, RTLD_NOW);
        if (!h) ALOGE("dlopen %s: %s", kRealLibrary, dlerror());
        return h;
    }();
    return handle;
}

Forward resolve(const char* name) {
    void* handle = realLibrary();
    if (!handle) return nullptr;
    auto fn = reinterpret_cast<Forward>(dlsym(handle, name));
    if (!fn) ALOGE("dlsym %s: %s", name, dlerror());
    return fn;
}

}  // namespace

#define FORWARD(name)                                                                       \
    extern "C" uint64_t name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, \
                             uint64_t a5, uint64_t a6, uint64_t a7) {                        \
        static Forward fn = resolve(#name);                                                 \
        return fn ? fn(a0, a1, a2, a3, a4, a5, a6, a7) : 0;                                 \
    }

FORWARD(parseAPPlist)
FORWARD(notifyAPPstate)
FORWARD(getForegroundAPPInfo)

extern "C" int _checkFPSupdate(void* info) {
    using CheckFn = int (*)(void*);
    static CheckFn fn = reinterpret_cast<CheckFn>(resolve("_checkFPSupdate"));
    if (!fn) {
        usleep(kNoUpdateDelayUs);
        return 0;
    }
    int updated = fn(info);
    if (!updated) usleep(kNoUpdateDelayUs);
    return updated;
}
