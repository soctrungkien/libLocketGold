#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include "dobby.h"

#define TAG "LocketGold"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

static const uint32_t HERMES_VERSION = 96;
static const uint32_t HERMES_MAGIC = 0x03BC1FC6;

static const char FN_BADGE[] = "doesUserHaveGoldBadge";
static const char FN_PAYMENTS[] = "PaymentsProvider";
static const char FN_CAMERA[] = "getAndroidCameraVideoRecordingConfig";
static const char STR_ENTITLEMENT[] = "subscription_entitlement";
static const char STR_CAMERA_CFG[] = "android_camera_video_recording";

static const char* REDIRECT_PATH = "/data/user/0/com.locket.Locket/files/index.android.bundle";
static const char* TARGET_BUNDLE_NAME = "index.android.bundle";

// --- Standalone SHA1 Implementation ---
typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e, i, j, t, m[80];
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (buffer[j] << 24) | (buffer[j + 1] << 16) | (buffer[j + 2] << 8) | (buffer[j + 3]);
    for (; i < 80; ++i) {
        m[i] = m[i - 3] ^ m[i - 8] ^ m[i - 14] ^ m[i - 16];
        m[i] = (m[i] << 1) | (m[i] >> 31);
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    for (i = 0; i < 20; ++i) {
        t = ((a << 5) | (a >> 27)) + ((b & c) | (~b & d)) + e + m[i] + 0x5A827999;
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    for (; i < 40; ++i) {
        t = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + m[i] + 0x6ED9EBA1;
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    for (; i < 60; ++i) {
        t = ((a << 5) | (a >> 27)) + ((b & c) | (b & d) | (c & d)) + e + m[i] + 0x8F1BBCDC;
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    for (; i < 80; ++i) {
        t = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + m[i] + 0xCA62C1D6;
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_init(SHA1_CTX *context) {
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}

static void sha1_update(SHA1_CTX *context, const uint8_t *data, size_t len) {
    size_t i, j;
    j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += (uint32_t)len << 3) < ((uint32_t)len << 3)) context->count[1]++;
    context->count[1] += (uint32_t)(len >> 29);
    if ((j + len) > 63) {
        std::memcpy(&context->buffer[j], data, (i = 64 - j));
        sha1_transform(context->state, context->buffer);
        for (; i + 63 < len; i += 64) sha1_transform(context->state, &data[i]);
        j = 0;
    } else i = 0;
    std::memcpy(&context->buffer[j], &data[i], len - i);
}

static void sha1_final(uint8_t digest[20], SHA1_CTX *context) {
    uint32_t i;
    uint8_t finalcount[8];
    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    sha1_update(context, (const uint8_t *)"\200", 1);
    while ((context->count[0] & 504) != 448) sha1_update(context, (const uint8_t *)"\0", 1);
    sha1_update(context, finalcount, 8);
    for (i = 0; i < 20; i++) {
        digest[i] = (unsigned char)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

// --- Helper Functions ---
static size_t align_val(size_t total, size_t alignment, size_t position) {
    size_t aligned = (position + alignment - 1) & ~(alignment - 1);
    if (aligned > total) {
        return 0x7FFFFFFF - 1;
    }
    return aligned;
}

static bool region_equals_ascii(const uint8_t* data, size_t data_len, size_t pos, const char* want, size_t want_len) {
    if (pos + want_len > data_len) {
        return false;
    }
    return std::memcmp(data + pos, want, want_len) == 0;
}

static bool get_string_info(const uint8_t* data, size_t data_len, size_t small_string_table_pos, size_t overflow_table_pos, size_t storage_pos, size_t string_count, size_t idx, size_t& out_abs_pos, size_t& out_length, bool& out_is_utf16) {
    if (idx >= string_count) return false;
    uint32_t entry = *reinterpret_cast<const uint32_t*>(data + small_string_table_pos + idx * 4);
    out_is_utf16 = (entry & 1) != 0;
    if (out_is_utf16) return true;
    
    size_t off = (entry >> 1) & 0x7FFFFF;
    size_t length = (entry >> 24) & 0xFF;
    if (length == 0xFF) {
        uint32_t ov = *reinterpret_cast<const uint32_t*>(data + overflow_table_pos + off * 8);
        length = *reinterpret_cast<const uint32_t*>(data + overflow_table_pos + off * 8 + 4);
        out_abs_pos = storage_pos + ov;
    } else {
        out_abs_pos = storage_pos + off;
    }
    return true;
}

// --- Patch Bundle Logic ---
static bool patch_bundle_buffer(std::vector<uint8_t>& data) {
    if (data.size() < 64) return false;

    uint32_t version = *reinterpret_cast<uint32_t*>(data.data() + 8);
    if (version != HERMES_VERSION) {
        LOGW("Hermes version %u != %u", version, HERMES_VERSION);
    }

    uint32_t function_count = *reinterpret_cast<uint32_t*>(data.data() + 40);
    uint32_t string_kind_count = *reinterpret_cast<uint32_t*>(data.data() + 44);
    uint32_t identifier_count = *reinterpret_cast<uint32_t*>(data.data() + 48);
    uint32_t string_count = *reinterpret_cast<uint32_t*>(data.data() + 52);
    uint32_t overflow_count = *reinterpret_cast<uint32_t*>(data.data() + 56);
    uint32_t string_storage_size = *reinterpret_cast<uint32_t*>(data.data() + 60);

    size_t p = align_val(data.size(), 32, 111);
    size_t headers_start = p;
    p = align_val(data.size(), 4, p + function_count * 16);
    p = align_val(data.size(), 4, p + string_kind_count * 4);
    p = align_val(data.size(), 4, p + identifier_count * 4);
    size_t small_string_table_pos = p;
    p = align_val(data.size(), 4, p + string_count * 4);
    size_t overflow_table_pos = p;
    p = align_val(data.size(), 4, p + overflow_count * 8);
    size_t storage_pos = p;

    if (p + string_storage_size > data.size()) return false;

    // 1. entitlement
    int ent_id = -1;
    for (uint32_t i = 0; i < string_count; i++) {
        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data.data(), data.size(), small_string_table_pos, overflow_table_pos, storage_pos, string_count, i, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;
            if (length == std::strlen(STR_ENTITLEMENT) && region_equals_ascii(data.data(), data.size(), abs_pos, STR_ENTITLEMENT, length)) {
                ent_id = i;
                break;
            }
        }
    }
    if (ent_id < 0) return false;

    // 2. scan functions
    size_t badge_offset = (size_t)-1, badge_size = 0;
    size_t payments_offset = (size_t)-1, payments_size = 0;
    size_t camera_offset = (size_t)-1, camera_size = 0;

    for (uint32_t i = 0; i < function_count; i++) {
        size_t base = headers_start + i * 16;
        if (base + 16 > data.size()) break;
        uint32_t w1 = *reinterpret_cast<uint32_t*>(data.data() + base + 4);
        if (((data[base + 15] >> 4) & 1) != 0) continue;
        uint32_t name_id = (w1 >> 15) & 0x1FFFF;
        if (name_id >= string_count) continue;

        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data.data(), data.size(), small_string_table_pos, overflow_table_pos, storage_pos, string_count, name_id, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;
            uint32_t offset = *reinterpret_cast<uint32_t*>(data.data() + base) & 0x1FFFFFF;
            uint32_t size_fn = w1 & 0x7FFF;

            if (badge_offset == (size_t)-1 && length == std::strlen(FN_BADGE) && region_equals_ascii(data.data(), data.size(), abs_pos, FN_BADGE, length)) {
                badge_offset = offset; badge_size = size_fn;
            } else if (payments_offset == (size_t)-1 && length == std::strlen(FN_PAYMENTS) && region_equals_ascii(data.data(), data.size(), abs_pos, FN_PAYMENTS, length)) {
                payments_offset = offset; payments_size = size_fn;
            } else if (camera_offset == (size_t)-1 && length == std::strlen(FN_CAMERA) && region_equals_ascii(data.data(), data.size(), abs_pos, FN_CAMERA, length)) {
                camera_offset = offset; camera_size = size_fn;
            }
        }
        if (badge_offset != (size_t)-1 && payments_offset != (size_t)-1 && camera_offset != (size_t)-1) break;
    }

    if (badge_offset == (size_t)-1 || payments_offset == (size_t)-1) return false;

    // 3. patch badge
    if (badge_size < 4 || badge_offset + badge_size > data.size() || data[badge_offset + badge_size - 2] != 0x5C) return false;
    uint8_t ret_reg = data[badge_offset + badge_size - 1];
    data[badge_offset] = 0x78;
    data[badge_offset + 1] = ret_reg;
    data[badge_offset + 2] = 0x5C;
    data[badge_offset + 3] = ret_reg;

    // 4. patch payments
    uint16_t id_le = static_cast<uint16_t>(ent_id);
    bool patched_pay = false;
    if (payments_offset + payments_size <= data.size()) {
        for (size_t i = 0; i + 12 <= payments_size; i++) {
            size_t pos = payments_offset + i;
            uint16_t val = *reinterpret_cast<uint16_t*>(data.data() + pos + 4);
            if (val == id_le && data[pos + 6] == 0x0B && data[pos + 7] == data[pos + 8] &&
                data[pos + 9] == 0x0B && data[pos + 11] == data[pos + 7]) {
                uint8_t d1 = data[pos + 7], d2 = data[pos + 10];
                size_t at = pos + 6;
                data[at] = 0x78; data[at+1] = d2;
                data[at+2] = 0x79; data[at+3] = d1;
                data[at+4] = 0x78; data[at+5] = d2;
                patched_pay = true;
                break;
            }
        }
    }
    if (!patched_pay) return false;

    // 5. patch duration
    uint32_t candidates[] = {2500, 10000};
    uint32_t new_val = 2147000000;
    for (uint32_t target_val : candidates) {
        uint8_t old_b[4], new_b[4];
        std::memcpy(old_b, &target_val, 4);
        std::memcpy(new_b, &new_val, 4);

        size_t start = 0;
        while (start < data.size()) {
            auto found = std::search(data.begin() + start, data.end(), old_b, old_b + 4);
            if (found == data.end()) break;
            size_t idx = std::distance(data.begin(), found);

            size_t left = (idx >= 64) ? idx - 64 : 0;
            size_t right = (idx + 4 + 64 <= data.size()) ? idx + 4 + 64 : data.size();

            uint32_t m_i32 = 720;
            float m_f32 = 720.0f;
            double m_f64 = 720.0;
            bool has_marker = false;
            for (size_t m = left; m + 4 <= right; m++) {
                if (std::memcmp(data.data() + m, &m_i32, 4) == 0 ||
                    std::memcmp(data.data() + m, &m_f32, 4) == 0 ||
                    (m + 8 <= right && std::memcmp(data.data() + m, &m_f64, 8) == 0)) {
                    has_marker = true;
                    break;
                }
            }
            if (has_marker) {
                std::memcpy(data.data() + idx, new_b, 4);
            }
            start = idx + 1;
        }
    }

    // 6. bypass remote config camera
    for (uint32_t i = 0; i < string_count; i++) {
        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data.data(), data.size(), small_string_table_pos, overflow_table_pos, storage_pos, string_count, i, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;
            if (length == std::strlen(STR_CAMERA_CFG) && region_equals_ascii(data.data(), data.size(), abs_pos, STR_CAMERA_CFG, length)) {
                std::memset(data.data() + abs_pos, 0, length);
                break;
            }
        }
    }

    // 7. update SHA1
    if (data.size() >= 20) {
        SHA1_CTX ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, data.data(), data.size() - 20);
        sha1_final(data.data() + data.size() - 20, &ctx);
    }

    return true;
}

// Extract base.apk path from maps
static std::string find_apk_path() {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("com.locket.Locket") != std::string::npos && line.find("base.apk") != std::string::npos) {
            size_t idx = line.find("/data/app/");
            if (idx != std::string::npos) {
                return line.substr(idx);
            }
        }
    }
    return "";
}

// Extract bundle from APK zip
static std::vector<uint8_t> extract_bundle_from_apk(const std::string& apk_path) {
    std::ifstream apk(apk_path, std::ios::binary);
    if (!apk.is_open()) return {};

    apk.seekg(0, std::ios::end);
    size_t file_size = apk.tellg();
    apk.seekg(0, std::ios::beg);

    std::vector<uint8_t> apk_data(file_size);
    apk.read(reinterpret_cast<char*>(apk_data.data()), file_size);
    apk.close();

    const std::string target = "assets/index.android.bundle";
    for (size_t i = 0; i < apk_data.size() - target.size(); ++i) {
        if (std::memcmp(&apk_data[i], target.data(), target.size()) == 0) {
            if (i >= 30) {
                size_t header_pos = i - 30;
                uint32_t signature = *reinterpret_cast<const uint32_t*>(&apk_data[header_pos]);
                if (signature == 0x04034b50) {
                    uint16_t compression_method = *reinterpret_cast<const uint16_t*>(&apk_data[header_pos + 8]);
                    uint32_t uncompressed_size = *reinterpret_cast<const uint32_t*>(&apk_data[header_pos + 22]);
                    uint16_t name_len = *reinterpret_cast<const uint16_t*>(&apk_data[header_pos + 26]);
                    uint16_t extra_len = *reinterpret_cast<const uint16_t*>(&apk_data[header_pos + 28]);

                    size_t data_offset = header_pos + 30 + name_len + extra_len;
                    if (compression_method == 0 && data_offset + uncompressed_size <= apk_data.size()) {
                        std::vector<uint8_t> bundle_data(uncompressed_size);
                        std::memcpy(bundle_data.data(), &apk_data[data_offset], uncompressed_size);
                        return bundle_data;
                    }
                }
            }
        }
    }
    return {};
}

// Automatically extract and patch on every startup
static void prepare_and_patch_bundle() {
    std::string apk_path = find_apk_path();
    if (apk_path.empty()) {
        LOGE("Failed to find base.apk path!");
        return;
    }
    LOGI("Found APK path: %s", apk_path.c_str());

    std::vector<uint8_t> bundle = extract_bundle_from_apk(apk_path);
    if (bundle.empty()) {
        LOGE("Failed to extract index.android.bundle from APK!");
        return;
    }
    LOGI("Extracted bundle size: %zu bytes", bundle.size());

    if (!patch_bundle_buffer(bundle)) {
        LOGE("Failed to patch bundle buffer!");
        return;
    }

    std::ofstream outfile(REDIRECT_PATH, std::ios::binary);
    if (outfile.is_open()) {
        outfile.write(reinterpret_cast<const char*>(bundle.data()), bundle.size());
        outfile.close();
        LOGI("Patched bundle successfully written to: %s", REDIRECT_PATH);
    } else {
        LOGE("Failed to write patched bundle to %s", REDIRECT_PATH);
    }
}

// --- Dobby Hook openat ---
typedef int (*openat_t)(int dirfd, const char* pathname, int flags, mode_t mode);
static openat_t orig_openat = nullptr;

static int hooked_openat(int dirfd, const char* pathname, int flags, mode_t mode) {
    if (pathname != nullptr) {
        if (std::strstr(pathname, TARGET_BUNDLE_NAME) != nullptr) {
            if (std::strcmp(pathname, REDIRECT_PATH) != 0) {
                LOGI("Redirecting file open: %s -> %s", pathname, REDIRECT_PATH);
                return orig_openat(dirfd, REDIRECT_PATH, flags, mode);
            }
        }
    }
    return orig_openat(dirfd, pathname, flags, mode);
}

static void init_openat_hook() {
    void* openat_addr = DobbySymbolResolver("libc.so", "openat");
    if (openat_addr) {
        DobbyHook(openat_addr, (void*)hooked_openat, (void**)&orig_openat);
        LOGI("Successfully hooked openat!");
    } else {
        LOGE("Failed to resolve openat symbol");
    }
}

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("LocketGold initializing...");
    prepare_and_patch_bundle();
    init_openat_hook();
    return JNI_VERSION_1_6;
}
