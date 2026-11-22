#include "ran/platform/platform.h"

#include <cassert>
#include <cstdio>

using ran::platform::NormalizeRelativePath;

static void Expect(const char* in, const char* want) {
    std::string got;
    bool ok = NormalizeRelativePath(in, got);
    if (!ok || got != want) {
        std::printf("FAIL: '%s' -> ok=%d got='%s' want='%s'\n", in, ok, got.c_str(), want);
        assert(false);
    }
}

static void ExpectReject(const char* in) {
    std::string got;
    if (NormalizeRelativePath(in, got)) {
        std::printf("FAIL: '%s' should be rejected, got '%s'\n", in, got.c_str());
        assert(false);
    }
}

int main() {
    Expect("data\\gui\\ui.txt", "data/gui/ui.txt");   // Windows separators
    Expect("data//gui///ui.txt", "data/gui/ui.txt"); // repeated separators
    Expect("./data/./gui/ui.txt", "data/gui/ui.txt");
    Expect("data/gui/../gui/ui.txt", "data/gui/ui.txt");
    Expect("", "");
    ExpectReject("../secret.txt");                   // escape at start
    ExpectReject("data/../../secret.txt");           // escape after descending
    ExpectReject("..\\secret.txt");
    std::puts("platform path tests: all checks passed");
    return 0;
}
