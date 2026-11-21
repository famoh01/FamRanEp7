#include "ran/platform/backends.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace ran::platform {
namespace {

class FileSystemStd final : public IFileSystem {
public:
    FileSystemStd(std::string assetRoot, std::string writableRoot)
        : assetRoot_(std::move(assetRoot)), writableRoot_(std::move(writableRoot)) {}

    bool ReadAsset(const std::string& relPath, std::vector<uint8_t>& out) override {
        std::string norm;
        if (!NormalizeRelativePath(relPath, norm) || norm.empty()) return false;
        // u8path keeps UTF-8 (Thai) file names intact on Windows.
        std::filesystem::path p = std::filesystem::u8path(assetRoot_ + "/" + norm);
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }

    std::string WritableRoot() const override { return writableRoot_; }

private:
    std::string assetRoot_;
    std::string writableRoot_;
};

}  // namespace

std::unique_ptr<IFileSystem> CreateFileSystem(const std::string& assetRoot,
                                              const std::string& writableRoot) {
    return std::make_unique<FileSystemStd>(assetRoot, writableRoot);
}

}  // namespace ran::platform
