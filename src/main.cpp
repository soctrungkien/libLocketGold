#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include "dobby.h"

#define TAG "LocketGold"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

static const uint32_t HERMES_VERSION = 96;[span_1](start_span)[span_1](end_span)
static const uint32_t HERMES_MAGIC = 0x03BC1FC6;

static const char FN_BADGE[] = "doesUserHaveGoldBadge";[span_2](start_span)[span_2](end_span)
static const char FN_PAYMENTS[] = "PaymentsProvider";[span_3](start_span)[span_3](end_span)
static const char FN_CAMERA[] = "getAndroidCameraVideoRecordingConfig";[span_4](start_span)[span_4](end_span)
static const char STR_ENTITLEMENT[] = "subscription_entitlement";[span_5](start_span)[span_5](end_span)
static const char STR_CAMERA_CFG[] = "android_camera_video_recording";[span_6](start_span)[span_6](end_span)

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
    size_t aligned = (position + alignment - 1) & ~(alignment - 1);[span_7](start_span)[span_7](end_span)
    if (aligned > total) {
        return 0x7FFFFFFF - 1;[span_8](start_span)[span_8](end_span)
    }
    return aligned;[span_9](start_span)[span_9](end_span)
}

static bool region_equals_ascii(const uint8_t* data, size_t data_len, size_t pos, const char* want, size_t want_len) {
    if (pos < 0 || pos + want_len > data_len) {[span_10](start_span)[span_10](end_span)
        return false;[span_11](start_span)[span_11](end_span)
    }
    return std::memcmp(data + pos, want, want_len) == 0;[span_12](start_span)[span_12](end_span)
}

static bool get_string_info(const uint8_t* data, size_t data_len, size_t small_string_table_pos, size_t overflow_table_pos, size_t storage_pos, size_t string_count, size_t idx, size_t& out_abs_pos, size_t& out_length, bool& out_is_utf16) {
    if (idx >= string_count) return false;[span_13](start_span)[span_13](end_span)
    uint32_t entry = *reinterpret_cast<const uint32_t*>(data + small_string_table_pos + idx * 4);[span_14](start_span)[span_14](end_span)
    out_is_utf16 = (entry & 1) != 0;[span_15](start_span)[span_15](end_span)
    if (out_is_utf16) return true;[span_16](start_span)[span_16](end_span)
    
    size_t off = (entry >> 1) & 0x7FFFFF;[span_17](start_span)[span_17](end_span)
    size_t length = (entry >> 24) & 0xFF;[span_18](start_span)[span_18](end_span)
    if (length == 0xFF) {[span_19](start_span)[span_19](end_span)
        uint32_t ov = *reinterpret_cast<const uint32_t*>(data + overflow_table_pos + off * 8);[span_20](start_span)[span_20](end_span)
        length = *reinterpret_cast<const uint32_t*>(data + overflow_table_pos + off * 8 + 4);[span_21](start_span)[span_21](end_span)
        out_abs_pos = storage_pos + ov;[span_22](start_span)[span_22](end_span)
    } else {
        out_abs_pos = storage_pos + off;[span_23](start_span)[span_23](end_span)
    }
    return true;[span_24](start_span)[span_24](end_span)
}

// --- Main Patch Logic Translation ---
static bool patch_bundle(uint8_t* data, size_t size) {
    if (!data || size < 64) return false;
    LOGI("[*] Bắt đầu thực thi patch bundle tại địa chỉ %p, kích thước %zu", data, size);

    uint32_t version = *reinterpret_cast<uint32_t*>(data + 8);[span_25](start_span)[span_25](end_span)
    if (version != HERMES_VERSION) {[span_26](start_span)[span_26](end_span)
        LOGW("[!] Cảnh báo: Hermes version %u != %u", version, HERMES_VERSION);[span_27](start_span)[span_27](end_span)
    } else {
        LOGI("[+] Hermes version đúng chuẩn: %u", version);[span_28](start_span)[span_28](end_span)
    }

    uint32_t function_count = *reinterpret_cast<uint32_t*>(data + 40);[span_29](start_span)[span_29](end_span)
    uint32_t string_kind_count = *reinterpret_cast<uint32_t*>(data + 44);[span_30](start_span)[span_30](end_span)
    uint32_t identifier_count = *reinterpret_cast<uint32_t*>(data + 48);[span_31](start_span)[span_31](end_span)
    uint32_t string_count = *reinterpret_cast<uint32_t*>(data + 52);[span_32](start_span)[span_32](end_span)
    uint32_t overflow_count = *reinterpret_cast<uint32_t*>(data + 56);[span_33](start_span)[span_33](end_span)
    uint32_t string_storage_size = *reinterpret_cast<uint32_t*>(data + 60);[span_34](start_span)[span_34](end_span)

    LOGI("[*] functions=%u, strings=%u", function_count, string_count);[span_35](start_span)[span_35](end_span)

    size_t p = align_val(size, 32, 111);[span_36](start_span)[span_36](end_span)
    size_t headers_start = p;[span_37](start_span)[span_37](end_span)
    p = align_val(size, 4, p + function_count * 16);[span_38](start_span)[span_38](end_span)
    p = align_val(size, 4, p + string_kind_count * 4);[span_39](start_span)[span_39](end_span)
    p = align_val(size, 4, p + identifier_count * 4);[span_40](start_span)[span_40](end_span)
    size_t small_string_table_pos = p;[span_41](start_span)[span_41](end_span)
    p = align_val(size, 4, p + string_count * 4);[span_42](start_span)[span_42](end_span)
    size_t overflow_table_pos = p;[span_43](start_span)[span_43](end_span)
    p = align_val(size, 4, p + overflow_count * 8);[span_44](start_span)[span_44](end_span)
    size_t storage_pos = p;[span_45](start_span)[span_45](end_span)

    if (p + string_storage_size > size) {[span_46](start_span)[span_46](end_span)
        LOGE("[-] string storage vượt kích thước file");[span_47](start_span)[span_47](end_span)
        return false;[span_48](start_span)[span_48](end_span)
    }

    // 1. Tìm subscription_entitlement
    LOGI("[*] Đang tìm subscription_entitlement...");[span_49](start_span)[span_49](end_span)
    int ent_id = -1;
    for (uint32_t i = 0; i < string_count; i++) {
        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, i, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;[span_50](start_span)[span_50](end_span)
            if (length == std::strlen(STR_ENTITLEMENT) && region_equals_ascii(data, size, abs_pos, STR_ENTITLEMENT, length)) {[span_51](start_span)[span_51](end_span)
                ent_id = i;[span_52](start_span)[span_52](end_span)
                LOGI("[+] Tìm thấy entitlement id=%d", ent_id);[span_53](start_span)[span_53](end_span)
                break;[span_54](start_span)[span_54](end_span)
            }
        }
    }
    if (ent_id < 0) {[span_55](start_span)[span_55](end_span)
        LOGE("[-] Không tìm thấy entitlement");[span_56](start_span)[span_56](end_span)
        return false;[span_57](start_span)[span_57](end_span)
    }

    // 2. Quét hàm (functions scan)
    LOGI("[*] Đang quét danh sách hàm...");[span_58](start_span)[span_58](end_span)
    size_t badge_offset = (size_t)-1, badge_size = 0;
    size_t payments_offset = (size_t)-1, payments_size = 0;
    size_t camera_offset = (size_t)-1, camera_size = 0;

    for (uint32_t i = 0; i < function_count; i++) {
        size_t base = headers_start + i * 16;[span_59](start_span)[span_59](end_span)
        if (base + 16 > size) break;
        uint32_t w1 = *reinterpret_cast<uint32_t*>(data + base + 4);[span_60](start_span)[span_60](end_span)
        if (((data[base + 15] >> 4) & 1) != 0) {[span_61](start_span)[span_61](end_span)
            continue;[span_62](start_span)[span_62](end_span)
        }
        uint32_t name_id = (w1 >> 15) & 0x1FFFF;[span_63](start_span)[span_63](end_span)
        if (name_id >= string_count) continue;[span_64](start_span)[span_64](end_span)

        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, name_id, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;[span_65](start_span)[span_65](end_span)
            uint32_t offset = *reinterpret_cast<uint32_t*>(data + base) & 0x1FFFFFF;[span_66](start_span)[span_66](end_span)
            uint32_t size_fn = w1 & 0x7FFF;[span_67](start_span)[span_67](end_span)

            if (badge_offset == (size_t)-1 && length == std::strlen(FN_BADGE) && region_equals_ascii(data, size, abs_pos, FN_BADGE, length)) {[span_68](start_span)[span_68](end_span)
                badge_offset = offset; badge_size = size_fn;[span_69](start_span)[span_69](end_span)
                LOGI("[+] doesUserHaveGoldBadge @ %zu", badge_offset);[span_70](start_span)[span_70](end_span)
            } else if (payments_offset == (size_t)-1 && length == std::strlen(FN_PAYMENTS) && region_equals_ascii(data, size, abs_pos, FN_PAYMENTS, length)) {[span_71](start_span)[span_71](end_span)
                payments_offset = offset; payments_size = size_fn;[span_72](start_span)[span_72](end_span)
                LOGI("[+] PaymentsProvider @ %zu", payments_offset);[span_73](start_span)[span_73](end_span)
            } else if (camera_offset == (size_t)-1 && length == std::strlen(FN_CAMERA) && region_equals_ascii(data, size, abs_pos, FN_CAMERA, length)) {[span_74](start_span)[span_74](end_span)
                camera_offset = offset; camera_size = size_fn;[span_75](start_span)[span_75](end_span)
                LOGI("[+] getAndroidCameraVideoRecordingConfig @ %zu", camera_offset);[span_76](start_span)[span_76](end_span)
            }
        }
        if (badge_offset != (size_t)-1 && payments_offset != (size_t)-1 && camera_offset != (size_t)-1) {
            break;
        }
    }

    if (badge_offset == (size_t)-1 || payments_offset == (size_t)-1) {[span_77](start_span)[span_77](end_span)
        LOGE("[-] Thiếu hàm badge hoặc payments");[span_78](start_span)[span_78](end_span)
        return false;[span_79](start_span)[span_79](end_span)
    }

    // 3. Patch doesUserHaveGoldBadge
    LOGI("[*] Đang patch doesUserHaveGoldBadge...");[span_80](start_span)[span_80](end_span)
    if (badge_size < 4 || badge_offset + badge_size > size || data[badge_offset + badge_size - 2] != 0x5C) {[span_81](start_span)[span_81](end_span)
        LOGE("[-] Cấu trúc badge lỗi hoặc vượt giới hạn");[span_82](start_span)[span_82](end_span)
        return false;[span_83](start_span)[span_83](end_span)
    }
    uint8_t ret_reg = data[badge_offset + badge_size - 1];[span_84](start_span)[span_84](end_span)
    data[badge_offset] = 0x78;[span_85](start_span)[span_85](end_span)
    data[badge_offset + 1] = ret_reg;[span_86](start_span)[span_86](end_span)
    data[badge_offset + 2] = 0x5C;[span_87](start_span)[span_87](end_span)
    data[badge_offset + 3] = ret_reg;[span_88](start_span)[span_88](end_span)
    LOGI("[+] Đã patch badge thành công (ret_reg=%u)", ret_reg);[span_89](start_span)[span_89](end_span)

    // 4. Patch PaymentsProvider
    LOGI("[*] Đang patch PaymentsProvider...");[span_90](start_span)[span_90](end_span)
    uint16_t id_le = static_cast<uint16_t>(ent_id);[span_91](start_span)[span_91](end_span)
    bool patched_pay = false;
    if (payments_offset + payments_size <= size) {
        for (size_t i = 0; i + 12 <= payments_size; i++) {[span_92](start_span)[span_92](end_span)
            size_t pos = payments_offset + i;
            uint16_t val = *reinterpret_cast<uint16_t*>(data + pos + 4);
            if (val == id_le && data[pos + 6] == 0x0B && data[pos + 7] == data[pos + 8] &&[span_93](start_span)[span_93](end_span)
                data[pos + 9] == 0x0B && data[pos + 11] == data[pos + 7]) {[span_94](start_span)[span_94](end_span)
                uint8_t d1 = data[pos + 7], d2 = data[pos + 10];[span_95](start_span)[span_95](end_span)
                size_t at = pos + 6;[span_96](start_span)[span_96](end_span)
                data[at] = 0x78; data[at+1] = d2;[span_97](start_span)[span_97](end_span)
                data[at+2] = 0x79; data[at+3] = d1;[span_98](start_span)[span_98](end_span)
                data[at+4] = 0x78; data[at+5] = d2;[span_99](start_span)[span_99](end_span)
                LOGI("[+] PaymentsProvider patched @ %zu", at);[span_100](start_span)[span_100](end_span)
                patched_pay = true;[span_101](start_span)[span_101](end_span)
                break;[span_102](start_span)[span_102](end_span)
            }
        }
    }
    if (!patched_pay) {[span_103](start_span)[span_103](end_span)
        LOGE("[-] Không tìm thấy pattern PaymentsProvider");[span_104](start_span)[span_104](end_span)
        return false;[span_105](start_span)[span_105](end_span)
    }

    // 5. Patch maximumDuration (scan toàn file)
    LOGI("[*] Đang patch maximumDuration...");[span_106](start_span)[span_106](end_span)
    uint32_t candidates[] = {2500, 10000};[span_107](start_span)[span_107](end_span)
    uint32_t new_val = 2147000000;[span_108](start_span)[span_108](end_span)
    int patched_dur_count = 0;
    for (uint32_t target_val : candidates) {
        uint8_t old_b[4], new_b[4];
        std::memcpy(old_b, &target_val, 4);
        std::memcpy(new_b, &new_val, 4);

        size_t start = 0;
        while (start < size) {
            // Tìm kiếm chuỗi byte
            const uint8_t* found_ptr = std::search(data + start, data + size, old_b, old_b + 4);
            if (found_ptr == data + size) break;
            size_t idx = found_ptr - data;

            size_t left = (idx >= 64) ? idx - 64 : 0;[span_109](start_span)[span_109](end_span)
            size_t right = (idx + 4 + 64 <= size) ? idx + 4 + 64 : size;[span_110](start_span)[span_110](end_span)

            uint32_t m_i32 = 720;[span_111](start_span)[span_111](end_span)
            float m_f32 = 720.0f;[span_112](start_span)[span_112](end_span)
            double m_f64 = 720.0;[span_113](start_span)[span_113](end_span)
            bool has_marker = false;
            for (size_t m = left; m + 4 <= right; m++) {
                if (std::memcmp(data + m, &m_i32, 4) == 0 ||[span_114](start_span)[span_114](end_span)
                    std::memcmp(data + m, &m_f32, 4) == 0 ||[span_115](start_span)[span_115](end_span)
                    (m + 8 <= right && std::memcmp(data + m, &m_f64, 8) == 0)) {[span_116](start_span)[span_116](end_span)
                    has_marker = true;[span_117](start_span)[span_117](end_span)
                    break;[span_118](start_span)[span_118](end_span)
                }
            }
            if (has_marker) {[span_119](start_span)[span_119](end_span)
                std::memcpy(data + idx, new_b, 4);[span_120](start_span)[span_120](end_span)
                patched_dur_count++;[span_121](start_span)[span_121](end_span)
                LOGI("[+] duration patched value %u @ %zu", target_val, idx);[span_122](start_span)[span_122](end_span)
            }
            start = idx + 1;[span_123](start_span)[span_123](end_span)
        }
    }
    LOGI("[+] Tổng duration patched: %d", patched_dur_count);[span_124](start_span)[span_124](end_span)

    // 6. Bypass remote config camera
    LOGI("[*] Bypass remote config android_camera_video_recording...");[span_125](start_span)[span_125](end_span)
    bool found_cam = false;
    for (uint32_t i = 0; i < string_count; i++) {
        size_t abs_pos = 0, length = 0;
        bool is_utf16 = false;
        if (get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, i, abs_pos, length, is_utf16)) {
            if (is_utf16) continue;[span_126](start_span)[span_126](end_span)
            if (length == std::strlen(STR_CAMERA_CFG) && region_equals_ascii(data, size, abs_pos, STR_CAMERA_CFG, length)) {[span_127](start_span)[span_127](end_span)
                LOGI("[+] Tìm thấy string camera config @ %zu (id=%u)", abs_pos, i);[span_128](start_span)[span_128](end_span)
                std::memset(data + abs_pos, 0, length);[span_129](start_span)[span_129](end_span)
                LOGI("[+] Đã xóa nội dung string (force falsy)");[span_130](start_span)[span_130](end_span)
                found_cam = true;[span_131](start_span)[span_131](end_span)
                break;[span_132](start_span)[span_132](end_span)
            }
        }
    }
    if (!found_cam) {[span_133](start_span)[span_133](end_span)
        LOGW("[-] Không tìm thấy string remote config camera");[span_134](start_span)[span_134](end_span)
    }

    // 7. Cập nhật SHA1 ở 20 byte cuối
    LOGI("[*] Cập nhật SHA1 checksum...");[span_135](start_span)[span_135](end_span)
    if (size >= 20) {
        SHA1_CTX ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, data, size - 20);
        sha1_final(data + size - 20, &ctx);[span_136](start_span)[span_136](end_span)
        LOGI("[+] SHA1 updated thành công");[span_137](start_span)[span_137](end_span)
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
            
            // Cấp quyền ghi bộ nhớ
            uintptr_t page_size = sysconf(_SC_PAGESIZE);
            uintptr_t start_page = (uintptr_t)ptr & ~(page_size - 1);
            size_t len_page = (length + ((uintptr_t)ptr - start_page) + page_size - 1) & ~(page_size - 1);

            if (mprotect((void*)start_page, len_page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
                LOGW("mprotect failed, trying direct patch...");
            }

            if (patch_bundle((uint8_t*)ptr, length)) {
                g_bundle_patched = true;
                LOGI("Bundle successfully patched on the fly with all Python features!");
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
