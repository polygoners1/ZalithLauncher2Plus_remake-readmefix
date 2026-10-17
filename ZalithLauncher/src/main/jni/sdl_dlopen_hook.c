// SDL JNI_OnLoad isolation for the embedded game JVM.
// Reference: official ZalithLauncher2 + Amethyst e4c79084d
//
// Critical: only ONE libSDL3 mapping must exist. Using RTLD_LOCAL|RTLD_NOW
// before System.loadLibrary can create a second copy — OnLoad runs on one
// instance while SDL_Init uses the other → SIGSEGV in Android backend.
//
// Strategy (official-compatible):
// 1) Prefer RTLD_NOLOAD to discover the System.loadLibrary handle.
// 2) Replace JNI_OnLoad with isolated wrapper.
// 3) isolated runs real OnLoad ONLY once when vm == dalvikJavaVMPtr.
// 4) Hotspot lookups get no-op OnLoad.

#include <dlfcn.h>
#include <jni.h>
#include <bytehook.h>
#include <stdlib.h>
#include <string.h>

#include "environ/environ.h"
#include "logger/logger.h"
#include "sdl_hook.h"
#include "utils.h"

typedef void *(*dlsym_func_t)(void *handle, const char *symbol);
typedef jint (*jni_on_load_func_t)(JavaVM *vm, void *reserved);

typedef bytehook_stub_t (*bytehook_hook_all_fn)(const char *callee_path_name, const char *symbol,
                                                  void *new_func, bytehook_hooked_t hooked, void *hooked_arg);

static void *sdlHandle;
static jni_on_load_func_t originalSdlJniOnLoad;
static volatile int sSdlJniOnLoadDone = 0;

static jint isolatedSdlJniOnLoad(JavaVM *vm, void *reserved) {
    if (sSdlJniOnLoadDone) {
        return JNI_VERSION_1_4;
    }
    if (originalSdlJniOnLoad != NULL && pojav_environ != NULL
            && pojav_environ->dalvikJavaVMPtr == vm) {
        sSdlJniOnLoadDone = 1;
        LOG_TO_I("SDL_Hook: running real SDL JNI_OnLoad ONCE on Dalvik/ART vm=%p", (void *) vm);
        jint ver = originalSdlJniOnLoad(vm, reserved);
        LOG_TO_I("SDL_Hook: real SDL JNI_OnLoad finished, version=0x%x", (unsigned) ver);
        return ver;
    }
    LOG_TO_I("SDL_Hook: skip SDL JNI_OnLoad for non-Dalvik vm=%p (dalvik=%p)",
             (void *) vm,
             pojav_environ ? (void *) pojav_environ->dalvikJavaVMPtr : NULL);
    return JNI_VERSION_1_4;
}

static void *resolveSdlHandle(void) {
    if (sdlHandle != NULL) {
        return sdlHandle;
    }
    /* Prefer the copy already loaded by System.loadLibrary — do NOT create a second mapping. */
    void *h = dlopen("libSDL3.so", RTLD_NOLOAD);
    if (h == NULL) {
        /* Fallback only if not loaded yet (should be rare during dlsym of JNI_OnLoad). */
        h = dlopen("libSDL3.so", RTLD_NOW);
    }
    sdlHandle = h;
    return sdlHandle;
}

static int isSdlJniOnLoadSymbol(void *addr) {
    if (addr == NULL) return 0;
    Dl_info info;
    if (dladdr(addr, &info) == 0 || info.dli_fname == NULL) return 0;
    return strstr(info.dli_fname, "libSDL3.so") != NULL;
}

static void *customDlsym(void *handle, const char *symbol) {
    void *result = BYTEHOOK_CALL_PREV(customDlsym, dlsym_func_t, handle, symbol);
    BYTEHOOK_POP_STACK();

    void *sdl = resolveSdlHandle();

    if (symbol != NULL && strcmp(symbol, "JNI_OnLoad") == 0 && result != NULL) {
        int fromSdl = (sdl != NULL && handle == sdl) || isSdlJniOnLoadSymbol(result);
        if (fromSdl) {
            originalSdlJniOnLoad = (jni_on_load_func_t) result;
            if (!sSdlJniOnLoadDone) {
                LOG_TO_I("SDL_Hook: replacing libSDL3 JNI_OnLoad %p with isolated (handle=%p sdl=%p)",
                         result, handle, sdl);
            }
            result = (void *) isolatedSdlJniOnLoad;
        }
    }

    /* LWJGL org.lwjgl.sdl resolves SDL functions via dlsym — proxy through sdl_hook. */
    if (result != NULL && symbol != NULL) {
        void *proxy = sdlDlsymProxy(symbol, result);
        if (proxy != NULL) {
            LOG_TO_I("SDL_Hook: dlsym proxy for %s", symbol);
            result = proxy;
        }
    }
    return result;
}

void create_sdl_dlopen_hooks(bytehook_hook_all_fn hookAll) {
    if (hookAll == NULL) return;
    hookAll(NULL, "dlsym", (void *) customDlsym, NULL, NULL);
    /* ART linker may resolve JNI_OnLoad via __loader_dlsym */
    hookAll(NULL, "__loader_dlsym", (void *) customDlsym, NULL, NULL);
}
