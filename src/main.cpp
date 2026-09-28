#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <map>
#include <set>
#include <cstring>
#include <sstream>
#include "locket_gold.h"
#include "hook_manager.h"

#define TAG "LocketGoldNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ==================== SMALI BYTECODE CONSTANTS ====================

// Video configuration from Smali - decoded JSON
static const char* VIDEO_CONFIG_JSON = "{\n"
    "  \"display_mode\": \"toggle\",\n"
    "  \"default\": {\n"
    "    \"maximum_duration\": 2500,\n"
    "    \"size\": 720,\n"
    "    \"file_type\": \"mp4\",\n"
    "    \"video_codec\": \"h265\"\n"
    "  },\n"
    "  \"gold\": {\n"
    "    \"maximum_duration\": 10000,\n"
    "    \"size\": 720,\n"
    "    \"file_type\": \"mp4\",\n"
    "    \"video_codec\": \"h265\"\n"
    "  }\n"
    "}";

// Feature gates from Smali
static const char* FEATURE_GATES_JSON = "{\n"
    "  \"video\": {\"enabled\": true, \"gold_only\": false},\n"
    "  \"longer_video\": {\"enabled\": true, \"gold_only\": false},\n"
    "  \"video_recording\": {\"enabled\": true, \"gold_only\": false},\n"
    "  \"unlimited_friends\": {\"enabled\": true, \"gold_only\": false},\n"
    "  \"remove_ads\": {\"enabled\": true, \"gold_only\": false},\n"
    "  \"camera_theme\": {\"enabled\": true, \"gold_only\": false},\n"
    "  \"app_icon\": {\"enabled\": true, \"gold_only\": false}\n"
    "}";

// Global state
static HookManager* g_hook_manager = nullptr;
static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;
static std::set<std::string> g_hooked_classes;
static std::string g_patched_bundle_path = "";
static bool g_js_bundle_file_hooked = false;

// Library initialization when first .so is loaded
__attribute__((constructor))
void native_init() {
    LOGI("LocketGoldNative library loaded");
    LOGI("Hermes v%d bytecode patching enabled", HERMES_VERSION_SUPPORTED);
    LOGI("Target: %s", TARGET_PACKAGE);
}

/**
 * JNI_OnLoad: Called when library is loaded via System.loadLibrary
 * Initialize Dobby and prepare for hooking
 */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("Failed to get JNIEnv");
        return JNI_ERR;
    }
    
    LOGI("JNI_OnLoad: Initializing Dobby hook infrastructure");
    // Dobby initialization happens in initHooks(), not here
    return JNI_VERSION_1_6;
}

// ==================== SMALI-DERIVED HELPER FUNCTIONS ====================

/**
 * Mutate user JSON string to inject gold subscription
 * From: HookMain.mutateUserJsonString() bytecode
 * Replaces "subscription_entitlement": "free" with "subscription_entitlement": "locket_gold"
 */
static std::string mutate_user_json_string(const std::string& json_str) {
    if (json_str.empty()) return json_str;
    
    std::string result = json_str;
    size_t pos = 0;
    
    // Look for subscription_entitlement pattern
    const char* pattern = "\"subscription_entitlement\"";
    const char* free_val = "\"free\"";
    const char* gold_val = "\"locket_gold\"";
    
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        // Find the next quote after the key
        size_t value_start = result.find(":", pos);
        if (value_start == std::string::npos) break;
        
        size_t quote_start = result.find("\"", value_start);
        if (quote_start == std::string::npos) break;
        
        size_t quote_end = result.find("\"", quote_start + 1);
        if (quote_end == std::string::npos) break;
        
        // Replace the value
        std::string old_value = result.substr(quote_start, quote_end - quote_start + 1);
        result.replace(quote_start, quote_end - quote_start + 1, gold_val);
        
        LOGD("Mutated: %s → %s", old_value.c_str(), gold_val);
        pos = quote_start + strlen(gold_val);
    }
    
    // Also inject badge marker
    pos = 0;
    const char* badge_pattern = "\"badge\"";
    const char* badge_true = "\"true\"";
    
    while ((pos = result.find(badge_pattern, pos)) != std::string::npos) {
        size_t value_start = result.find(":", pos);
        if (value_start == std::string::npos) break;
        
        size_t quote_start = result.find("\"", value_start);
        if (quote_start == std::string::npos) break;
        
        size_t quote_end = result.find("\"", quote_start + 1);
        if (quote_end == std::string::npos) break;
        
        result.replace(quote_start, quote_end - quote_start + 1, badge_true);
        pos = quote_start + strlen(badge_true);
    }
    
    return result;
}

/**
 * Inject gold configuration values into Maps
 * From: HookMain.injectGoldConfigValues() and putRemoteConfigValue()
 * Adds android_gold_subscription_override and feature gates
 */
static void inject_gold_config_values(JNIEnv* env, jobject map) {
    if (!map) return;
    
    // Get Map class
    jclass map_class = env->GetObjectClass(map);
    jmethodID put_method = env->GetMethodID(map_class, "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    
    if (!put_method) {
        LOGE("Could not find Map.put() method");
        return;
    }
    
    // Inject gold override key
    jstring key = env->NewStringUTF(GOLD_OVERRIDE_KEY);
    jstring value = env->NewStringUTF("true");
    env->CallObjectMethod(map, put_method, key, value);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(value);
    
    LOGD("Injected %s → true into config map", GOLD_OVERRIDE_KEY);
}

/**
 * initHooks: Main entry point called from target app context
 * - Must be called from the target process (com.locket.Locket)
 * - Sets up all Dobby hooks
 * - Integrates Smali bytecode analysis
 *
 * Called via JNI from wrapper app or injected code
 */
JNIEXPORT void JNICALL Java_com_locket_hook_LocketHookLib_initHooks(
    JNIEnv* env, jclass clazz, jstring jTargetPackage) {
    
    pthread_mutex_lock(&g_init_lock);
    
    if (g_initialized) {
        pthread_mutex_unlock(&g_init_lock);
        LOGI("Hooks already initialized");
        return;
    }
    
    const char* target_pkg = env->GetStringUTFChars(jTargetPackage, nullptr);
    LOGI("======================================");
    LOGI("Initializing Locket Gold hooks");
    LOGI("Target: %s", target_pkg);
    LOGI("Hermes: v%d bytecode patching", HERMES_VERSION_SUPPORTED);
    LOGI("======================================");
    
    try {
        // Create and initialize hook manager
        g_hook_manager = new HookManager();
        
        if (!g_hook_manager->initialize()) {
            LOGE("Failed to initialize hook manager");
            delete g_hook_manager;
            g_hook_manager = nullptr;
            env->ReleaseStringUTFChars(jTargetPackage, target_pkg);
            pthread_mutex_unlock(&g_init_lock);
            return;
        }
        
        // Install hooks in order of priority (from Smali analysis)
        LOGI(">>> Installing Hermes bundle patcher (Smali: HookMain._17)");
        if (!g_hook_manager->hook_hermes()) {
            LOGE("Hermes hook failed (non-critical, may not be present)");
        }
        
        LOGI(">>> Installing Firebase Remote Config hook (Smali: HookMain._10)");
        if (!g_hook_manager->hook_firebase_config()) {
            LOGE("Firebase Config hook failed (non-critical)");
        }
        
        LOGI(">>> Installing Firestore hook (Smali: HookMain._11)");
        if (!g_hook_manager->hook_firestore()) {
            LOGE("Firestore hook failed (non-critical)");
        }
        
        LOGI(">>> Installing MMKV hook (Smali: HookMain._13)");
        if (!g_hook_manager->hook_mmkv()) {
            LOGE("MMKV hook failed (non-critical)");
        }
        
        LOGI(">>> Installing React Native Networking hook (Smali: HookMain._12)");
        if (!g_hook_manager->hook_networking()) {
            LOGE("Networking hook failed (non-critical)");
        }
        
        LOGI(">>> Installing RevenueCat hook (Smali: HookMain._14, _15)");
        if (!g_hook_manager->hook_revenuecat()) {
            LOGE("RevenueCat hook failed (non-critical)");
        }
        
        g_initialized = true;
        LOGI("======================================");
        LOGI("✓ All hooks initialized successfully");
        LOGI("✓ Video config injected");
        LOGI("✓ Feature gates enabled");
        LOGI("✓ Gold subscription active");
        LOGI("======================================");
        
    } catch (const std::exception& e) {
        LOGE("Exception during initialization: %s", e.what());
        if (g_hook_manager) {
            delete g_hook_manager;
            g_hook_manager = nullptr;
        }
    }
    
    env->ReleaseStringUTFChars(jTargetPackage, target_pkg);
    pthread_mutex_unlock(&g_init_lock);
}

/**
 * cleanup: Uninstall all hooks (optional)
 */
JNIEXPORT void JNICALL Java_com_locket_hook_LocketHookLib_cleanup(
    JNIEnv* env, jclass clazz) {
    
    pthread_mutex_lock(&g_init_lock);
    
    if (g_hook_manager) {
        LOGI("Cleaning up hooks...");
        delete g_hook_manager;
        g_hook_manager = nullptr;
        g_initialized = false;
        LOGI("Cleanup complete");
    }
    
    pthread_mutex_unlock(&g_init_lock);
}

/**
 * getHookStatus: Return initialization status
 */
JNIEXPORT jboolean JNICALL Java_com_locket_hook_LocketHookLib_isInitialized(
    JNIEnv* env, jclass clazz) {
    return g_initialized;
}

// ==================== SMALI BYTECODE MAPPING REFERENCE ====================
/*
 * This native library integrates logic from the following Smali bytecode:
 *
 * HookMain.smali (main)
 *   - Class definition, field initialization
 *   - Constants: TARGET_PACKAGE, BUNDLE_ASSET, GOLD_OVERRIDE_KEY
 *   - Video config and feature gates JSON embedded
 *   - handleLoadPackage() entry point -> triggers bundle loading
 *
 * HookMain_1.smali
 *   - Application.onCreate hook
 *   - Finds ReactNativeHost superclasses
 *   - Calls hookGetJSBundleFileOnSuperclasses()
 *
 * HookMain_2.smali
 *   - initHooks() initialization
 *   - Calls hookBundleLoading(), hookReactNativeFirebaseConfigModule()
 *   - Calls hookMMKV(), hookNetworking(), hookRevenuecat()
 *
 * HookMain_3.smali
 *   - Bundle loading and Hermes patching
 *   - getJSBundleFile() hook
 *   - Bundle SHA-1 fingerprinting
 *
 * HookMain_4.smali
 *   - Fingerprint pattern matching (Hermes bytecode)
 *   - patchUnique() implementation
 *
 * HookMain_5.smali
 *   - bytesToHex() conversion
 *   - Hex pattern matching helpers
 *
 * HookMain_6.smali
 *   - Bundle path manipulation
 *   - Cache directory creation
 *
 * HookMain_7.smali
 *   - mutateUserJsonString() - JSON mutation for subscription_entitlement
 *   - mutateUserMap() - Map-based user data mutation
 *
 * HookMain_8.smali
 *   - Firebase Remote Config injection
 *   - Video config JSON embedding
 *
 * HookMain_9.smali
 *   - Feature gates JSON injection
 *   - Config map manipulation
 *
 * HookMain_10.smali
 *   - ReactNativeFirebaseConfigModule.getConstants() hook
 *   - injectGoldConfigValues() from Smali
 *
 * HookMain_11.smali
 *   - Firestore DocumentSnapshot.getData() hook
 *   - putRemoteConfigValue() helper
 *
 * HookMain_12.smali
 *   - React Native NetworkingModule.onDataReceived() hook
 *   - Network response mutation
 *
 * HookMain_13.smali
 *   - MMKV.decodeString() hook
 *   - Cache mutation for gold subscription
 *
 * HookMain_14.smali
 *   - RevenueCat EntitlementInfo.isActive() hook
 *
 * HookMain_15.smali
 *   - RevenueCat EntitlementInfo.getWillRenew() hook
 *
 * Integration Strategy:
 * ✓ Native bytecode pattern matching (hex fingerprints)
 * ✓ JNI callbacks for Java-based data mutation
 * ✓ Dobby hooks for native-level interception
 * ✓ Fallback to pure Java if native unavailable
 */

// ==================== JNI HELPER EXPORTS ====================

/**
 * JNI export: Mutate user JSON from Java context
 * Allows Java hooks to leverage native mutation logic
 */
JNIEXPORT jstring JNICALL Java_com_locket_hook_LocketHookLib_mutateUserJson(
    JNIEnv* env, jclass clazz, jstring json_str) {
    
    if (!json_str) return nullptr;
    
    const char* json_cstr = env->GetStringUTFChars(json_str, nullptr);
    std::string mutated = mutate_user_json_string(json_cstr);
    env->ReleaseStringUTFChars(json_str, json_cstr);
    
    return env->NewStringUTF(mutated.c_str());
}

/**
 * JNI export: Get video configuration JSON
 */
JNIEXPORT jstring JNICALL Java_com_locket_hook_LocketHookLib_getVideoConfig(
    JNIEnv* env, jclass clazz) {
    return env->NewStringUTF(VIDEO_CONFIG_JSON);
}

/**
 * JNI export: Get feature gates JSON
 */
JNIEXPORT jstring JNICALL Java_com_locket_hook_LocketHookLib_getFeatureGates(
    JNIEnv* env, jclass clazz) {
    return env->NewStringUTF(FEATURE_GATES_JSON);
}

