#include "ran/render/fvf.h"

namespace ran::render {

namespace {
constexpr uint32_t kReservedBits = 0x001 | 0x2000;
constexpr uint32_t kLastBetaMask = kFvfLastBetaUbyte4 | kFvfLastBetaD3dColor;

// TEXCOORDSIZE field (2 bits per set) -> number of floats. D3D9: 0=2, 1=3, 2=4, 3=1.
constexpr uint32_t kTexFloats[4] = {2, 3, 4, 1};
}  // namespace

uint32_t VertexSizeFromFvf(uint32_t fvf) {
    if (fvf & kReservedBits) return 0;

    uint32_t size = 0;
    const uint32_t pos = fvf & kFvfPositionMask;
    const bool hasBlend = (pos >= 0x006 && pos <= 0x00E && (pos % 2) == 0);  // XYZB1..XYZB5
    if (pos == 0x002) {                 // XYZ
        size = 12;
    } else if (pos == 0x004) {          // XYZRHW
        size = 16;
    } else if (pos == 0x4002) {         // XYZW
        size = 16;
    } else if (hasBlend) {
        const uint32_t blends = (pos - 4) / 2;  // 1..5 float weights
        size = 12 + 4 * blends;
    } else {
        return 0;                       // no position, or an unknown position type
    }

    const uint32_t lastBeta = fvf & kLastBetaMask;
    if (lastBeta != 0) {
        if (!hasBlend || lastBeta == kLastBetaMask) return 0;
    }

    if (fvf & kFvfNormal) size += 12;
    if (fvf & kFvfPsize) size += 4;
    if (fvf & kFvfDiffuse) size += 4;
    if (fvf & kFvfSpecular) size += 4;

    const uint32_t texCount = (fvf & kFvfTexCountMask) >> kFvfTexCountShift;
    if (texCount > 8) return 0;
    for (uint32_t i = 0; i < texCount; ++i) {
        const uint32_t bits = (fvf >> (16 + 2 * i)) & 3u;
        size += 4 * kTexFloats[bits];
    }
    return size;
}

}  // namespace ran::render
