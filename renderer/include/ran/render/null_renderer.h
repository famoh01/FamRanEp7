// null_renderer.h - headless IRenderer. Validates every resource handle, range,
// and frame state the same way a real backend must, but draws nothing. Used by
// tests and by the game's logic runs that have no GPU.
#pragma once
#include <cstdint>
#include <memory>

#include "ran/render/handle.h"
#include "ran/render/renderer.h"

namespace ran::render {

class NullRenderer final : public IRenderer {
public:
    NullRenderer();
    ~NullRenderer() override;

    bool BeginFrame() override;
    void EndFrame() override;
    void Clear(uint32_t argbColor, bool clearColor, bool clearDepth) override;

    Handle CreateVertexBuffer(uint32_t byteSize, uint32_t fvf, bool dynamic) override;
    Handle CreateIndexBuffer(uint32_t indexCount, bool use32Bit, bool dynamic) override;
    bool UpdateVertexBuffer(Handle vb, uint32_t byteOffset, const void* data, uint32_t byteCount) override;
    bool UpdateIndexBuffer(Handle ib, uint32_t byteOffset, const void* data, uint32_t byteCount) override;

    Handle CreateTexture2D(uint32_t width, uint32_t height, uint32_t mipLevels, TextureFormat format) override;
    bool UpdateTexture2D(Handle tex, uint32_t level, const void* data, uint32_t byteCount) override;

    bool Destroy(Handle h) override;

    void SetRenderState(const RenderState& state) override;
    void SetTexture(uint32_t stage, Handle tex) override;
    void SetTransform(TransformKind kind, const float matrix16[16]) override;
    bool SetVertexBuffer(Handle vb) override;
    bool SetIndexBuffer(Handle ib) override;

    bool DrawPrimitive(PrimitiveType type, uint32_t startVertex, uint32_t primitiveCount) override;
    bool DrawIndexedPrimitive(PrimitiveType type, uint32_t startIndex, uint32_t primitiveCount) override;

    // Counters for tests.
    uint32_t FramesCompleted() const;
    uint32_t DrawCalls() const;
    uint32_t PrimitivesDrawn() const;
    std::size_t LiveResources() const;
    const RenderState& CurrentState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ran::render
