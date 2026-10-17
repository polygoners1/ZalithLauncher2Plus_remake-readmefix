// SDL JNI_OnLoad isolation for the embedded game JVM.
// Reference: https://github.com/AngelAuraMC/Amethyst-Android/commit/e4c79084d

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
    /* Run real OnLoad only once on Dalvik — re-entry via dlsym during OnLoad is common. */
    if (sSdlJniOnLoadDone) {
        return JNI_VERSION_1_4;
    }
    if (originalSdlJniOnLoad != NULL && pojav_environ != NULL
            && pojav_environ->dalvikJavaVMPtr == vm) {
        sSdlJniOnLoadDone = 1;
        LOG_TO_I("SDL_Hook: real JNI_OnLoad ONCE on Dalvik vm=%p", (void *) vm);
        jint ver = originalSdlJniOnLoad(vm, reserved);
        LOG_TO_I("SDL_Hook: JNI_OnLoad done version=0x%x", (unsigned) ver);
        return ver;
    }
    return JNI_VERSION_1_4;
}

static void *customDlsym(void *handle, const char *symbol) {
    void *result = BYTEHOOK_CALL_PREV(customDlsym, dlsym_func_t, handle, symbol);
    BYTEHOOK_POP_STACK();
    if (sdlHandle == NULL) {
        {
            void *noload = dlopen("libSDL3.so", RTLD_NOLOAD);
            sdlHandle = noload ? noload : dlopen("libSDL3.so", RTLD_NOW);
        }
    }
    if (sdlHandle != NULL && handle == sdlHandle && symbol != NULL && strcmp(symbol, "JNI_OnLoad") == 0) {
        originalSdlJniOnLoad = (jni_on_load_func_t) result;
        result = (void *) isolatedSdlJniOnLoad;
    }
    // LWJGL 的 org.lwjgl.sdl 绑定等消费方经 dlsym 解析 SDL 函数指针后直接调用，
    // hook_all 的 GOT 导入补丁拦截不到；在解析出口统一换成 sdl_hook 的 dlsym 层代理
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
}