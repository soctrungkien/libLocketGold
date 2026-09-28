#include "log_util.h"

/**
 * RevenueCat Hook: EntitlementInfo passive getters
 * 
 * isActive() and getWillRenew() always return true
 * Pure Java; JNI-based
 */

void hook_revenuecat_setup() {
    LOG_I("RevenueCat hook setup (JNI-based)");
}

