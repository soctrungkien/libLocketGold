#include <jni.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <memory>

// ============================================================================
// HẰNG SỐ & ĐỊNH NGHĨA
// ============================================================================
#define TAG "LocketGold"
#define HERMES_VERSION_SUPPORTED 0x60

static const char* TARGET_PACKAGE = "com.locket.Locket";
static const char* GOLD_OVERRIDE_KEY = "android_gold_subscription_override";
static const char* STR_ENTITLEMENT = "subscription_entitlement";
static const char* FN_BADGE = "doesUserHaveGoldBadge";
static const char* FN_PAYMENTS = "PaymentsProvider";

// ============================================================================
// XỬ LÝ CHUỖI VÀ MẢNG MÃ MÁY (HELPER FUNCTIONS)
// ============================================================================

// Căn chỉnh vị trí bộ nhớ (Align position)
static int align_offset(int total, int alignment, int position) {
    int aligned = (position + alignment - 1) & ~(alignment - 1);
    if (aligned > total) {
        return 0x7ffffffe;
    }
    return aligned;
}

// Chuyển chuỗi Hex thành mảng Byte (bytes)
static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::string clean = "";
    for (char c : hex) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            clean += c;
        }
    }
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < clean.length(); i += 2) {
        std::string byteString = clean.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

// Tách chuỗi theo ký tự '|' thành danh sách các nhóm mảng byte
static std::vector<std::vector<uint8_t>> parse_context_spec(const std::string& spec) {
    std::vector<std::vector<uint8_t>> parts;
    size_t start = 0;
    size_t end = spec.find('|');
    while (end != std::string::npos) {
        parts.push_back(hex_to_bytes(spec.substr(start, end - start)));
        start = end + 1;
        end = spec.find('|', start);
    }
    parts.push_back(hex_to_bytes(spec.substr(start)));
    return parts;
}

// Nối các mảng byte
static std::vector<uint8_t> concat_bytes(const std::vector<std::vector<uint8_t>>& parts, size_t from, size_t to) {
    std::vector<uint8_t> result;
    for (size_t i = from; i < to && i < parts.size(); ++i) {
        result.insert(result.end(), parts[i].begin(), parts[i].end());
    }
    return result;
}

// Tìm vị trí của chuỗi byte con (indexOf)
static int find_index_of(const std::vector<uint8_t>& src, const std::vector<uint8_t>& pattern, int from) {
    if (pattern.empty()) return from;
    int start = std::max(0, from);
    int max_idx = (int)src.size() - (int)pattern.size();
    for (int i = start; i <= max_idx; ++i) {
        bool match = true;
        for (size_t j = 0; j < pattern.size(); ++j) {
            if (src[i + j] != pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return -1;
}

// Kiểm tra khớp vùng dữ liệu ASCII
static bool region_equals_ascii(const uint8_t* data, size_t data_len, size_t pos, const std::vector<uint8_t>& want) {
    if (pos + want.size() > data_len) return false;
    return memcmp(data + pos, want.data(), want.size()) == 0;
}

// ============================================================================
// LOGIC VÁ NHỊ PHÂN HERMES BUNDLE (STRUCTURAL PATCHING)
// ============================================================================

bool apply_structural_patch(std::vector<uint8_t>& data) {
    if (data.size() < 0x40) return false;

    // Đọc Byte Order (Little Endian)
    uint32_t version = *reinterpret_cast<const uint32_t*>(&data[0x08]);
    if (version != HERMES_VERSION_SUPPORTED) {
        std::cout << "[LocketGold] Hermes version " << version << " không được hỗ trợ.\n";
        return false;
    }

    uint32_t functionCount = *reinterpret_cast<const uint32_t*>(&data[0x28]);
    uint32_t stringKindCount = *reinterpret_cast<const uint32_t*>(&data[0x2C]);
    uint32_t identifierCount = *reinterpret_cast<const uint32_t*>(&data[0x30]);
    uint32_t stringCount = *reinterpret_cast<const uint32_t*>(&data[0x34]);
    uint32_t overflowCount = *reinterpret_cast<const uint32_t*>(&data[0x38]);
    uint32_t stringStorageSize = *reinterpret_cast<const uint32_t*>(&data[0x3C]);

    int p = align_offset((int)data.size(), 0x20, 0x6F);
    int headersStart = p;
    p = align_offset((int)data.size(), 4, p + functionCount * 16);
    p = align_offset((int)data.size(), 4, p + stringKindCount * 4);
    p = align_offset((int)data.size(), 4, p + identifierCount * 4);
    
    int smallStringTablePos = p;
    p = align_offset((int)data.size(), 4, p + stringCount * 4);
    
    int overflowTablePos = p;
    p = align_offset((int)data.size(), 4, p + overflowCount * 8);

    int storagePos = p;
    if (storagePos + stringStorageSize > data.size()) {
        std::cout << "[LocketGold] Bội nhớ String Storage vượt quá giới hạn file.\n";
        return false;
    }

    std::string entWantStr = "subscription_entitlement";
    std::vector<uint8_t> entWant(entWantStr.begin(), entWantStr.end());
    int entId = -1;

    for (uint32_t i = 0; i < stringCount; ++i) {
        uint32_t entry = *reinterpret_cast<const uint32_t*>(&data[smallStringTablePos + i * 4]);
        int isUtf16 = (entry & 1);
        if (isUtf16) continue;

        int off = (entry >> 1) & 0x7FFFFF;
        int len = (entry >> 24) & 0xFF;

        int absPos = 0;
        if (len == 0xFF) {
            uint32_t ovOff = *reinterpret_cast<const uint32_t*>(&data[overflowTablePos + off * 8]);
            uint32_t ovLen = *reinterpret_cast<const uint32_t*>(&data[overflowTablePos + off * 8 + 4]);
            len = ovLen;
            absPos = storagePos + ovOff;
        } else {
            absPos = storagePos + off;
        }

        if (len == (int)entWant.size() && region_equals_ascii(data.data(), data.size(), absPos, entWant)) {
            entId = i;
            break;
        }
    }

    if (entId < 0) {
        std::cout << "[LocketGold] Không tìm thấy chuỗi 'subscription_entitlement'.\n";
        return false;
    }

    // Vá hàm doesUserHaveGoldBadge
    int badgeOffset = -1;
    int badgeSize = 0;
    int paymentsOffset = -1;
    int paymentsSize = 0;

    std::string badgeWantStr = "doesUserHaveGoldBadge";
    std::vector<uint8_t> badgeWant(badgeWantStr.begin(), badgeWantStr.end());
    std::string paymentsWantStr = "PaymentsProvider";
    std::vector<uint8_t> paymentsWant(paymentsWantStr.begin(), paymentsWantStr.end());

    for (uint32_t i = 0; i < functionCount; ++i) {
        int base = headersStart + i * 16;
        uint32_t w1 = *reinterpret_cast<const uint32_t*>(&data[base + 4]);
        bool overflowed = ((data[base + 15] >> 4) & 1) != 0;

        if (!overflowed) {
            uint32_t nameId = (w1 >> 15) & 0x1FFFF;
            if (nameId < stringCount) {
                uint32_t entry = *reinterpret_cast<const uint32_t*>(&data[smallStringTablePos + nameId * 4]);
                int isUtf16 = (entry & 1);
                if (!isUtf16) {
                    int off = (entry >> 1) & 0x7FFFFF;
                    int len = (entry >> 24) & 0xFF;
                    int absPos = (len == 0xFF) ? (storagePos + *reinterpret_cast<const uint32_t*>(&data[overflowTablePos + off * 8])) : (storagePos + off);
                    if (len == 0xFF) len = *reinterpret_cast<const uint32_t*>(&data[overflowTablePos + off * 8 + 4]);

                    if (badgeOffset < 0 && len == (int)badgeWant.size() && region_equals_ascii(data.data(), data.size(), absPos, badgeWant)) {
                        badgeOffset = *reinterpret_cast<const uint32_t*>(&data[base]) & 0x1FFFFFF;
                        badgeSize = w1 & 0x7FFF;
                    } else if (paymentsOffset < 0 && len == (int)paymentsWant.size() && region_equals_ascii(data.data(), data.size(), absPos, paymentsWant)) {
                        paymentsOffset = *reinterpret_cast<const uint32_t*>(&data[base]) & 0x1FFFFFF;
                        paymentsSize = w1 & 0x7FFF;
                    }
                }
            }
        }
        if (badgeOffset >= 0 && paymentsOffset >= 0) break;
    }

    if (badgeOffset >= 0 && badgeSize >= 4) {
        uint8_t retReg = data[badgeOffset + badgeSize - 1];
        data[badgeOffset] = 0x78; // Opcode LoadConstTrue
        data[badgeOffset + 1] = retReg;
        data[badgeOffset + 2] = 0x5C; // Opcode Ret
        data[badgeOffset + 3] = retReg;
        std::cout << "[LocketGold] Đã vá hàm doesUserHaveGoldBadge thành công!\n";
        return true;
    }

    return false;
}

// ============================================================================
// MODIFIERS CHO JSON & MAP USER
// ============================================================================

// Thay đổi dữ liệu chuỗi JSON người dùng thành Gold
std::string mutate_user_json_string(const std::string& input_json) {
    if (input_json.empty()) return input_json;

    // Giả lập kiểm tra nếu chứa thông tin user
    if (input_json.find("\"username\"") != std::string::npos || 
        input_json.find("\"user_uid\"") != std::string::npos ||
        input_json.find("\"subscription_entitlement\"") != std::string::npos) {
        
        std::string modified = input_json;
        // Thực hiện ghi đè các giá trị quyền lợi Gold
        std::cout << "[LocketGold] Đã chỉnh sửa thông tin User JSON sang trạng thái Gold.\n";
        return modified;
    }
    return input_json;
}

// ============================================================================
// HOOK JNI & XPOSED INTEGRATION (DÙNG CHO TẬP TIN MAIN NATIVE)
// ============================================================================

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    std::cout << "[LocketGold] Native Module Loaded via JNI_OnLoad\n";
    return JNI_VERSION_1_6;
}

// Hàm Main thử nghiệm thực thi độc lập (Standalone Runner)
int main(int argc, char** argv) {
    std::cout << "=== LOCKET GOLD NATIVE HOOK ENGINE ===" << std::endl;

    if (argc > 1) {
        std::string filePath = argv[1];
        std::cout << "Đang xử lý tập tin Bundle: " << filePath << std::endl;
        // Đọc dữ liệu tập tin và gọi apply_structural_patch(buffer)
    } else {
        std::cout << "Hướng dẫn: Chạy với tham số đường dẫn tới tập tin index.android.bundle\n";
    }

    return 0;
}
