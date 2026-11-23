// mobile/assets/asset_manifest.cpp
#include "asset_manifest.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <set>
#include <sstream>

namespace mobile::assets {

namespace {

// File names the loader accepts: lowercase letters, digits and underscore, then ".bin". No '/',
// no '\\', no "..", no leading dot: a manifest can never point outside the asset directory.
bool ValidName(const std::string& name) {
    const std::string suffix = ".bin";
    if (name.size() <= suffix.size() || name.size() > 64) return false;
    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    for (size_t i = 0; i < name.size() - suffix.size(); ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

bool ParseU64(const std::string& text, uint64_t& out) {
    if (text.empty() || text.size() > 20) return false;
    uint64_t v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + uint64_t(c - '0');
    }
    out = v;
    return true;
}

bool ParseChecksum(const std::string& text, uint32_t& out) {
    if (text.size() != 8) return false;
    uint32_t v = 0;
    for (char c : text) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;                     // uppercase is rejected on purpose (one form only)
        v = (v << 4) | uint32_t(d);
    }
    out = v;
    return true;
}

std::vector<std::string> SplitWords(const std::string& line) {
    std::vector<std::string> words;
    std::istringstream in(line);
    std::string w;
    while (in >> w) words.push_back(w);
    return words;
}

}  // namespace

const char* KindName(Kind kind) {
    switch (kind) {
        case Kind::Texture: return "texture";
        case Kind::Label:   return "label";
        case Kind::Raw:     return "raw";
    }
    return "raw";
}

bool ParseKind(const std::string& text, Kind& out) {
    if (text == "texture") { out = Kind::Texture; return true; }
    if (text == "label") { out = Kind::Label; return true; }
    if (text == "raw") { out = Kind::Raw; return true; }
    return false;
}

uint32_t Fnv1a32(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        h ^= uint32_t(p[i]);
        h *= 16777619u;
    }
    return h;
}

const char* StatusName(Status status) {
    switch (status) {
        case Status::Ok:               return "ok";
        case Status::NotInManifest:    return "not_in_manifest";
        case Status::Missing:          return "missing";
        case Status::SizeMismatch:     return "size_mismatch";
        case Status::ChecksumMismatch: return "checksum_mismatch";
        case Status::ReadError:        return "read_error";
        case Status::TooLarge:         return "too_large";
    }
    return "unknown";
}

bool ParseManifest(const std::string& text, std::vector<AssetEntry>& out, std::string& err) {
    out.clear();
    err.clear();
    if (text.size() > kMaxManifestBytes) {
        err = "manifest too large";
        return false;
    }
    std::vector<AssetEntry> entries;
    std::set<std::string> seen;
    bool header = false;
    int lineNo = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> words = SplitWords(line);
        if (words.empty()) continue;                       // blank line
        if (words[0][0] == '#') continue;                  // comment
        if (!header) {
            if (words.size() != 2 || words[0] != "ran-assets" || words[1] != "1") {
                err = "line " + std::to_string(lineNo) + ": expected 'ran-assets 1' header";
                return false;
            }
            header = true;
            continue;
        }
        if (words.size() != 4) {
            err = "line " + std::to_string(lineNo) + ": expected 'name kind bytes checksum'";
            return false;
        }
        AssetEntry e;
        e.name = words[0];
        if (!ValidName(e.name)) {
            err = "line " + std::to_string(lineNo) + ": bad asset name '" + e.name + "'";
            return false;
        }
        if (!ParseKind(words[1], e.kind)) {
            err = "line " + std::to_string(lineNo) + ": unknown kind '" + words[1] + "'";
            return false;
        }
        uint64_t bytes = 0;
        if (!ParseU64(words[2], bytes) || bytes > kMaxAssetBytes) {
            err = "line " + std::to_string(lineNo) + ": bad size '" + words[2] + "'";
            return false;
        }
        e.bytes = size_t(bytes);
        if (!ParseChecksum(words[3], e.checksum)) {
            err = "line " + std::to_string(lineNo) + ": bad checksum '" + words[3] + "' (8 lowercase hex)";
            return false;
        }
        if (!seen.insert(e.name).second) {
            err = "line " + std::to_string(lineNo) + ": duplicate asset '" + e.name + "'";
            return false;
        }
        entries.push_back(e);
    }
    if (!header) {
        err = "empty manifest";
        return false;
    }
    out = std::move(entries);
    return true;
}

bool AssetStore::LoadManifest(const std::string& text, std::string& err) {
    std::vector<AssetEntry> entries;
    if (!ParseManifest(text, entries, err)) {
        loaded_ = false;
        entries_.clear();
        return false;
    }
    entries_ = std::move(entries);
    loaded_ = true;
    return true;
}

const AssetEntry* AssetStore::Find(const std::string& name) const {
    for (const AssetEntry& e : entries_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

Status AssetStore::Load(const Source& source, const std::string& name, std::vector<uint8_t>& out,
                        std::string& err) {
    err.clear();
    out.clear();
    const AssetEntry* entry = loaded_ ? Find(name) : nullptr;
    if (entry == nullptr) {
        err = name + ": not listed in the manifest";
        return Status::NotInManifest;
    }
    if (!source) {
        err = name + ": no source";
        return Status::ReadError;
    }
    try {
        if (!source(name, out, err)) {
            if (err.empty()) err = name + ": not found";
            out.clear();
            return Status::Missing;
        }
    } catch (...) {
        err = name + ": source threw";
        out.clear();
        return Status::ReadError;
    }
    if (out.size() > kMaxAssetBytes) {
        err = name + ": larger than the limit";
        out.clear();
        return Status::TooLarge;
    }
    if (out.size() != entry->bytes) {
        err = name + ": size " + std::to_string(out.size()) + " != manifest " +
              std::to_string(entry->bytes);
        out.clear();
        return Status::SizeMismatch;
    }
    const uint32_t sum = Fnv1a32(out.data(), out.size());
    if (sum != entry->checksum) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s: checksum %08x != manifest %08x", name.c_str(), sum,
                      entry->checksum);
        err = buf;
        out.clear();
        return Status::ChecksumMismatch;
    }
    return Status::Ok;
}

AssetStore::LoadReport AssetStore::LoadAll(const Source& source,
                                           std::map<std::string, std::vector<uint8_t>>& out) {
    LoadReport report;
    for (const AssetEntry& e : entries_) {
        std::vector<uint8_t> bytes;
        std::string err;
        switch (Load(source, e.name, bytes, err)) {
            case Status::Ok:
                ++report.loaded;
                out[e.name] = std::move(bytes);
                break;
            case Status::Missing:
                ++report.missing;
                report.problems.push_back(e.name + ": " + StatusName(Status::Missing));
                break;
            default:
                ++report.broken;
                report.problems.push_back(err.empty() ? e.name : err);
                break;
        }
    }
    return report;
}

Source MemorySource(std::map<std::string, std::vector<uint8_t>>* files) {
    return [files](const std::string& name, std::vector<uint8_t>& out, std::string& err) {
        if (files == nullptr) {
            err = "no files";
            return false;
        }
        const auto it = files->find(name);
        if (it == files->end()) {
            err = name + ": absent";
            return false;
        }
        out = it->second;
        return true;
    };
}

}  // namespace mobile::assets
