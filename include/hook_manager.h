#ifndef HOOK_MANAGER_H
#define HOOK_MANAGER_H

#include <string>

/**
 * HookManager: Central orchestrator for all Dobby hooks
 * 
 * Manages:
 * - Hook installation/teardown via Dobby
 * - JNI callback registration
 * - Hook state tracking
 */
class HookManager {
public:
    HookManager();
    ~HookManager();
    
    // Initialize hook infrastructure (Dobby setup, etc.)
    bool initialize();
    
    // Install individual hook categories
    bool hook_hermes();              // Bundle patching (non-critical)
    bool hook_firebase_config();     // Remote config injection (JNI)
    bool hook_firestore();           // Firestore.getData() mutation (JNI)
    bool hook_mmkv();                // MMKV cache manipulation (JNI)
    bool hook_networking();          // RN Networking interception (JNI)
    bool hook_revenuecat();          // RevenueCat spoofing (JNI)
    
    // Native helper for hooking native libraries
    bool hook_jni_method(const char* lib_name, const char* symbol,
                        void* replacement, void** original);
    
    bool is_initialized() const { return initialized; }
    
private:
    bool initialized;
};

#endif // HOOK_MANAGER_H

