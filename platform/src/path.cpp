#include "ran/platform/platform.h"

#include <vector>

namespace ran::platform {

bool NormalizeRelativePath(const std::string& in, std::string& out) {
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") {
            // skip empty and current-dir segments
        } else if (cur == "..") {
            if (parts.empty()) return false;   // escapes the root
            parts.pop_back();
        } else {
            parts.push_back(cur);
        }
        cur.clear();
        return true;
    };
    for (char c : in) {
        if (c == '/' || c == '\\') {
            if (!flush()) return false;
        } else {
            cur.push_back(c);
        }
    }
    if (!flush()) return false;

    out.clear();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back('/');
        out += parts[i];
    }
    return true;
}

}  // namespace ran::platform
