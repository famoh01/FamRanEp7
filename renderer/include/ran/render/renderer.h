// renderer.h - renderer interface for the port (phase 3, step 3.1).
// Not tied to Direct3D 9 or to bgfx. The D3D9 adapter (3.2) and the bgfx backend
// (3.3) each implement IRenderer. Portable: no OS or graphics-API headers.
#pragma once
#include <cstdint>

#include "ran/render/handle.h"

namespace ran::render {

enum class PrimitiveType { TriangleList, TriangleStrip, TriangleFan, LineList, LineStrip, PointList };

enum class TransformKind { World, View, Projection, Texture0 };

enum class CullMode { None, Clockwise, CounterClockwise };

enum class BlendFactor {
    Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstColor, InvDstColor, DstAlpha, InvDstAlpha
};

enum class CompareFunc { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

enum class TextureFormat { Rgba8, Rgb565, Rgba4444, Depth16 };

// The subset of fixed-function state the game uses (see docs/research/phase3-bgfx.md, group A).
struct RenderState {
    bool depthTest = true;
    bool depthWrite = true;
    CompareFunc depthFunc = CompareFunc::LessEqual;
    CullMode cull = CullMode::CounterClockwise;
    bool blend = false;
    BlendFactor srcBlend = BlendFactor::One;
    BlendFactor dstBlend = BlendFactor::Zero;
    bool alphaTest = false;
    CompareFunc alphaFunc = CompareFunc::Always;
    uint8_t alphaRef = 0;
    bool wireframe = false;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // BeginFrame returns false if a frame is already open. EndFrame closes it.
    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // Clears the color buffer (ARGB) and/or the depth buffer of the current target.
    virtual void Clear(uint32_t argbColor, bool clearColor, bool clearDepth) = 0;

    // Buffers. fvf is a D3D9 FVF value; its size must be valid (see fvf.h).
    // Returns kInvalidHandle on invalid arguments.
    virtual Handle CreateVertexBuffer(uint32_t byteSize, uint32_t fvf, bool dynamic) = 0;
    virtual Handle CreateIndexBuffer(uint32_t indexCount, bool use32Bit, bool dynamic) = 0;
    virtual bool UpdateVertexBuffer(Handle vb, uint32_t byteOffset, const void* data, uint32_t byteCount) = 0;
    virtual bool UpdateIndexBuffer(Handle ib, uint32_t byteOffset, const void* data, uint32_t byteCount) = 0;

    // Textures. level 0 is the full-size image; mipLevels 0 means "full chain".
    virtual Handle CreateTexture2D(uint32_t width, uint32_t height, uint32_t mipLevels, TextureFormat format) = 0;
    virtual bool UpdateTexture2D(Handle tex, uint32_t level, const void* data, uint32_t byteCount) = 0;

    // Destroys a buffer or texture. Returns false for a stale or invalid handle.
    virtual bool Destroy(Handle h) = 0;

    // State and binding. A bound resource must stay alive until it is unbound or replaced.
    virtual void SetRenderState(const RenderState& state) = 0;
    virtual void SetTexture(uint32_t stage, Handle tex) = 0;
    virtual void SetTransform(TransformKind kind, const float matrix16[16]) = 0;
    virtual bool SetVertexBuffer(Handle vb) = 0;
    virtual bool SetIndexBuffer(Handle ib) = 0;

    // Draws. Each returns false if the state is invalid (no frame open, missing
    // buffer, or a range that runs past the buffer).
    virtual bool DrawPrimitive(PrimitiveType type, uint32_t startVertex, uint32_t primitiveCount) = 0;
    virtual bool DrawIndexedPrimitive(PrimitiveType type, uint32_t startIndex, uint32_t primitiveCount) = 0;
};

}  // namespace ran::render
