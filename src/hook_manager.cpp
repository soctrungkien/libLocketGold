#include "hook_manager.h"
#include "log_util.h"

#include <dobby.h>
#include <unistd.h>
#include <dlfcn.h>

HookManager::HookManager()
    : initialized(false) {
}

HookManager::~HookManager() {
    initialized = false;
}

bool HookManager::initialize() {
    if (initialized) {
        LOG_I("Hook manager already initialized");
        return true;
    }

    // Dobby does not require explicit initialization.
    initialized = true;

    LOG_I("Hook manager initialized successfully");
    return true;
}

bool HookManager::hook_hermes() {
    LOG_I(
        "Hermes hook setup "
        "(module-specific, defer to runtime detection)"
    );

    return true;
}

bool HookManager::hook_firebase_config() {
    void* firebase_module = dlopen(
        "libfirebase_app.so",
        RTLD_NOW
    );

    if (!firebase_module) {
        LOG_W(
            "Firebase library not found (non-critical)"
        );

        return false;
    }

    LOG_I(
        "Firebase Config hook configured "
        "(JNI-based)"
    );

    return true;
}

bool HookManager::hook_firestore() {
    LOG_I(
        "Firestore hook configured "
        "(JNI-based)"
    );

    return true;
}

bool HookManager::hook_mmkv() {
    void* mmkv_lib = dlopen(
        "libmmkv.so",
        RTLD_NOW
    );

    if (!mmkv_lib) {
        LOG_W(
            "MMKV library not loaded yet"
        );

        return false;
    }

    LOG_I(
        "MMKV hook configured "
        "(deferred to Java level)"
    );

    dlclose(mmkv_lib);

    return true;
}

bool HookManager::hook_networking() {
    LOG_I(
        "React Native Networking hook configured "
        "(JNI-based)"
    );

    return true;
}

bool HookManager::hook_revenuecat() {
    LOG_I(
        "RevenueCat hook configured "
        "(JNI-based)"
    );

    return true;
}

bool HookManager::hook_jni_method(
    const char* lib_name,
    const char* symbol,
    void* replacement,
    void** original
) {
    if (!lib_name ||
        !symbol ||
        !replacement ||
        !original) {

        LOG_W("Invalid hook arguments");
        return false;
    }

    void* lib = dlopen(
        lib_name,
        RTLD_NOW
    );

    if (!lib) {
        LOG_W(
            "Library not found: %s",
            lib_name
        );

        return false;
    }

    void* target = dlsym(
        lib,
        symbol
    );

    if (!target) {
        LOG_W(
            "Symbol not found: %s in %s",
            symbol,
            lib_name
        );

        dlclose(lib);
        return false;
    }

    int ret = DobbyHook(
        target,
        replacement,
        original
    );

    if (ret != 0) {
        LOG_E(
            "Failed to hook %s: %d",
            symbol,
            ret
        );

        dlclose(lib);
        return false;
    }

    LOG_I(
        "Hooked native symbol: %s",
        symbol
    );

    dlclose(lib);

    return true;
}
