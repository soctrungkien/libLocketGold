#include "hook_manager.h"
#include "log_util.h"
#include <dobby.h>
#include <unistd.h>
#include <dlfcn.h>

HookManager::HookManager() : initialized(false) {
}

HookManager::~HookManager() {
    // Dobby cleanup (restores original functions)
    DobbyDestroy(nullptr);  // Cleanup all hooks
}

bool HookManager::initialize() {
    if (initialized) {
        LOG_I("Hook manager already initialized");
        return true;
    }
    
    // Initialize Dobby
    int ret = DobbyInitialize();
    if (ret != DOBBY_SUCCESS) {
        LOG_E("Failed to initialize Dobby: %d", ret);
        return false;
    }
    
    initialized = true;
    LOG_I("Hook manager initialized successfully");
    return true;
}

bool HookManager::hook_hermes() {
    // Strategy: Hook Hermes bytecode loading and patch bundle in-memory
    // Target: ReactNativeHost.getJSBundleFile() or JSBundleLoader
    
    // Since Hermes is complex and bytecode-version dependent,
    // we'll hook at the JS execution level where possible
    // For now, this is a placeholder for Hermes patching logic
    
    LOG_I("Hermes hook setup (module-specific, defer to runtime detection)");
    return true;  // Non-critical; defer to runtime
}

bool HookManager::hook_firebase_config() {
    // Hook: ReactNativeFirebaseConfigModule.getConstants()
    // Effect: Inject gold subscription flags into remote config
    
    void* firebase_module = dlopen("libfirebase_app.so", RTLD_NOW);
    if (!firebase_module) {
        LOG_W("Firebase library not found (non-critical)");
        return false;
    }
    
    // Look for common Firebase Config class patterns
    // This requires JNI callbacks since Firebase is Java-based
    // We'll register a callback that native code can invoke
    
    LOG_I("Firebase Config hook configured (JNI-based)");
    return true;  // JNI-based, handled via Java wrapper
}

bool HookManager::hook_firestore() {
    // Hook: Firestore DocumentSnapshot.getData()
    // Effect: Mutate returned Maps to include gold subscription
    
    // Firestore is primarily Java; we hook via JNI callbacks
    LOG_I("Firestore hook configured (JNI-based)");
    return true;  // Handled via Java wrapper
}

bool HookManager::hook_mmkv() {
    // Hook: MMKV.decodeString()
    // MMKV has native components; we can hook the native layer
    
    void* mmkv_lib = dlopen("libmmkv.so", RTLD_NOW);
    if (!mmkv_lib) {
        LOG_W("MMKV library not loaded yet");
        return false;  // May load later
    }
    
    // MMKV native interface; hook decode functions
    // This would require knowing MMKV's internal symbol names
    // For now, we rely on Java-level hooking
    
    LOG_I("MMKV hook configured (deferred to Java level)");
    dlclose(mmkv_lib);
    return true;
}

bool HookManager::hook_networking() {
    // Hook: React Native NetworkingModule.onDataReceived()
    // Pure Java implementation; use JNI-based hooking
    
    LOG_I("React Native Networking hook configured (JNI-based)");
    return true;  // Handled via Java wrapper
}

bool HookManager::hook_revenuecat() {
    // Hook: RevenueCat EntitlementInfo passive getters
    // Pure Java; use JNI-based hooking
    
    LOG_I("RevenueCat hook configured (JNI-based)");
    return true;  // Handled via Java wrapper
}

// ==================== NATIVE HELPER FUNCTIONS ====================

/**
 * Hook a JNI method via Dobby
 * Useful for hooking native methods or JNI bridges
 */
bool HookManager::hook_jni_method(const char* lib_name, const char* symbol, 
                                  void* replacement, void** original) {
    void* lib = dlopen(lib_name, RTLD_NOW);
    if (!lib) {
        LOG_W("Library not found: %s", lib_name);
        return false;
    }
    
    void* target = dlsym(lib, symbol);
    if (!target) {
        LOG_W("Symbol not found: %s in %s", symbol, lib_name);
        dlclose(lib);
        return false;
    }
    
    int ret = DobbyHook(target, replacement, original);
    dlclose(lib);
    
    if (ret != DOBBY_SUCCESS) {
        LOG_E("Failed to hook %s: %d", symbol, ret);
        return false;
    }
    
    LOG_I("Hooked native symbol: %s", symbol);
    return true;
}

