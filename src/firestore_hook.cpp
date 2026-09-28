#include "log_util.h"

/**
 * Firestore Hook: DocumentSnapshot.getData()
 * 
 * Mutates returned user data to inject gold subscription
 * Implemented via JNI callbacks from Java layer
 */

void hook_firestore_setup() {
    LOG_I("Firestore hook setup (JNI-based)");
}

