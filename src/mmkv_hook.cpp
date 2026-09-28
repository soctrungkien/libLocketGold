#include "log_util.h"

/**
 * MMKV Hook: decodeString()
 * 
 * Mutates cached user data to inject gold subscription
 * Hybrid: Can hook MMKV native layer or JNI layer
 */

void hook_mmkv_setup() {
    LOG_I("MMKV hook setup (JNI-based)");
}

