// fvf.h - vertex size from a Direct3D 9 flexible vertex format (FVF) value.
// Bit values match d3d9types.h. The game calls D3DXGetFVFVertexSize about 160
// times, so this replaces that call with portable code.
#pragma once
#include <cstdint>

namespace ran::render {

constexpr uint32_t kFvfPositionMask = 0x400E;  // XYZ, XYZRHW, XYZBn, XYZW
constexpr uint32_t kFvfNormal = 0x010;
constexpr uint32_t kFvfPsize = 0x020;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfSpecular = 0x080;
constexpr uint32_t kFvfTexCountMask = 0xF00;
constexpr uint32_t kFvfTexCountShift = 8;
constexpr uint32_t kFvfLastBetaUbyte4 = 0x1000;
constexpr uint32_t kFvfLastBetaD3dColor = 0x8000;

// Returns the vertex size in bytes, or 0 when the FVF is invalid.
// Rules: a position is required; XYZBn (blend weights) and LASTBETA are sized
// as in D3D9; at most 8 texture coordinate sets; reserved bits are invalid.
// TEXCOORDSIZE bits for sets beyond the TEXCOUNT are ignored.
uint32_t VertexSizeFromFvf(uint32_t fvf);

}  // namespace ran::render
