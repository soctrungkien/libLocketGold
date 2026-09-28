package com.locket.hook;

import android.util.Log;

/**
 * JNI bridge to native Dobby hook library
 * 
 * Usage:
 * 
 *   // Load library
 *   System.loadLibrary("locket_gold");
 *   
 *   // Initialize hooks
 *   LocketHookLib.initHooks("com.locket.Locket");
 *   
 *   // Check status
 *   if (LocketHookLib.isInitialized()) {
 *       Log.i(TAG, "Native hooks active");
 *   }
 *   
 *   // Cleanup (optional)
 *   LocketHookLib.cleanup();
 */
public class LocketHookLib {
    private static final String TAG = "LocketGold";
    
    static {
        try {
            System.loadLibrary("locket_gold");
            Log.i(TAG, "Native hook library loaded successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "Failed to load native hook library", e);
        }
    }
    
    /**
     * Initialize all Dobby hooks
     * Must be called from target process (com.locket.Locket)
     * 
     * @param targetPackage Package name to hook (usually "com.locket.Locket")
     */
    public static void initHooks(String targetPackage) {
        try {
            nativeInitHooks(targetPackage);
            Log.i(TAG, "Native hooks initialized");
        } catch (Throwable e) {
            Log.e(TAG, "Failed to initialize native hooks", e);
        }
    }
    
    /**
     * Cleanup and uninstall all hooks
     * Optional; called automatically on library unload
     */
    public static void cleanup() {
        try {
            nativeCleanup();
            Log.i(TAG, "Native hooks cleaned up");
        } catch (Throwable e) {
            Log.e(TAG, "Failed to cleanup native hooks", e);
        }
    }
    
    /**
     * Check if hooks are initialized
     * 
     * @return true if all hooks are active
     */
    public static boolean isInitialized() {
        try {
            return nativeIsInitialized();
        } catch (Throwable e) {
            Log.w(TAG, "Failed to check initialization status", e);
            return false;
        }
    }
    
    /**
     * Mutate user JSON to inject gold subscription marker
     * Leverages native C++ mutation logic for performance
     * 
     * @param jsonString User data as JSON string
     * @return Mutated JSON with gold subscription markers
     */
    public static String mutateUserJson(String jsonString) {
        try {
            return nativeMutateUserJson(jsonString);
        } catch (Throwable e) {
            Log.w(TAG, "Native JSON mutation failed, returning original", e);
            return jsonString;
        }
    }
    
    /**
     * Get video configuration with extended duration for gold users
     * From Smali: HookMain.VIDEO_CONFIG_JSON
     * 
     * @return Video config as JSON string
     */
    public static String getVideoConfig() {
        try {
            return nativeGetVideoConfig();
        } catch (Throwable e) {
            Log.w(TAG, "Failed to get video config", e);
            return "{}";
        }
    }
    
    /**
     * Get feature gates with all gold features enabled
     * From Smali: HookMain.FEATURE_GATES_JSON
     * 
     * @return Feature gates as JSON string
     */
    public static String getFeatureGates() {
        try {
            return nativeGetFeatureGates();
        } catch (Throwable e) {
            Log.w(TAG, "Failed to get feature gates", e);
            return "{}";
        }
    }
    
    // ==================== NATIVE METHODS ====================
    // These are implemented in liblocket_gold.so (C++)
    // See: main.cpp integration with Smali bytecode
    
    private static native void nativeInitHooks(String targetPackage);
    
    private static native void nativeCleanup();
    
    private static native boolean nativeIsInitialized();
    
    /**
     * Native helper: Mutate user JSON
     * From Smali: HookMain.mutateUserJsonString()
     */
    private static native String nativeMutateUserJson(String jsonString);
    
    /**
     * Native helper: Get video configuration
     * From Smali: HookMain.VIDEO_CONFIG_JSON constant
     */
    private static native String nativeGetVideoConfig();
    
    /**
     * Native helper: Get feature gates
     * From Smali: HookMain.FEATURE_GATES_JSON constant
     */
    private static native String nativeGetFeatureGates();
}

