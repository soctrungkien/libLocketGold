#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <unistd.h>
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

// --- Main Patch Logic ---
static bool patch_bundle(uint8_t* data, size_t size) {
    if (!data || size < 64) return false;
    LOGI("[*] Bắt đầu thực thi patch bundle tại địa chỉ %p, kích thước %zu", data, size);

    uint32_t version = *reinterpret_cast<uint32_t*>(data + 8);
    if (version != HERMES_VERSION) {
        LOGW("[!] Cảnh báo: Hermes version %u != %u", version, HERMES_VERSION);
    } else {
        LOGI("[+] Hermes version đúng chuẩn: %u", version);
    }

    uint32_t function_count = *reinterpret_cast<uint32_t*>(data + 40);
    uint32_t string_kind_count = *reinterpret_cast<uint32_t*>(data + 44);
    uint32_t identifier_count = *reinterpret_cast<uint32_t*>(data + 48);
    uint32_t string_count = *reinterpret_cast<uint32_t*>(data + 52);
    uint32_t overflow_count = *reinterpret_cast<uint32_t*>(data + 56);
    uint32_t string_storage_size = *reinterpret_cast<uint32_t*>(data + 60);

    LOGI("[*] functions=%u, strings=%u", function_count, string_count);

    size_t p = align_val(size, 32, 111);
    size_t headers_start = p;
    p = align_val(size, 4, p + function_count * 16);
    p = align_val(size, 4, p + string_kind_count * 4);
    p = align_val(size, 4, p + identifier_count * 4);
    size_t small_string_table_pos = p;
    p = align_val(size, 4, p + string_count * 4);
    size_t overflow_table_pos = p;
    p = align_val(size, 4, p + overflow_count * 8);
    size_t storage_pos = p;

    if (p + string_storage_size > size) {
        LOGE("[-] string storage vượt kích thước file");
        return false;
    }

    // 1. Tìm subscription_entitlement
    LOGI("[*] Đang tìm subscription_entitlement...");
    int ent_id = -1;
    for (uint32_t i = 0; i < string_count; i++) {
        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, i, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;
            if (length == std::strlen(STR_ENTITLEMENT) && region_equals_ascii(data, size, abs_pos, STR_ENTITLEMENT, length)) {
                ent_id = i;
                LOGI("[+] Tìm thấy entitlement id=%d", ent_id);
                break;
            }
        }
    }
    if (ent_id < 0) {
        LOGE("[-] Không tìm thấy entitlement");
        return false;
    }

    // 2. Quét hàm (functions scan)
    LOGI("[*] Đang quét danh sách hàm...");
    size_t badge_offset = (size_t)-1, badge_size = 0;
    size_t payments_offset = (size_t)-1, payments_size = 0;
    size_t camera_offset = (size_t)-1, camera_size = 0;

    for (uint32_t i = 0; i < function_count; i++) {
        size_t base = headers_start + i * 16;
        if (base + 16 > size) break;
        uint32_t w1 = *reinterpret_cast<uint32_t*>(data + base + 4);
        if (((data[base + 15] >> 4) & 1) != 0) {
            continue;
        }
        uint32_t name_id = (w1 >> 15) & 0x1FFFF;
        if (name_id >= string_count) continue;

        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, name_id, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;
            uint32_t offset = *reinterpret_cast<uint32_t*>(data + base) & 0x1FFFFFF;
            uint32_t size_fn = w1 & 0x7FFF;

            if (badge_offset == (size_t)-1 && length == std::strlen(FN_BADGE) && region_equals_ascii(data, size, abs_pos, FN_BADGE, length)) {
                badge_offset = offset; badge_size = size_fn;
                LOGI("[+] doesUserHaveGoldBadge @ %zu", badge_offset);
            } else if (payments_offset == (size_t)-1 && length == std::strlen(FN_PAYMENTS) && region_equals_ascii(data, size, abs_pos, FN_PAYMENTS, length)) {
                payments_offset = offset; payments_size = size_fn;
                LOGI("[+] PaymentsProvider @ %zu", payments_offset);
            } else if (camera_offset == (size_t)-1 && length == std::strlen(FN_CAMERA) && region_equals_ascii(data, size, abs_pos, FN_CAMERA, length)) {
                camera_offset = offset; camera_size = size_fn;
                LOGI("[+] getAndroidCameraVideoRecordingConfig @ %zu", camera_offset);
            }
        }
        if (badge_offset != (size_t)-1 && payments_offset != (size_t)-1 && camera_offset != (size_t)-1) {
            break;
        }
    }

    if (badge_offset == (size_t)-1 || payments_offset == (size_t)-1) {
        LOGE("[-] Thiếu hàm badge hoặc payments");
        return false;
    }

    // 3. Patch doesUserHaveGoldBadge
    LOGI("[*] Đang patch doesUserHaveGoldBadge...");
    if (badge_size < 4 || badge_offset + badge_size > size || data[badge_offset + badge_size - 2] != 0x5C) {
        LOGE("[-] Cấu trúc badge lỗi hoặc vượt giới hạn");
        return false;
    }
    uint8_t ret_reg = data[badge_offset + badge_size - 1];
    data[badge_offset] = 0x78;
    data[badge_offset + 1] = ret_reg;
    data[badge_offset + 2] = 0x5C;
    data[badge_offset + 3] = ret_reg;
    LOGI("[+] Đã patch badge thành công (ret_reg=%u)", ret_reg);

    // 4. Patch PaymentsProvider
    LOGI("[*] Đang patch PaymentsProvider...");
    uint16_t id_le = static_cast<uint16_t>(ent_id);
    bool patched_pay = false;
    if (payments_offset + payments_size <= size) {
        for (size_t i = 0; i + 12 <= payments_size; i++) {
            size_t pos = payments_offset + i;
            uint16_t val = *reinterpret_cast<uint16_t*>(data + pos + 4);
            if (val == id_le && data[pos + 6] == 0x0B && data[pos + 7] == data[pos + 8] &&
                data[pos + 9] == 0x0B && data[pos + 11] == data[pos + 7]) {
                uint8_t d1 = data[pos + 7], d2 = data[pos + 10];
                size_t at = pos + 6;
                data[at] = 0x78; data[at+1] = d2;
                data[at+2] = 0x79; data[at+3] = d1;
                data[at+4] = 0x78; data[at+5] = d2;
                LOGI("[+] PaymentsProvider patched @ %zu", at);
                patched_pay = true;
                break;
            }
        }
    }
    if (!patched_pay) {
        LOGE("[-] Không tìm thấy pattern PaymentsProvider");
        return false;
    }

    // 5. Patch maximumDuration
    LOGI("[*] Đang patch maximumDuration...");
    uint32_t candidates[] = {2500, 10000};
    uint32_t new_val = 2147000000;
    int patched_dur_count = 0;
    for (uint32_t target_val : candidates) {
        uint8_t old_b[4], new_b[4];
        std::memcpy(old_b, &target_val, 4);
        std::memcpy(new_b, &new_val, 4);

        size_t start = 0;
        while (start < size) {
            const uint8_t* found_ptr = std::search(data + start, data + size, old_b, old_b + 4);
            if (found_ptr == data + size) break;
            size_t idx = found_ptr - data;

            size_t left = (idx >= 64) ? idx - 64 : 0;
            size_t right = (idx + 4 + 64 <= size) ? idx + 4 + 64 : size;

            uint32_t m_i32 = 720;
            float m_f32 = 720.0f;
            double m_f64 = 720.0;
            bool has_marker = false;
            for (size_t m = left; m + 4 <= right; m++) {
                if (std::memcmp(data + m, &m_i32, 4) == 0 ||
                    std::memcmp(data + m, &m_f32, 4) == 0 ||
                    (m + 8 <= right && std::memcmp(data + m, &m_f64, 8) == 0)) {
                    has_marker = true;
                    break;
                }
            }
            if (has_marker) {
                std::memcpy(data + idx, new_b, 4);
                patched_dur_count++;
                LOGI("[+] duration patched value %u @ %zu", target_val, idx);
            }
            start = idx + 1;
        }
    }
    LOGI("[+] Tổng duration patched: %d", patched_dur_count);

    // 6. Bypass remote config camera
    LOGI("[*] Bypass remote config android_camera_video_recording...");
    bool found_cam = false;
    for (uint32_t i = 0; i < string_count; i++) {
        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, i, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;
            if (length == std::strlen(STR_CAMERA_CFG) && region_equals_ascii(data, size, abs_pos, STR_CAMERA_CFG, length)) {
                LOGI("[+] Tìm thấy string camera config @ %zu (id=%u)", abs_pos, i);
                std::memset(data + abs_pos, 0, length);
                LOGI("[+] Đã xóa nội dung string (force falsy)");
                found_cam = true;
                break;
            }
        }
    }
    if (!found_cam) {
        LOGW("[-] Không tìm thấy string remote config camera");
    }

    // 7. Cập nhật SHA1 ở 20 byte cuối
    LOGI("[*] Cập nhật SHA1 checksum...");
    if (size >= 20) {
        SHA1_CTX ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, data, size - 20);
        sha1_final(data + size - 20, &ctx);
        LOGI("[+] SHA1 updated thành công");
    }

    return true;
}

// --- Dobby Hook mmap/mmap64 ---
typedef void* (*mmap_t)(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
typedef void* (*mmap64_t)(void* addr, size_t length, int prot, int flags, int fd, off_t offset64);

static mmap_t orig_mmap = nullptr;
static mmap64_t orig_mmap64 = nullptr;
static bool g_bundle_patched = false;

static void check_and_patch_mapping(void* ptr, size_t length) {
    if (g_bundle_patched || ptr == MAP_FAILED || ptr == nullptr) return;
    if (length < 0x10000 || length > 0x50000000) return;

    if ((uintptr_t)ptr % sizeof(uint32_t) == 0) {
        uint32_t* magic_ptr = (uint32_t*)ptr;
        if (*magic_ptr == HERMES_MAGIC) {
            LOGI("Hermes bundle caught via mmap! Address: %p, Size: %zu", ptr, length);
            
            uintptr_t page_size = sysconf(_SC_PAGESIZE);
            uintptr_t start_page = (uintptr_t)ptr & ~(page_size - 1);
            size_t len_page = (length + ((uintptr_t)ptr - start_page) + page_size - 1) & ~(page_size - 1);

            if (mprotect((void*)start_page, len_page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
                LOGW("mprotect failed, trying direct patch...");
            }

            if (patch_bundle((uint8_t*)ptr, length)) {
                g_bundle_patched = true;
                LOGI("Bundle successfully patched on the fly with all features!");
            } else {
                LOGE("Failed to apply full patches on Hermes bundle.");
            }
        }
    }
}

static void* hooked_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    void* result = orig_mmap(addr, length, prot, flags, fd, offset);
    if ((prot & PROT_READ) != 0) {
        check_and_patch_mapping(result, length);
    }
    return result;
}

static void* hooked_mmap64(void* addr, size_t length, int prot, int flags, int fd, off_t offset64) {
    void* result = orig_mmap64(addr, length, prot, flags, fd, offset64);
    if ((prot & PROT_READ) != 0) {
        check_and_patch_mapping(result, length);
    }
    return result;
}

static void init_mmap_hooks() {
    void* mmap_addr = DobbySymbolResolver("libc.so", "mmap");
    if (mmap_addr) {
        DobbyHook(mmap_addr, (void*)hooked_mmap, (void**)&orig_mmap);
        LOGI("Successfully hooked mmap!");
    } else {
        LOGE("Failed to resolve symbol: mmap");
    }

    void* mmap64_addr = DobbySymbolResolver("libc.so", "mmap64");
    if (mmap64_addr) {
        DobbyHook(mmap64_addr, (void*)hooked_mmap64, (void**)&orig_mmap64);
        LOGI("Successfully hooked mmap64!");
    } else {
        LOGW("mmap64 symbol unified or not found separately.");
    }
}

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("LocketGold native library loaded successfully.");
    init_mmap_hooks();
    return JNI_VERSION_1_6;
}
