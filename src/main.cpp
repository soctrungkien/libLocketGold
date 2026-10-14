#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include "dobby.h" // Thêm header của Dobby Hook

#define LOG_TAG "LocketGold"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static constexpr uint32_t HERMES_VERSION = 96;
static constexpr uint32_t HERMES_MAGIC = 0x03BC1FC6;

namespace sha1 {
    typedef struct {
        uint32_t state[5];
        uint32_t count[2];
        uint8_t buffer[64];
    } context;

    static uint32_t rol(uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    static void transform(uint32_t state[5], const uint8_t buffer[64]) {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)buffer[i*4] << 24) | ((uint32_t)buffer[i*4+1] << 16) |
                   ((uint32_t)buffer[i*4+2] << 8) | (uint32_t)buffer[i*4+3];
        }
        for (int i = 16; i < 80; i++) {
            uint32_t t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = rol(t, 1);
        }

        auto f0 = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); };
        auto f1 = [](uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; };
        auto f2 = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (x & z) | (y & z); };
        auto f3 = [](uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; };

        #define ROUND(v, w_reg, x, y, z, f, k, i) \
            z += rol(v, 5) + f(w_reg, x, y) + k + w[i]; \
            w_reg = rol(w_reg, 30);

        int i = 0;
        while (i < 20) {
            ROUND(a, b, c, d, e, f0, 0x5A827999, i++);
            ROUND(e, a, b, c, d, f0, 0x5A827999, i++);
            ROUND(d, e, a, b, c, f0, 0x5A827999, i++);
            ROUND(c, d, e, a, b, f0, 0x5A827999, i++);
            ROUND(b, c, d, e, a, f0, 0x5A827999, i++);
        }
        while (i < 40) {
            ROUND(a, b, c, d, e, f1, 0x6ED9EBA1, i++);
            ROUND(e, a, b, c, d, f1, 0x6ED9EBA1, i++);
            ROUND(d, e, a, b, c, f1, 0x6ED9EBA1, i++);
            ROUND(c, d, e, a, b, f1, 0x6ED9EBA1, i++);
            ROUND(b, c, d, e, a, f1, 0x6ED9EBA1, i++);
        }
        while (i < 60) {
            ROUND(a, b, c, d, e, f2, 0x8F1BBCDC, i++);
            ROUND(e, a, b, c, d, f2, 0x8F1BBCDC, i++);
            ROUND(d, e, a, b, c, f2, 0x8F1BBCDC, i++);
            ROUND(c, d, e, a, b, f2, 0x8F1BBCDC, i++);
            ROUND(b, c, d, e, a, f2, 0x8F1BBCDC, i++);
        }
        while (i < 80) {
            ROUND(a, b, c, d, e, f3, 0xCA62C1D6, i++);
            ROUND(e, a, b, c, d, f3, 0xCA62C1D6, i++);
            ROUND(d, e, a, b, c, f3, 0xCA62C1D6, i++);
            ROUND(c, d, e, a, b, f3, 0xCA62C1D6, i++);
            ROUND(b, c, d, e, a, f3, 0xCA62C1D6, i++);
        }
        #undef ROUND

        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
    }

    static void init(context* context) {
        context->state[0] = 0x67452301;
        context->state[1] = 0xEFCDAB89;
        context->state[2] = 0x98BADCFE;
        context->state[3] = 0x10325476;
        context->state[4] = 0xC3D2E1F0;
        context->count[0] = context->count[1] = 0;
    }

    static void update(context* context, const uint8_t* data, size_t len) {
        size_t i, j;
        j = (context->count[0] >> 3) & 63;
        if ((context->count[0] += (uint32_t)len << 3) < ((uint32_t)len << 3)) context->count[1]++;
        context->count[1] += (uint32_t)(len >> 29);
        if ((j + len) > 63) {
            std::memcpy(&context->buffer[j], data, (i = 64 - j));
            transform(context->state, context->buffer);
            for (; i + 63 < len; i += 64) transform(context->state, &data[i]);
            j = 0;
        } else i = 0;
        std::memcpy(&context->buffer[j], &data[i], len - i);
    }

    static void final(uint8_t digest[20], context* context) {
        uint32_t i;
        uint8_t finalcount[8];
        for (i = 0; i < 8; i++) {
            finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
        }
        update(context, (uint8_t*)"\x80", 1);
        while ((context->count[0] & 504) != 448) update(context, (uint8_t*)"\0", 1);
        update(context, finalcount, 8);
        for (i = 0; i < 20; i++) {
            digest[i] = (unsigned char)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
        }
    }
}

static size_t get_page_size() {
    return sysconf(_SC_PAGESIZE);
}

static bool unprotect(void* addr, size_t len) {
    size_t page_size = get_page_size();
    uintptr_t page = (uintptr_t)addr & ~(page_size - 1);
    size_t size = ((uintptr_t)addr + len - page + page_size - 1) & ~(page_size - 1);
    return mprotect((void*)page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

static bool protect(void* addr, size_t len) {
    size_t page_size = get_page_size();
    uintptr_t page = (uintptr_t)addr & ~(page_size - 1);
    size_t size = ((uintptr_t)addr + len - page + page_size - 1) & ~(page_size - 1);
    return mprotect((void*)page, size, PROT_READ | PROT_EXEC) == 0;
}

struct ScopedProtect {
    void* addr;
    size_t len;
    bool success;
    ScopedProtect(void* p, size_t l) : addr(p), len(l) {
        success = unprotect(addr, len);
    }
    ~ScopedProtect() {
        if (success) {
            protect(addr, len);
        }
    }
};

static uint32_t align(uint32_t total, uint32_t alignment, uint32_t pos) {
    uint32_t aligned = (pos + alignment - 1) & ~(alignment - 1);
    if (aligned > total) return 0x7FFFFFFE;
    return aligned;
}

static bool region_eq(const uint8_t* data, size_t size, uint32_t pos, const char* want) {
    size_t len = strlen(want);
    if (pos + len > size) return false;
    return memcmp(data + pos, want, len) == 0;
}

struct StringInfo {
    uint32_t abs_pos;
    uint32_t length;
    bool is_utf16;
};

static StringInfo get_string_info(const uint8_t* data, size_t size,
                                  uint32_t small_tbl, uint32_t overflow_tbl,
                                  uint32_t storage, uint32_t str_count, uint32_t idx) {
    StringInfo info{0, 0, false};
    if (idx >= str_count) return info;
    if (small_tbl + idx * 4 + 4 > size) return info;

    uint32_t entry = *(uint32_t*)(data + small_tbl + idx * 4);
    bool is_utf16 = entry & 1;
    if (is_utf16) {
        info.is_utf16 = true;
        return info;
    }

    uint32_t off = (entry >> 1) & 0x7FFFFF;
    uint32_t length = (entry >> 24) & 0xFF;

    if (length == 0xFF) {
        if (overflow_tbl + off * 8 + 8 > size) return info;
        uint32_t ov = *(uint32_t*)(data + overflow_tbl + off * 8);
        length = *(uint32_t*)(data + overflow_tbl + off * 8 + 4);
        info.abs_pos = storage + ov;
    } else {
        info.abs_pos = storage + off;
    }
    if (info.abs_pos + length > size) return info;
    info.length = length;
    return info;
}

static bool patch_bundle(uint8_t* data, size_t size) {
    if (size < 128) return false;
    if (*(uint32_t*)data != HERMES_MAGIC) return false;

    uint32_t version = *(uint32_t*)(data + 8);
    if (version != HERMES_VERSION) {
        LOGI("Hermes version %u (expected %u)", version, HERMES_VERSION);
    }

    uint32_t function_count      = *(uint32_t*)(data + 40);
    uint32_t string_kind_count   = *(uint32_t*)(data + 44);
    uint32_t identifier_count    = *(uint32_t*)(data + 48);
    uint32_t string_count        = *(uint32_t*)(data + 52);
    uint32_t overflow_count      = *(uint32_t*)(data + 56);
    uint32_t string_storage_size = *(uint32_t*)(data + 60);

    LOGI("functions=%u strings=%u", function_count, string_count);

    uint32_t p = align(size, 32, 111);
    uint32_t headers_start = p;
    p = align(size, 4, p + function_count * 16);
    p = align(size, 4, p + string_kind_count * 4);
    p = align(size, 4, p + identifier_count * 4);
    uint32_t small_string_table_pos = p;
    p = align(size, 4, p + string_count * 4);
    uint32_t overflow_table_pos = p;
    p = align(size, 4, p + overflow_count * 8);
    uint32_t storage_pos = p;

    if (p + string_storage_size > size) {
        LOGE("string storage overflow");
        return false;
    }

    // 1. subscription_entitlement
    int32_t ent_id = -1;
    for (uint32_t i = 0; i < string_count; i++) {
        auto info = get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, i);
        if (info.is_utf16 || info.length == 0) continue;
        if (info.length == 23 && region_eq(data, size, info.abs_pos, "subscription_entitlement")) {
            ent_id = (int32_t)i;
            LOGI("entitlement id=%d", ent_id);
            break;
        }
    }
    if (ent_id < 0) {
        LOGE("entitlement not found");
        return false;
    }

    // 2. tìm hàm
    int32_t badge_off = -1, badge_size = -1;
    int32_t pay_off = -1, pay_size = -1;
    int32_t cam_off = -1, cam_size = -1;

    for (uint32_t i = 0; i < function_count; i++) {
        uint32_t base = headers_start + i * 16;
        if (base + 16 > size) continue;
        uint32_t w1 = *(uint32_t*)(data + base + 4);
        if (((data[base + 15] >> 4) & 1) != 0) continue;

        uint32_t name_id = (w1 >> 15) & 0x1FFFF;
        if (name_id >= string_count) continue;

        auto info = get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, name_id);
        if (info.is_utf16 || info.length == 0) continue;

        uint32_t offset = *(uint32_t*)(data + base) & 0x1FFFFFF;
        uint32_t fsize  = w1 & 0x7FFF;
        if (offset + fsize > size) continue;

        if (badge_off < 0 && info.length == 20 && region_eq(data, size, info.abs_pos, "doesUserHaveGoldBadge")) {
            badge_off = offset; badge_size = fsize;
            LOGI("doesUserHaveGoldBadge @ %u", badge_off);
        }
        else if (pay_off < 0 && info.length == 16 && region_eq(data, size, info.abs_pos, "PaymentsProvider")) {
            pay_off = offset; pay_size = fsize;
            LOGI("PaymentsProvider @ %u", pay_off);
        }
        else if (cam_off < 0 && info.length == 35 && region_eq(data, size, info.abs_pos, "getAndroidCameraVideoRecordingConfig")) {
            cam_off = offset; cam_size = fsize;
            LOGI("getAndroidCameraVideoRecordingConfig @ %u", cam_off);
        }

        if (badge_off >= 0 && pay_off >= 0 && cam_off >= 0) break;
    }

    if (badge_off < 0 || pay_off < 0) {
        LOGE("missing badge/payments");
        return false;
    }

    // 3. patch badge
    if (badge_size < 4 || badge_off + badge_size > size || data[badge_off + badge_size - 2] != 0x5C) {
        LOGE("badge structure invalid");
        return false;
    }
    uint8_t ret_reg = data[badge_off + badge_size - 1];
    {
        ScopedProtect sp(data + badge_off, 4);
        if (!sp.success) return false;
        data[badge_off + 0] = 0x78;
        data[badge_off + 1] = ret_reg;
        data[badge_off + 2] = 0x5C;
        data[badge_off + 3] = ret_reg;
    }
    LOGI("badge patched (reg=%u)", ret_reg);

    // 4. patch PaymentsProvider
    if (pay_off + pay_size > size) return false;
    uint8_t id_le[2] = {(uint8_t)(ent_id & 0xFF), (uint8_t)((ent_id >> 8) & 0xFF)};
    bool patched_pay = false;
    for (int i = 0; i < pay_size - 12; i++) {
        if (data[pay_off + i + 4] == id_le[0] && data[pay_off + i + 5] == id_le[1] &&
            data[pay_off + i + 6] == 0x0B &&
            data[pay_off + i + 7] == data[pay_off + i + 8] &&
            data[pay_off + i + 9] == 0x0B &&
            data[pay_off + i + 11] == data[pay_off + i + 7]) {

            uint8_t d1 = data[pay_off + i + 7];
            uint8_t d2 = data[pay_off + i + 10];
            uint32_t at = pay_off + i + 6;

            ScopedProtect sp(data + at, 6);
            if (!sp.success) return false;
            data[at + 0] = 0x78;
            data[at + 1] = d2;
            data[at + 2] = 0x79;
            data[at + 3] = d1;
            data[at + 4] = 0x78;
            data[at + 5] = d2;
            LOGI("PaymentsProvider patched @ %u", at);
            patched_pay = true;
            break;
        }
    }
    if (!patched_pay) {
        LOGE("PaymentsProvider pattern not found");
        return false;
    }

    // 5. patch duration
    uint32_t candidates[][2] = {{2500, 2147000000}, {10000, 2147000000}};
    int patched = 0;

    for (auto& c : candidates) {
        uint32_t old_v = c[0], new_v = c[1];
        for (size_t idx = 0; idx + 4 <= size; idx++) {
            if (*(uint32_t*)(data + idx) != old_v) continue;

            bool near = false;
            for (int k = -64; k < 64; k++) {
                if ((int)idx + k < 0 || idx + k + 4 > size) continue;
                uint32_t val32 = *(uint32_t*)(data + idx + k);
                float val_f = *(float*)(data + idx + k);
                if (val32 == 720 || val_f == 720.0f) {
                    near = true;
                    break;
                }
                if (idx + k + 8 <= size) {
                    double val_d = *(double*)(data + idx + k);
                    if (val_d == 720.0) {
                        near = true;
                        break;
                    }
                }
            }
            if (near) {
                ScopedProtect sp(data + idx, 4);
                if (sp.success) {
                    *(uint32_t*)(data + idx) = new_v;
                    patched++;
                    LOGI("duration %u → %u @ %zu", old_v, new_v, idx);
                }
            }
        }
    }
    LOGI("duration patched: %d", patched);

    // 6. null android_camera_video_recording
    for (uint32_t i = 0; i < string_count; i++) {
        auto info = get_string_info(data, size, small_string_table_pos, overflow_table_pos, storage_pos, string_count, i);
        if (info.is_utf16 || info.length == 0) continue;
        if (info.length == 31 && region_eq(data, size, info.abs_pos, "android_camera_video_recording")) {
            ScopedProtect sp(data + info.abs_pos, info.length);
            if (sp.success) {
                memset(data + info.abs_pos, 0, info.length);
                LOGI("camera string nulled @ %u", info.abs_pos);
            }
            break;
        }
    }

    // 7. Update SHA1 (last 20 bytes)
    if (size >= 20) {
        sha1::context ctx;
        sha1::init(&ctx);
        sha1::update(&ctx, data, size - 20);
        uint8_t digest[20];
        sha1::final(digest, &ctx);

        ScopedProtect sp(data + size - 20, 20);
        if (sp.success) {
            std::memcpy(data + size - 20, digest, 20);
            LOGI("SHA1 updated");
        }
    }

    LOGI("Full patch done");
    return true;
}

static void* find_hermes_bundle(size_t* out_size) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        uintptr_t start = 0, end = 0;
        char perms[8] = {};
        if (sscanf(line.c_str(), "%lx-%lx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] != 'r') continue;

        size_t sz = end - start;
        if (sz < 0x100000 || sz > 0x20000000) continue;

        uint8_t* p = (uint8_t*)start;
        if (*(uint32_t*)p == HERMES_MAGIC) {
            *out_size = sz;
            return p;
        }
        for (size_t off = 0; off + 4 < sz && off < 0x100000; off += 0x1000) {
            if (*(uint32_t*)(p + off) == HERMES_MAGIC) {
                *out_size = sz - off;
                return p + off;
            }
        }
    }
    return nullptr;
}

static void do_patch() {
    std::thread([]() {
        size_t size = 0;
        uint8_t* bundle = nullptr;
        
        // Loop chờ đến khi Hermes bundle xuất hiện trong memory
        while (!bundle) {
            bundle = (uint8_t*)find_hermes_bundle(&size);
            if (!bundle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        LOGI("Hermes bundle found @ %p size=%zu", bundle, size);

        if (!patch_bundle(bundle, size)) {
            LOGE("Patch failed");
        } else {
            LOGI("All features patched successfully");
        }
    }).detach();
}

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("LocketGold loaded with Dobby");
    do_patch();
    return JNI_VERSION_1_6;
}
