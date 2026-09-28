#include "log_util.h"
#include <cstring>
#include <vector>

/**
 * Hermes Bytecode Patcher
 * 
 * Patches Hermes v96 bytecode to unlock gold features:
 * 1. PaymentsProvider: hasGoldSubscription always true
 * 2. doesUserHaveGoldBadge: always returns true
 * 
 * Strategy: Patch bundle at load time or memory level
 */

struct HermesFingerprint {
    const char* version;
    const char* prefix;
    const char* replacement;
    const char* suffix;
};

// Simplified fingerprint list (from Java version)
static const HermesFingerprint FINGERPRINTS[] = {
    {"1.234.x", "9D8B|0B0A0A0B0D0A", "780D790A780D", "92060A08"},
    {"1.236.0", "0B9C81|0B0A0A0B0D0A", "780D790A780D", "92060A08"},
    {"1.238.x", "0B28DD|0B0A0A0B0D0A", "780D790A780D", "92060A08"},
};

/**
 * Parse hex string ("AABBCC" or "AA BB CC") into bytes
 */
static bool hex_to_bytes(const char* hex_str, std::vector<uint8_t>& out) {
    std::string clean;
    for (const char* p = hex_str; *p; ++p) {
        if (*p != ' ' && *p != '|' && *p != '-') {
            clean += *p;
        }
    }
    
    if (clean.length() % 2 != 0) {
        LOG_E("Invalid hex length: %s", hex_str);
        return false;
    }
    
    out.clear();
    for (size_t i = 0; i < clean.length(); i += 2) {
        char byte_str[3] = {clean[i], clean[i+1], '\0'};
        int byte_val = (int)strtol(byte_str, nullptr, 16);
        out.push_back((uint8_t)byte_val);
    }
    
    return true;
}

/**
 * Find pattern in buffer
 */
static const uint8_t* find_pattern(const uint8_t* buffer, size_t buffer_size,
                                   const std::vector<uint8_t>& pattern) {
    if (pattern.empty() || buffer_size < pattern.size()) {
        return nullptr;
    }
    
    for (size_t i = 0; i <= buffer_size - pattern.size(); ++i) {
        if (std::memcmp(buffer + i, pattern.data(), pattern.size()) == 0) {
            return buffer + i;
        }
    }
    return nullptr;
}

/**
 * Apply bytecode patch to Hermes bundle
 * Returns true if patch applied, false if fingerprint not recognized
 */
bool patch_hermes_bundle(uint8_t* bundle_data, size_t bundle_size) {
    if (!bundle_data || bundle_size < 1024) {
        LOG_E("Invalid bundle data");
        return false;
    }
    
    LOG_I("Attempting to patch Hermes bundle (%zu bytes)...", bundle_size);
    
    // Try each known fingerprint
    for (const auto& fp : FINGERPRINTS) {
        std::vector<uint8_t> prefix, middle, suffix;
        
        // Parse pattern
        if (!hex_to_bytes(fp.prefix, prefix)) {
            LOG_W("Failed to parse prefix for %s", fp.version);
            continue;
        }
        if (!hex_to_bytes(fp.replacement, middle)) {
            LOG_W("Failed to parse replacement for %s", fp.version);
            continue;
        }
        if (!hex_to_bytes(fp.suffix, suffix)) {
            LOG_W("Failed to parse suffix for %s", fp.version);
            continue;
        }
        
        // Build full pattern: prefix + middle + suffix
        std::vector<uint8_t> full_pattern = prefix;
        full_pattern.insert(full_pattern.end(), middle.begin(), middle.end());
        full_pattern.insert(full_pattern.end(), suffix.begin(), suffix.end());
        
        // Search for pattern
        const uint8_t* match = find_pattern(bundle_data, bundle_size, full_pattern);
        if (!match) {
            LOG_D("Pattern not found for %s", fp.version);
            continue;
        }
        
        LOG_I("Found Hermes %s bytecode pattern", fp.version);
        
        // Calculate replacement offset and size
        size_t replacement_offset = match - bundle_data + prefix.size();
        
        // Patch: Load true → return
        // "780D790A780D" = LoadConstTrue r13; LoadConstFalse r10; LoadConstTrue r13
        const uint8_t true_return[] = {0x78, 0x00, 0x0C, 0x01};  // LoadConstTrue; Ret
        
        if (replacement_offset + sizeof(true_return) <= bundle_size) {
            std::memcpy((uint8_t*)bundle_data + replacement_offset, true_return, sizeof(true_return));
            LOG_I("Patched Hermes bytecode at offset 0x%zx", replacement_offset);
            return true;
        }
    }
    
    LOG_W("No matching Hermes fingerprint found; newer version may require update");
    return false;  // Non-critical
}

