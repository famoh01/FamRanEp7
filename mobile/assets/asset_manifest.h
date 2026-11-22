// mobile/assets/asset_manifest.h
// Manifest-verified asset loader for the mobile client.
//
// CLEAN-ROOM RULE: this loader only ever reads files this project generates itself
// (`mobile/assets/make_placeholder_assets.py`, plus `labels.bin` from
// `mobile/android/tools/make_labels.py`). No archive, model, texture or data file from the
// original RAN EP7 release is read, converted, packed or shipped, and no anti-cheat
// component is included. See docs/ASSETS.md.
//
// What it guarantees, all unit-tested on the host without any game data:
//   - the manifest is parsed strictly (format, name charset, no path traversal, no duplicates)
//   - a file must be listed in the manifest to be loaded
//   - the loaded bytes must match the declared size AND checksum
//   - a missing file is reported as Missing (the game then draws its procedural placeholder
//     instead of failing), never as an empty buffer
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mobile::assets {

// Largest single asset the loader accepts (placeholder files are a few hundred bytes).
constexpr size_t kMaxAssetBytes = 1024 * 1024;
// Largest manifest accepted.
constexpr size_t kMaxManifestBytes = 64 * 1024;
// Manifest format version written by make_placeholder_assets.py.
constexpr int kManifestVersion = 1;

// The kinds the manifest may declare. `texture` = raw RGBA8 bytes, `label` = packed raster
// atlas (labels.bin), `raw` = opaque bytes the game interprets itself.
enum class Kind { Texture, Label, Raw };

const char* KindName(Kind kind);
bool ParseKind(const std::string& text, Kind& out);

struct AssetEntry {
    std::string name;    // file name only, e.g. "ph_ground.bin"
    Kind kind = Kind::Raw;
    size_t bytes = 0;
    uint32_t checksum = 0;   // FNV-1a 32-bit of the file contents
};

// FNV-1a, 32 bit. Deterministic and dependency-free, enough to catch a truncated or swapped file.
uint32_t Fnv1a32(const void* data, size_t size);

// Parses the manifest text. On failure `err` describes the first problem and `out` is empty.
bool ParseManifest(const std::string& text, std::vector<AssetEntry>& out, std::string& err);

// Result of loading one asset.
enum class Status {
    Ok,
    NotInManifest,      // the name is not listed (or the manifest was never loaded)
    Missing,            // listed, but the source has no such file
    SizeMismatch,       // the file exists but its length differs from the manifest
    ChecksumMismatch,   // length matches, contents do not
    ReadError,          // the source failed for another reason
    TooLarge,           // larger than kMaxAssetBytes (the manifest is not trusted blindly)
};

const char* StatusName(Status status);

// Where the bytes come from: the Android AAssetManager, a directory on the host, or an
// in-memory map in tests. Return false when the asset does not exist; `err` may say why.
using Source = std::function<bool(const std::string& name, std::vector<uint8_t>& out, std::string& err)>;

class AssetStore {
public:
    // Parses the manifest text. Replaces whatever was loaded before.
    bool LoadManifest(const std::string& text, std::string& err);
    bool Loaded() const { return loaded_; }
    const std::vector<AssetEntry>& Entries() const { return entries_; }
    const AssetEntry* Find(const std::string& name) const;

    // Loads and verifies one asset. `out` is only filled on Status::Ok.
    Status Load(const Source& source, const std::string& name, std::vector<uint8_t>& out, std::string& err);

    // Loads every manifest entry. Bumps `missing`/`broken` counters for the report the game logs.
    struct LoadReport {
        int loaded = 0;
        int missing = 0;
        int broken = 0;              // present but failed verification
        std::vector<std::string> problems;   // "<name>: <status>" for each failure
    };
    LoadReport LoadAll(const Source& source, std::map<std::string, std::vector<uint8_t>>& out);

private:
    bool loaded_ = false;
    std::vector<AssetEntry> entries_;
};

// A Source over an in-memory map of name -> bytes (tests, and the CI asset check).
Source MemorySource(std::map<std::string, std::vector<uint8_t>>* files);

}  // namespace mobile::assets
