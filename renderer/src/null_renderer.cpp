#include "ran/render/null_renderer.h"

#include "ran/render/fvf.h"

namespace ran::render {

namespace {

enum class Kind { None, Vertex, Index, Texture };

// One table for all resource kinds. Separate tables would let a texture handle and a
// vertex-buffer handle with the same index and generation collide, so each entry
// stores its kind and every lookup checks it.
struct Resource {
    Kind kind = Kind::None;
    uint32_t byteSize = 0;   // vertex or index bytes
    uint32_t stride = 0;     // vertex stride, or index size in bytes
    uint32_t indexCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
};

// Vertices needed to draw `count` primitives of the given type.
uint32_t VerticesFor(PrimitiveType type, uint32_t count) {
    switch (type) {
        case PrimitiveType::TriangleList: return 3 * count;
        case PrimitiveType::TriangleStrip: return count + 2;
        case PrimitiveType::TriangleFan: return count + 2;
        case PrimitiveType::LineList: return 2 * count;
        case PrimitiveType::LineStrip: return count + 1;
        case PrimitiveType::PointList: return count;
    }
    return 0;
}

}  // namespace

struct NullRenderer::Impl {
    HandleTable<Resource> resources;
    bool inFrame = false;
    uint32_t frames = 0;
    uint32_t drawCalls = 0;
    uint32_t primitives = 0;
    RenderState state;
    Handle boundVertex = kInvalidHandle;
    Handle boundIndex = kInvalidHandle;

    // Returns the entry only if the handle is live and has the expected kind.
    Resource* Find(Handle h, Kind kind) {
        Resource* r = resources.Get(h);
        return (r && r->kind == kind) ? r : nullptr;
    }
};

NullRenderer::NullRenderer() : impl_(std::make_unique<Impl>()) {}
NullRenderer::~NullRenderer() = default;

bool NullRenderer::BeginFrame() {
    if (impl_->inFrame) return false;
    impl_->inFrame = true;
    return true;
}

void NullRenderer::EndFrame() {
    if (!impl_->inFrame) return;
    impl_->inFrame = false;
    ++impl_->frames;
}

void NullRenderer::Clear(uint32_t, bool, bool) {}

Handle NullRenderer::CreateVertexBuffer(uint32_t byteSize, uint32_t fvf, bool) {
    const uint32_t stride = VertexSizeFromFvf(fvf);
    if (byteSize == 0 || stride == 0 || byteSize % stride != 0) return kInvalidHandle;
    Resource r;
    r.kind = Kind::Vertex;
    r.byteSize = byteSize;
    r.stride = stride;
    return impl_->resources.Insert(r);
}

Handle NullRenderer::CreateIndexBuffer(uint32_t indexCount, bool use32Bit, bool) {
    if (indexCount == 0) return kInvalidHandle;
    Resource r;
    r.kind = Kind::Index;
    r.indexCount = indexCount;
    r.stride = use32Bit ? 4u : 2u;
    r.byteSize = indexCount * r.stride;
    return impl_->resources.Insert(r);
}

bool NullRenderer::UpdateVertexBuffer(Handle vb, uint32_t byteOffset, const void* data, uint32_t byteCount) {
    Resource* r = impl_->Find(vb, Kind::Vertex);
    if (!r || !data || byteCount == 0) return false;
    if (byteOffset > r->byteSize || byteCount > r->byteSize - byteOffset) return false;
    return true;
}

bool NullRenderer::UpdateIndexBuffer(Handle ib, uint32_t byteOffset, const void* data, uint32_t byteCount) {
    Resource* r = impl_->Find(ib, Kind::Index);
    if (!r || !data || byteCount == 0) return false;
    if (byteOffset > r->byteSize || byteCount > r->byteSize - byteOffset) return false;
    return true;
}

Handle NullRenderer::CreateTexture2D(uint32_t width, uint32_t height, uint32_t mipLevels, TextureFormat) {
    // Power-of-two only: the port targets GLES 2 class hardware.
    if (width == 0 || height == 0 || (width & (width - 1)) != 0 || (height & (height - 1)) != 0) {
        return kInvalidHandle;
    }
    uint32_t fullChain = 1;
    for (uint32_t m = width > height ? width : height; m > 1; m >>= 1) ++fullChain;
    Resource r;
    r.kind = Kind::Texture;
    r.width = width;
    r.height = height;
    r.mipLevels = (mipLevels == 0 || mipLevels > fullChain) ? fullChain : mipLevels;
    return impl_->resources.Insert(r);
}

bool NullRenderer::UpdateTexture2D(Handle tex, uint32_t level, const void*, uint32_t byteCount) {
    Resource* r = impl_->Find(tex, Kind::Texture);
    if (!r || level >= r->mipLevels || byteCount == 0) return false;
    return true;
}

bool NullRenderer::Destroy(Handle h) {
    Resource* r = impl_->resources.Get(h);
    if (!r) return false;
    if (h == impl_->boundVertex) impl_->boundVertex = kInvalidHandle;
    if (h == impl_->boundIndex) impl_->boundIndex = kInvalidHandle;
    return impl_->resources.Remove(h);
}

void NullRenderer::SetRenderState(const RenderState& state) { impl_->state = state; }

void NullRenderer::SetTexture(uint32_t, Handle) {}

void NullRenderer::SetTransform(TransformKind, const float[16]) {}

bool NullRenderer::SetVertexBuffer(Handle vb) {
    if (vb != kInvalidHandle && impl_->Find(vb, Kind::Vertex) == nullptr) return false;
    impl_->boundVertex = vb;
    return true;
}

bool NullRenderer::SetIndexBuffer(Handle ib) {
    if (ib != kInvalidHandle && impl_->Find(ib, Kind::Index) == nullptr) return false;
    impl_->boundIndex = ib;
    return true;
}

bool NullRenderer::DrawPrimitive(PrimitiveType type, uint32_t startVertex, uint32_t primitiveCount) {
    if (!impl_->inFrame || primitiveCount == 0) return false;
    const Resource* vb = impl_->Find(impl_->boundVertex, Kind::Vertex);
    if (!vb) return false;
    const uint32_t vertexCount = vb->byteSize / vb->stride;
    const uint32_t needed = VerticesFor(type, primitiveCount);
    if (startVertex > vertexCount || needed > vertexCount - startVertex) return false;
    ++impl_->drawCalls;
    impl_->primitives += primitiveCount;
    return true;
}

bool NullRenderer::DrawIndexedPrimitive(PrimitiveType type, uint32_t startIndex, uint32_t primitiveCount) {
    if (!impl_->inFrame || primitiveCount == 0) return false;
    const Resource* vb = impl_->Find(impl_->boundVertex, Kind::Vertex);
    const Resource* ib = impl_->Find(impl_->boundIndex, Kind::Index);
    if (!vb || !ib) return false;
    const uint32_t needed = VerticesFor(type, primitiveCount);
    if (startIndex > ib->indexCount || needed > ib->indexCount - startIndex) return false;
    ++impl_->drawCalls;
    impl_->primitives += primitiveCount;
    return true;
}

uint32_t NullRenderer::FramesCompleted() const { return impl_->frames; }
uint32_t NullRenderer::DrawCalls() const { return impl_->drawCalls; }
uint32_t NullRenderer::PrimitivesDrawn() const { return impl_->primitives; }
std::size_t NullRenderer::LiveResources() const { return impl_->resources.LiveCount(); }
const RenderState& NullRenderer::CurrentState() const { return impl_->state; }

}  // namespace ran::render
