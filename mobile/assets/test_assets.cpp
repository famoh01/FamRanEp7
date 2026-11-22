// mobile/assets/test_assets.cpp
// Host unit tests for the manifest-verified placeholder asset loader. No game data is involved:
// the files are produced by make_placeholder_assets.py from arithmetic.
// Run: make -C mobile/assets test
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "asset_manifest.h"

using mobile::assets::AssetEntry;
using mobile::assets::AssetStore;
using mobile::assets::Kind;
using mobile::assets::Status;

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++g_failures;
    }
}

static std::string Sum(const std::vector<uint8_t>& bytes) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", mobile::assets::Fnv1a32(bytes.data(), bytes.size()));
    return buf;
}

static const char* kHeader = "ran-assets 1\n";

// A three-entry manifest that matches `kFiles` below.
static std::string GoodManifest(const std::map<std::string, std::vector<uint8_t>>& files) {
    std::string text = kHeader;
    for (const auto& kv : files) {
        text += kv.first + " raw " + std::to_string(kv.second.size()) + " " + Sum(kv.second) + "\n";
    }
    return text;
}

static void TestFnv() {
    const char* abc = "abc";
    Check(mobile::assets::Fnv1a32(abc, 3) == 0x1a47e90bu, "FNV-1a 32 of \"abc\"");
    Check(mobile::assets::Fnv1a32("", 0) == 2166136261u, "FNV-1a 32 of empty input");
    const char* abd = "abd";
    Check(mobile::assets::Fnv1a32(abc, 3) != mobile::assets::Fnv1a32(abd, 3), "one changed byte changes the sum");
}

static void TestManifestParsing() {
    std::vector<AssetEntry> entries;
    std::string err;
    const std::string ok = "ran-assets 1\n"
                           "# comment\n"
                           "\n"
                           "ph_ground.bin texture 16392 7e4e661a\n"
                           "ph_ui_frame.bin   raw   4616 3F28AF72\n";
    // Uppercase checksum is rejected below; build with the correct lowercase form instead.
    Check(!mobile::assets::ParseManifest(ok, entries, err), "uppercase checksum is rejected");
    Check(err.find("checksum") != std::string::npos, "the error names the bad field");

    const std::string good = "ran-assets 1\n"
                             "# comment\n"
                             "\n"
                             "ph_ground.bin texture 16392 7e4e661a\n"
                             "ph_ui_frame.bin   raw   4616 3f28af72\n";
    Check(mobile::assets::ParseManifest(good, entries, err), "a valid manifest parses");
    Check(entries.size() == 2, "two entries");
    Check(entries[0].name == "ph_ground.bin" && entries[0].kind == Kind::Texture &&
              entries[0].bytes == 16392 && entries[0].checksum == 0x7e4e661a,
          "entry values are read exactly");
    Check(entries[1].kind == Kind::Raw && entries[1].bytes == 4616, "second entry values");

    struct Bad { const char* text; const char* why; };
    const Bad bad[] = {
        {"", "no header"},
        {"ran-assets 2\n", "wrong version"},
        {"ran-assets\n", "header needs a version"},
        {"# only a comment\n", "comment only"},
        {"ran-assets 1\n../escape.bin raw 4 00000000\n", "path traversal"},
        {"ran-assets 1\nsub/dir.bin raw 4 00000000\n", "path separator"},
        {"ran-assets 1\n.bin raw 4 00000000\n", "empty base name"},
        {"ran-assets 1\nph_ground.txt raw 4 00000000\n", "wrong extension"},
        {"ran-assets 1\nPh_gnd.bin raw 4 00000000\n", "uppercase name"},
        {"ran-assets 1\nph_gnd.bin sprite 4 00000000\n", "unknown kind"},
        {"ran-assets 1\nph_gnd.bin raw 4\n", "missing checksum"},
        {"ran-assets 1\nph_gnd.bin raw -4 00000000\n", "negative size"},
        {"ran-assets 1\nph_gnd.bin raw 99999999 00000000\n", "size over the limit"},
        {"ran-assets 1\nph_gnd.bin raw 4 0000000\n", "short checksum"},
        {"ran-assets 1\nph_gnd.bin raw 4 zzzzzzzz\n", "non-hex checksum"},
        {"ran-assets 1\nph_gnd.bin raw 4 00000000\nph_gnd.bin raw 4 00000000\n", "duplicate name"},
        {"ran-assets 1\nph_gnd.bin raw 4 00000000 extra\n", "trailing field"},
    };
    for (const Bad& b : bad) {
        std::vector<AssetEntry> e2;
        std::string e2err;
        const bool parsed = mobile::assets::ParseManifest(b.text, e2, e2err);
        if (parsed || e2err.empty() || !e2.empty()) {
            std::printf("FAIL bad manifest accepted or unexplained: %s\n", b.why);
            ++g_failures;
        }
    }
    Check(true, "17 malformed manifests are all rejected with a reason");

    // A huge manifest is refused before it is parsed.
    std::string huge = kHeader;
    huge.append(mobile::assets::kMaxManifestBytes, '#');
    Check(!mobile::assets::ParseManifest(huge, entries, err), "oversized manifest is rejected");
}

static void TestLoadAndVerify() {
    std::map<std::string, std::vector<uint8_t>> files = {
        {"ph_ground.bin", {1, 2, 3, 4, 5, 6, 7, 8}},
        {"ph_block.bin", {9, 9, 9}},
    };
    AssetStore store;
    std::string err;
    Check(!store.Loaded(), "a fresh store is not loaded");
    std::vector<uint8_t> probe;
    Check(store.Load(mobile::assets::MemorySource(&files), "ph_ground.bin", probe, err) ==
              Status::NotInManifest && probe.empty(),
          "nothing loads before a manifest is loaded");
    Check(store.LoadManifest(GoodManifest(files), err), "manifest loads");

    std::vector<uint8_t> out;
    Check(store.Load(mobile::assets::MemorySource(&files), "ph_ground.bin", out, err) == Status::Ok &&
              out.size() == 8 && out[7] == 8,
          "a listed file loads with its exact bytes");

    files["ph_ground.bin"] = {1, 2, 3, 4, 5, 6, 7, 9};     // same size, one byte changed
    Check(store.Load(mobile::assets::MemorySource(&files), "ph_ground.bin", out, err) ==
              Status::ChecksumMismatch && out.empty(),
          "a changed byte is a checksum mismatch and no data is handed out");
    Check(err.find("checksum") != std::string::npos, "the error says checksum");

    files["ph_ground.bin"] = {1, 2, 3};                      // truncated
    Check(store.Load(mobile::assets::MemorySource(&files), "ph_ground.bin", out, err) ==
              Status::SizeMismatch && out.empty(),
          "a truncated file is a size mismatch");

    files.erase("ph_block.bin");
    Check(store.Load(mobile::assets::MemorySource(&files), "ph_block.bin", out, err) == Status::Missing &&
              out.empty(),
          "a missing file is Missing, never empty data");
    Check(err.find("ph_block.bin") != std::string::npos, "the error names the asset");

    Check(store.Load(mobile::assets::MemorySource(&files), "ph_unknown.bin", out, err) ==
              Status::NotInManifest,
          "an unlisted file is refused");
    Check(store.Load(mobile::assets::MemorySource(nullptr), "ph_ground.bin", out, err) == Status::Missing,
          "a null source is Missing");

    // A source that fails with its own message keeps it.
    mobile::assets::Source throwing = [](const std::string&, std::vector<uint8_t>&, std::string& e) -> bool {
        e = "disk on fire";
        return false;
    };
    Check(store.Load(throwing, "ph_ground.bin", out, err) == Status::Missing && err == "disk on fire",
          "the source's own error is kept");

    // LoadAll reports per-file results instead of failing the whole set.
    files["ph_ground.bin"] = {1, 2, 3, 4, 5, 6, 7, 8};      // valid again
    files["ph_block.bin"] = {9, 9, 9};
    std::map<std::string, std::vector<uint8_t>> loaded;
    const auto report = store.LoadAll(mobile::assets::MemorySource(&files), loaded);
    Check(report.loaded == 2 && report.missing == 0 && report.broken == 0 && report.problems.empty() &&
              loaded.size() == 2,
          "LoadAll loads everything when the files match");
    files.erase("ph_block.bin");
    loaded.clear();
    const auto report2 = store.LoadAll(mobile::assets::MemorySource(&files), loaded);
    Check(report2.loaded == 1 && report2.missing == 1 && loaded.size() == 1,
          "LoadAll reports a missing file and still loads the rest");
    Check(report2.problems.size() == 1 && report2.problems[0] == "ph_block.bin: missing",
          "the problem line is readable");
}

static void TestBadManifestLeavesNothingLoaded() {
    std::map<std::string, std::vector<uint8_t>> files = {{"ph_ground.bin", {1, 2, 3}}};
    AssetStore store;
    std::string err;
    Check(store.LoadManifest(GoodManifest(files), err), "first manifest loads");
    Check(store.LoadManifest("ran-assets 1\n../evil.bin raw 1 00000000\n", err) == false, "bad manifest rejected");
    Check(!store.Loaded() && store.Entries().empty(), "a rejected manifest leaves the store empty");
}

int main(int argc, char** argv) {
    TestFnv();
    TestManifestParsing();
    TestLoadAndVerify();
    TestBadManifestLeavesNothingLoaded();

    // Optional: verify the real generated files in an asset directory (CI passes the APK asset dir).
    if (argc > 1) {
        std::string dir = argv[1];
        if (!dir.empty() && dir.back() != '/') dir += '/';
        FILE* f = std::fopen((dir + "assets_manifest.bin").c_str(), "rb");
        if (!f) {
            std::printf("FAIL cannot open %sassets_manifest.bin\n", dir.c_str());
            ++g_failures;
        } else {
            std::string text;
            char buf[512];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
            std::fclose(f);
            AssetStore store;
            std::string err;
            Check(store.LoadManifest(text, err), "the generated manifest parses");
            mobile::assets::Source fromDir = [&dir](const std::string& name, std::vector<uint8_t>& out,
                                                    std::string& e) -> bool {
                FILE* g = std::fopen((dir + name).c_str(), "rb");
                if (!g) {
                    e = name + ": absent";
                    return false;
                }
                out.clear();
                unsigned char b[4096];
                size_t m;
                while ((m = std::fread(b, 1, sizeof(b), g)) > 0) out.insert(out.end(), b, b + m);
                std::fclose(g);
                return true;
            };
            std::map<std::string, std::vector<uint8_t>> loaded;
            const auto report = store.LoadAll(fromDir, loaded);
            Check(report.broken == 0 && report.missing == 0 && report.loaded == int(store.Entries().size()),
                  "every generated asset matches its manifest size and checksum");
            for (const std::string& p : report.problems) std::printf("     %s\n", p.c_str());
            // The placeholder image container is self-describing: magic + w + h.
            const auto it = loaded.find("ph_ground.bin");
            Check(it != loaded.end() && it->second.size() >= 8 && std::memcmp(it->second.data(), "PHP1", 4) == 0 &&
                      (it->second[4] | (it->second[5] << 8)) == 64,
                  "ph_ground.bin is a PHP1 image of width 64");
        }
    }

    if (g_failures == 0) {
        std::printf("assets: all checks passed\n");
        return 0;
    }
    std::printf("assets: %d check(s) failed\n", g_failures);
    return 1;
}
