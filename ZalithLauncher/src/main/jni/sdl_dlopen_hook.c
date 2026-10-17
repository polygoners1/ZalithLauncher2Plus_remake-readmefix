// SDL JNI_OnLoad isolation for the embedded game JVM.
// Reference: https://github.com/AngelAuraMC/Amethyst-Android/commit/e4c79084d
//
// Official behavior: when ART/Dalvik System.loadLibrary runs JNI_OnLoad, call the real
// OnLoad with dalvikJavaVMPtr so Android backend + RegisterNatives initialize.
// When the embedded Hotspot game JVM looks up JNI_OnLoad, return a no-op so Hotspot
// never runs SDL's FindClass path (SIGSEGV on some ART devices).
// Extra: identify libSDL3 via dladdr and also hook __loader_dlsym (ART linker).

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
    /* ART/System.loadLibrary may resolve JNI_OnLoad many times; original OnLoad
     * itself can re-enter via dlsym. Run real OnLoad exactly once on Dalvik. */
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
        if (isSdlJniOnLoadSymbol(result) || (sdlHandle != NULL && handle == sdlHandle)) {
            originalSdlJniOnLoad = (jni_on_load_func_t) result;
            if (!sSdlJniOnLoadDone) LOG_TO_I("SDL_Hook: replacing libSDL3 JNI_OnLoad %p with isolated", result);
            result = (void *) isolatedSdlJniOnLoad;
        }
    }

    if (sdlHandle == NULL) {
        sdlHandle = dlopen("libSDL3.so", RTLD_LOCAL | RTLD_NOW);
    }

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
    hookAll(NULL, "__loader_dlsym", (void *) customDlsym, NULL, NULL);
}
