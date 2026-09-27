#include <jni.h>
#include <android/log.h>
#include <dobby.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <memory>
#include <algorithm>
#include <mutex>

#define LOG_TAG "LocketGoldNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char* TARGET_PACKAGE = "com.locket.Locket";
static const char* BUNDLE_ASSET = "index.android.bundle";
static const char* STOCK_BUNDLE_ASSET_URL = "assets://index.android.bundle";
static const char* GOLD_OVERRIDE_KEY = "android_gold_subscription_override";
static const int HERMES_VERSION_SUPPORTED = 96;

static std::string g_patchedBundlePath = "";
static std::mutex g_patchMutex;

// ==================== SHA-1 IMPLEMENTATION ====================

struct SHA1_CTX {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
};

static void SHA1Transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) | (buffer[i * 4 + 2] << 8) | (buffer[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
        uint32_t val = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (val << 1) | (val >> 31);
    }
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void SHA1Init(SHA1_CTX* context) {
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}

static void SHA1Update(SHA1_CTX* context, const uint8_t* data, uint32_t len) {
    uint32_t i, j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += len << 3) < (len << 3)) context->count[1]++;
    context->count[1] += (len >> 29);
    if ((j + len) > 63) {
        memcpy(&context->buffer[j], data, (i = 64 - j));
        SHA1Transform(context->state, context->buffer);
        for (; i + 63 < len; i += 64) {
            SHA1Transform(context->state, &data[i]);
        }
        j = 0;
    } else {
        i = 0;
    }
    memcpy(&context->buffer[j], &data[i], len - i);
}

static void SHA1Final(uint8_t digest[20], SHA1_CTX* context) {
    uint8_t finalcount[8];
    for (int i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    uint8_t c = 0200;
    SHA1Update(context, &c, 1);
    while ((context->count[0] & 504) != 448) {
        c = 0;
        SHA1Update(context, &c, 1);
    }
    SHA1Update(context, finalcount, 8);
    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

// ==================== HERMES STRUCTURAL PATCH ====================

static int align_offset(size_t total, size_t alignment, size_t position) {
    size_t aligned = (position + alignment - 1) & ~(alignment - 1);
    if (aligned > total) return 0x7FFFFFFF;
    return static_cast<int>(aligned);
}

static bool regionEqualsAscii(const uint8_t* data, size_t dataLen, size_t pos, const char* want) {
    size_t wantLen = strlen(want);
    if (pos + wantLen > dataLen) return false;
    return memcmp(data + pos, want, wantLen) == 0;
}

static bool structuralPatchHermes(uint8_t* data, size_t size) {
    if (size < 111) return false;

    uint32_t version = *reinterpret_cast<uint32_t*>(data + 8);
    if (version != HERMES_VERSION_SUPPORTED) {
        LOGI("Structural patch: Hermes version %u not supported (expected %d)", version, HERMES_VERSION_SUPPORTED);
        return false;
    }

    uint32_t functionCount = *reinterpret_cast<uint32_t*>(data + 40);
    uint32_t stringKindCount = *reinterpret_cast<uint32_t*>(data + 44);
    uint32_t identifierCount = *reinterpret_cast<uint32_t*>(data + 48);
    uint32_t stringCount = *reinterpret_cast<uint32_t*>(data + 52);
    uint32_t overflowCount = *reinterpret_cast<uint32_t*>(data + 56);
    uint32_t stringStorageSize = *reinterpret_cast<uint32_t*>(data + 60);

    size_t p = align_offset(size, 32, 111);
    size_t headersStart = p;
    p = align_offset(size, 4, p + functionCount * 16);
    p = align_offset(size, 4, p + stringKindCount * 4);
    p = align_offset(size, 4, p + identifierCount * 4);
    size_t smallStringTablePos = p;
    p = align_offset(size, 4, p + stringCount * 4);
    size_t overflowTablePos = p;
    p = align_offset(size, 4, p + overflowCount * 8);
    size_t storagePos = p;

    if (p + stringStorageSize > size) {
        LOGE("Structural patch: string storage out of bounds");
        return false;
    }

    const char* STR_ENTITLEMENT = "subscription_entitlement";
    int entId = -1;

    for (uint32_t i = 0; i < stringCount; i++) {
        uint32_t entry = *reinterpret_cast<uint32_t*>(data + smallStringTablePos + i * 4);
        uint32_t isUtf16 = entry & 1;
        if (isUtf16 != 0) continue;

        uint32_t off = (entry >> 1) & 0x7FFFFF;
        uint32_t len = (entry >> 24) & 0xFF;
        size_t abs_pos = 0;

        if (len == 0xFF) {
            uint32_t ov = *reinterpret_cast<uint32_t*>(data + overflowTablePos + off * 8);
            len = *reinterpret_cast<uint32_t*>(data + overflowTablePos + off * 8 + 4);
            abs_pos = storagePos + ov;
        } else {
            abs_pos = storagePos + off;
        }

        if (len == strlen(STR_ENTITLEMENT) && regionEqualsAscii(data, size, abs_pos, STR_ENTITLEMENT)) {
            entId = static_cast<int>(i);
            break;
        }
    }

    if (entId < 0) {
        LOGE("Structural patch: 'subscription_entitlement' string not found");
        return false;
    }

    const char* FN_BADGE = "doesUserHaveGoldBadge";
    const char* FN_PAYMENTS = "PaymentsProvider";

    int badgeOffset = -1, badgeSize = 0;
    int paymentsOffset = -1, paymentsSize = 0;

    for (uint32_t i = 0; i < functionCount; i++) {
        size_t base = headersStart + i * 16;
        uint32_t w1 = *reinterpret_cast<uint32_t*>(data + base + 4);
        bool overflowed = ((data[base + 15] >> 4) & 1) != 0;
        if (overflowed) continue;

        uint32_t nameId = (w1 >> 15) & 0x1FFFF;
        if (nameId >= stringCount) continue;

        uint32_t entry = *reinterpret_cast<uint32_t*>(data + smallStringTablePos + nameId * 4);
        uint32_t isUtf16 = entry & 1;
        if (isUtf16 != 0) continue;

        uint32_t off = (entry >> 1) & 0x7FFFFF;
        uint32_t len = (entry >> 24) & 0xFF;
        size_t abs_pos = 0;

        if (len == 0xFF) {
            uint32_t ov = *reinterpret_cast<uint32_t*>(data + overflowTablePos + off * 8);
            len = *reinterpret_cast<uint32_t*>(data + overflowTablePos + off * 8 + 4);
            abs_pos = storagePos + ov;
        } else {
            abs_pos = storagePos + off;
        }

        if (badgeOffset < 0 && len == strlen(FN_BADGE) && regionEqualsAscii(data, size, abs_pos, FN_BADGE)) {
            badgeOffset = *reinterpret_cast<uint32_t*>(data + base) & 0x1FFFFFF;
            badgeSize = w1 & 0x7FFF;
        } else if (paymentsOffset < 0 && len == strlen(FN_PAYMENTS) && regionEqualsAscii(data, size, abs_pos, FN_PAYMENTS)) {
            paymentsOffset = *reinterpret_cast<uint32_t*>(data + base) & 0x1FFFFFF;
            paymentsSize = w1 & 0x7FFF;
        }

        if (badgeOffset >= 0 && paymentsOffset >= 0) break;
    }

    if (badgeOffset < 0 || paymentsOffset < 0) {
        LOGE("Structural patch: target functions not found");
        return false;
    }

    // Patch 1: doesUserHaveGoldBadge -> LoadConstTrue r0; Ret r0
    if (badgeSize >= 4 && data[badgeOffset + badgeSize - 2] == 0x5C) {
        uint8_t retReg = data[badgeOffset + badgeSize - 1];
        data[badgeOffset] = 0x78;         // LoadConstTrue retReg
        data[badgeOffset + 1] = retReg;
        data[badgeOffset + 2] = 0x5C;     // Ret retReg
        data[badgeOffset + 3] = retReg;
        LOGI("Structural patch doesUserHaveGoldBadge applied at 0x%x", badgeOffset);
    }

    // Patch 2: PaymentsProvider -> Force hasGoldSubscription to true
    uint8_t idLe[2] = { static_cast<uint8_t>(entId & 0xFF), static_cast<uint8_t>((entId >> 8) & 0xFF) };
    for (int i = 0; i <= paymentsSize - 12; i++) {
        size_t idx = paymentsOffset + i;
        if (data[idx + 4] == idLe[0] && data[idx + 5] == idLe[1] &&
            data[idx + 6] == 0x0B && data[idx + 7] == data[idx + 8] &&
            data[idx + 9] == 0x0B && data[idx + 11] == data[idx + 7]) {

            uint8_t d1 = data[idx + 7];
            uint8_t d2 = data[idx + 10];
            size_t at = idx + 6;

            data[at] = 0x78;          // LoadConstTrue d2
            data[at + 1] = d2;
            data[at + 2] = 0x79;      // LoadConstFalse d1
            data[at + 3] = d1;
            data[at + 4] = 0x78;      // LoadConstTrue d2
            data[at + 5] = d2;

            LOGI("Structural patch hasGoldSubscription applied at 0x%zx", at);
            return true;
        }
    }

    return false;
}

static void updateHermesFooter(uint8_t* data, size_t size) {
    if (size < 20) return;
    SHA1_CTX ctx;
    SHA1Init(&ctx);
    SHA1Update(&ctx, data, size - 20);
    uint8_t digest[20];
    SHA1Final(digest, &ctx);
    memcpy(data + size - 20, digest, 20);
}

// ==================== DOBBY SYSTEM HOOKS ====================

typedef int (*orig_openat_t)(int dirfd, const char *pathname, int flags, mode_t mode);
static orig_openat_t orig_openat = nullptr;

static int my_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (pathname != nullptr && strstr(pathname, BUNDLE_ASSET) != nullptr) {
        std::lock_guard<std::mutex> lock(g_patchMutex);
        if (!g_patchedBundlePath.empty() && strstr(pathname, g_patchedBundlePath.c_str()) == nullptr) {
            LOGI("Redirecting openat(%s) -> %s", pathname, g_patchedBundlePath.c_str());
            return orig_openat(dirfd, g_patchedBundlePath.c_str(), flags, mode);
        }
    }
    return orig_openat(dirfd, pathname, flags, mode);
}

static void installDobbyHooks() {
    void* openat_ptr = DobbySymbolResolver(nullptr, "openat");
    if (openat_ptr != nullptr) {
        DobbyHook(openat_ptr, (dobby_dummy_func_t)my_openat, (dobby_dummy_func_t*)&orig_openat);
        LOGI("Successfully hooked openat via Dobby");
    } else {
        LOGE("Failed to resolve symbol openat");
    }
}

// ==================== JNI HELPER HOOKS ====================

static void mutateUserJsonObject(JNIEnv* env, jobject jsonObject) {
    if (jsonObject == nullptr) return;

    jclass jsonClass = env->GetObjectClass(jsonObject);
    jmethodID putStringMethod = env->GetMethodID(jsonClass, "put", "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;");
    jmethodID putBoolMethod = env->GetMethodID(jsonClass, "put", "(Ljava/lang/String;Z)Lorg/json/JSONObject;");

    if (putStringMethod && putBoolMethod) {
        jstring kBadge = env->NewStringUTF("badge");
        jstring vBadge = env->NewStringUTF("locket_gold");
        jstring kEnt = env->NewStringUTF("subscription_entitlement");
        jstring vEnt = env->NewStringUTF("locket_gold");
        jstring kStore = env->NewStringUTF("subscription_store");
        jstring vStore = env->NewStringUTF("play_store");
        jstring kGold = env->NewStringUTF("is_gold");

        env->CallObjectMethod(jsonObject, putStringMethod, kBadge, vBadge);
        env->CallObjectMethod(jsonObject, putStringMethod, kEnt, vEnt);
        env->CallObjectMethod(jsonObject, putStringMethod, kStore, vStore);
        env->CallObjectMethod(jsonObject, putBoolMethod, kGold, JNI_TRUE);

        env->DeleteLocalRef(kBadge); env->DeleteLocalRef(vBadge);
        env->DeleteLocalRef(kEnt);   env->DeleteLocalRef(vEnt);
        env->DeleteLocalRef(kStore); env->DeleteLocalRef(vStore);
        env->DeleteLocalRef(kGold);
    }
}

static void processAssetPatch(JNIEnv* env, jobject context) {
    if (context == nullptr) return;

    jclass contextClass = env->GetObjectClass(context);
    jmethodID getFilesDir = env->GetMethodID(contextClass, "getFilesDir", "()Ljava/io/File;");
    jobject filesDirObj = env->CallObjectMethod(context, getFilesDir);

    if (!filesDirObj) return;

    jclass fileClass = env->GetObjectClass(filesDirObj);
    jmethodID getAbsolutePath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
    jstring pathStr = (jstring)env->CallObjectMethod(filesDirObj, getAbsolutePath);

    const char* rawPath = env->GetStringUTFChars(pathStr, nullptr);
    std::string outPath = std::string(rawPath) + "/locket_gold_patched_v5.bin";
    env->ReleaseStringUTFChars(pathStr, rawPath);

    jmethodID getAssets = env->GetMethodID(contextClass, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assetManager = env->CallObjectMethod(context, getAssets);

    if (!assetManager) return;

    jclass assetManagerClass = env->GetObjectClass(assetManager);
    jmethodID openAsset = env->GetMethodID(assetManagerClass, "open", "(Ljava/lang/String;)Ljava/io/InputStream;");
    jstring bundleName = env->NewStringUTF(BUNDLE_ASSET);
    jobject inputStream = env->CallObjectMethod(assetManager, openAsset, bundleName);
    env->DeleteLocalRef(bundleName);

    if (!inputStream) return;

    jclass inputStreamClass = env->GetObjectClass(inputStream);
    jmethodID readMethod = env->GetMethodID(inputStreamClass, "read", "([B)I");
    jmethodID closeMethod = env->GetMethodID(inputStreamClass, "close", "()V");

    std::vector<uint8_t> buffer;
    jbyteArray tempArray = env->NewByteArray(8192);

    while (true) {
        jint bytesRead = env->CallIntMethod(inputStream, readMethod, tempArray);
        if (bytesRead <= 0) break;
        jbyte* bytes = env->GetByteArrayElements(tempArray, nullptr);
        buffer.insert(buffer.end(), bytes, bytes + bytesRead);
        env->ReleaseByteArrayElements(tempArray, bytes, JNI_ABORT);
    }

    env->CallVoidMethod(inputStream, closeMethod);
    env->DeleteLocalRef(tempArray);

    if (!buffer.empty()) {
        if (structuralPatchHermes(buffer.data(), buffer.size())) {
            updateHermesFooter(buffer.data(), buffer.size());
            std::ofstream outFile(outPath, std::ios::binary);
            outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
            outFile.close();

            std::lock_guard<std::mutex> lock(g_patchMutex);
            g_patchedBundlePath = outPath;
            LOGI("Patched Hermes bundle written natively to %s", outPath.c_str());
        }
    }
}

// ==================== JNI_OnLoad ENTRY ====================

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("LocketGold Native .so loading via JNI_OnLoad (ARM64 Non-Root)...");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    // 1. Khởi tạo Dobby Hook cho hệ thống native openat
    installDobbyHooks();

    // 2. Tìm kiếm Application context để thực hiện patch bundle
    jclass activityThreadCls = env->FindClass("android/app/ActivityThread");
    if (activityThreadCls) {
        jmethodID currentAppMethod = env->GetStaticMethodID(activityThreadCls, "currentApplication", "()Landroid/app/Application;");
        if (currentAppMethod) {
            jobject appObj = env->CallStaticObjectMethod(activityThreadCls, currentAppMethod);
            if (appObj) {
                processAssetPatch(env, appObj);
            }
        }
    }

    return JNI_VERSION_1_6;
}
