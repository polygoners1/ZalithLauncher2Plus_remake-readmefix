// SDL JNI_OnLoad isolation for the embedded game JVM.
// Reference: https://github.com/AngelAuraMC/Amethyst-Android/commit/e4c79084d
//
// ZalithLauncher2Plus (device ZTE/Unisoc ART):
// System.loadLibrary("SDL3") → ART LoadNativeLibrary → real JNI_OnLoad SIGSEGVs inside
// art::Thread::ThrowNewWrappedException (FindClass failure with bad ClassLoader).
// Official isolation only replaces JNI_OnLoad when handle==sdlHandle; ART often passes a
// different handle and/or resolves via __loader_dlsym, so the real OnLoad still runs.
// Fix: identify libSDL3 JNI_OnLoad via dladdr, replace always, and NEVER call original
// OnLoad on this tree (library still loads; C SDL_Init continues via sdl_hook proxies).

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

static jint isolatedSdlJniOnLoad(JavaVM *vm, void *reserved) {
    (void) reserved;
    // Do NOT call originalSdlJniOnLoad — it SIGSEGVs on affected ART devices during FindClass.
    LOG_TO_I("SDL_Hook: isolated JNI_OnLoad skip (vm=%p dalvik=%p original=%p)",
             (void *) vm,
             pojav_environ ? (void *) pojav_environ->dalvikJavaVMPtr : NULL,
             (void *) originalSdlJniOnLoad);
    return JNI_VERSION_1_4;
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

    if (symbol != NULL && strcmp(symbol, "JNI_OnLoad") == 0 && result != NULL) {
        if (isSdlJniOnLoadSymbol(result)) {
            originalSdlJniOnLoad = (jni_on_load_func_t) result;
            LOG_TO_I("SDL_Hook: replacing libSDL3 JNI_OnLoad %p with isolated", result);
            result = (void *) isolatedSdlJniOnLoad;
        }
    }

    if (sdlHandle == NULL) {
        sdlHandle = dlopen("libSDL3.so", RTLD_LOCAL | RTLD_NOW);
    }

    // LWJGL org.lwjgl.sdl bindings resolve SDL function pointers via dlsym; proxy them.
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
    // ART/linker on many devices resolves JNI_OnLoad via __loader_dlsym, not dlsym.
    hookAll(NULL, "__loader_dlsym", (void *) customDlsym, NULL, NULL);
}
