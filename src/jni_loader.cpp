#include <jni.h>
#include <android/log.h>

#define TAG "LocketLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/**
 * Optional JNI Loader Library
 * 
 * If you want to inject the hook into the target app,
 * this loader can be called from a wrapper or injector app
 * to dynamically load liblocket_gold.so
 */

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("Failed to get JNIEnv");
        return JNI_ERR;
    }
    
    LOGI("Loader JNI_OnLoad called");
    
    // Register native methods if needed
    // For now, just return version
    
    return JNI_VERSION_1_6;
}

/**
 * Load the hook library into target process
 * Call this from wrapper app's context
 */
JNIEXPORT void JNICALL Java_com_locket_loader_HookLoader_loadHookLibrary(
    JNIEnv* env, jclass clazz, jstring jLibPath) {
    
    const char* lib_path = env->GetStringUTFChars(jLibPath, nullptr);
    LOGI("Loading hook library: %s", lib_path);
    
    // This would typically be called with path to liblocket_gold.so
    // System.loadLibrary("locket_gold") is simpler for embedded case
    
    env->ReleaseStringUTFChars(jLibPath, lib_path);
}

