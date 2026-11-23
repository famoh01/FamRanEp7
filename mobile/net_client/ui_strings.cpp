// mobile/net_client/ui_strings.cpp
// แยกวิเคราะห์ข้อความ UI ไทยจาก /api/ui/strings (ต้นทาง: ไฟล์จริงใน Google Drive, ThaiPack)
#include "ui_strings.h"

#include <cstdio>
#include <cstdlib>

namespace mobile {
namespace net {

namespace {

// ถอด escape ของ JSON string เป็น UTF-8 (ข้อความไทยส่วนใหญ่เป็น UTF-8 ตรง ๆ,
// \\uXXXX เกิดเมื่อ server serialize ด้วย ensure_ascii)
std::string Unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out.push_back(in[i]);
            continue;
        }
        const char c = in[++i];
        switch (c) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'u': {
                if (i + 4 >= in.size()) break;
                const std::string hex = in.substr(i + 1, 4);
                char* end = nullptr;
                const long cp = std::strtol(hex.c_str(), &end, 16);
                if (end == hex.c_str()) break;
                i += 4;
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: out.push_back(c); break;
        }
    }
    return out;
}

// อ่าน JSON string ที่เริ่มที่ s[i] == '"' ; คืนตำแหน่งหลังเครื่องหมายปิด
bool ReadString(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    std::size_t j = i + 1;
    while (j < s.size() && s[j] != '"') {
        if (s[j] == '\\') ++j;   // ข้ามอักขระที่ถูก escape
        ++j;
    }
    if (j >= s.size()) return false;
    out = Unescape(s.substr(i + 1, j - i - 1));
    i = j + 1;
    return true;
}

void SkipWs(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

}  // namespace

bool UiStrings::Parse(const std::string& json) {
    strings_.clear();
    const std::string key = "\"strings\"";
    std::size_t p = json.find(key);
    if (p == std::string::npos) return false;
    p = json.find('{', p + key.size());
    if (p == std::string::npos) return false;
    std::size_t i = p + 1;
    while (true) {
        SkipWs(json, i);
        if (i >= json.size()) return false;
        if (json[i] == '}') break;           // จบออบเจ็กต์ strings
        std::string k, v;
        if (!ReadString(json, i, k)) return false;
        SkipWs(json, i);
        if (i >= json.size() || json[i] != ':') return false;
        ++i;
        SkipWs(json, i);
        if (!ReadString(json, i, v)) return false;
        strings_[k] = v;
        SkipWs(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == '}') break;
        return false;
    }
    return !strings_.empty();
}

const std::string& UiStrings::Raw(const std::string& key) const {
    static const std::string kEmpty;
    const auto it = strings_.find(key);
    return it == strings_.end() ? kEmpty : it->second;
}

std::string UiStrings::Format(const std::string& key, const std::vector<std::string>& args,
                             const std::vector<long long>& numbers,
                             const std::string& fallback) const {
    const auto it = strings_.find(key);
    if (it == strings_.end()) return fallback;
    const std::string& tpl = it->second;
    std::string out;
    out.reserve(tpl.size() + 16);
    std::size_t a = 0, n = 0;
    for (std::size_t i = 0; i < tpl.size(); ++i) {
        if (tpl[i] == '%' && i + 1 < tpl.size()) {
            const char f = tpl[i + 1];
            if (f == 's' && a < args.size()) {
                out += args[a++];
                ++i;
                continue;
            }
            if (f == 'd' && n < numbers.size()) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", numbers[n++]);
                out += buf;
                ++i;
                continue;
            }
        }
        out.push_back(tpl[i]);   // %s/%d ที่ไม่มีค่าให้แทน ปล่อยไว้ตามเดิม
    }
    return out;
}

}  // namespace net
}  // namespace mobile
