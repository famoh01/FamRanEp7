// Renderer interface tests (no GPU needed).
//  - FVF vertex sizes, checked against the D3D9 bit layout and the D3DX rules.
//  - Handle tables reject stale handles after reuse.
//  - NullRenderer enforces frame state, buffer binding, and draw ranges.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ran/render/fvf.h"
#include "ran/render/handle.h"
#include "ran/render/null_renderer.h"

using namespace ran::render;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void TestFvfSizes() {
    // Values from d3d9types.h and the D3DX size rules.
    Check(VertexSizeFromFvf(0x002) == 12, "XYZ = 12");
    Check(VertexSizeFromFvf(0x002 | 0x040) == 16, "XYZ|DIFFUSE = 16");
    Check(VertexSizeFromFvf(0x004 | 0x040 | 0x100) == 28, "XYZRHW|DIFFUSE|TEX1 = 28");
    Check(VertexSizeFromFvf(0x002 | 0x010 | 0x100) == 32, "XYZ|NORMAL|TEX1 = 32");
    Check(VertexSizeFromFvf(0x006 | 0x010 | 0x040 | 0x200) == 48, "XYZB1|NORMAL|DIFFUSE|TEX2 = 48");
    Check(VertexSizeFromFvf(0x4002) == 16, "XYZW = 16");
    Check(VertexSizeFromFvf(0x002 | 0x100 | (1u << 16)) == 24, "XYZ|TEX1 with TEXCOORDSIZE3 = 24");
    Check(VertexSizeFromFvf(0x002 | 0x100 | (3u << 16)) == 16, "XYZ|TEX1 with TEXCOORDSIZE1 = 16");
    Check(VertexSizeFromFvf(0x002 | 0x200 | (2u << 18)) == 12 + 8 + 16, "XYZ|TEX2 set1 4 floats = 36");
    Check(VertexSizeFromFvf(0x00e | 0x020) == 12 + 20 + 4, "XYZB5|PSIZE = 36");
    // invalid cases
    Check(VertexSizeFromFvf(0) == 0, "no position = 0");
    Check(VertexSizeFromFvf(0x040) == 0, "diffuse only (no position) = 0");
    Check(VertexSizeFromFvf(0x002 | 0x001) == 0, "reserved bit 0x001 = 0");
    Check(VertexSizeFromFvf(0x002 | 0x2000) == 0, "reserved bit 0x2000 = 0");
    Check(VertexSizeFromFvf(0x002 | 0x1000) == 0, "LASTBETA without blend = 0");
    Check(VertexSizeFromFvf(0x006 | 0x1000) == 16, "XYZB1|LASTBETA_UBYTE4 = 16");
    Check(VertexSizeFromFvf(0x006 | 0x1000 | 0x8000) == 0, "both LASTBETA bits = 0");
    Check(VertexSizeFromFvf(0x002 | 0x900) == 0, "9 texture sets = 0");
}

static void TestHandles() {
    HandleTable<int> t;
    Handle a = t.Insert(10);
    Check(a != kInvalidHandle, "first handle is valid");
    Check(t.Get(a) && *t.Get(a) == 10, "lookup returns value");
    Check(t.Remove(a), "remove live handle");
    Check(t.Get(a) == nullptr, "stale handle rejected after remove");
    Check(!t.Remove(a), "double remove rejected");
    Handle b = t.Insert(20);
    Check(b != a, "reused slot gets a new generation");
    Check(t.Get(a) == nullptr, "old handle still rejected after slot reuse");
    Check(t.Get(b) && *t.Get(b) == 20, "new handle works");
    Check(t.Get(kInvalidHandle) == nullptr, "invalid handle rejected");
    Check(t.LiveCount() == 1, "live count");
}

static void TestNullRendererFrameAndDraw() {
    NullRenderer r;
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    r.SetTransform(TransformKind::World, identity);

    Handle vb = r.CreateVertexBuffer(12 * 6, 0x002, false);  // 6 XYZ vertices
    Check(vb != kInvalidHandle, "create vertex buffer");
    Check(r.CreateVertexBuffer(10, 0x002, false) == kInvalidHandle, "size not multiple of stride rejected");
    Check(r.CreateVertexBuffer(12, 0, false) == kInvalidHandle, "invalid FVF rejected");

    Check(!r.DrawPrimitive(PrimitiveType::TriangleList, 0, 1), "draw outside frame rejected");
    Check(r.BeginFrame(), "begin frame");
    Check(!r.BeginFrame(), "nested begin rejected");
    Check(!r.DrawPrimitive(PrimitiveType::TriangleList, 0, 1), "draw without bound buffer rejected");

    Check(r.SetVertexBuffer(vb), "bind vertex buffer");
    Check(r.DrawPrimitive(PrimitiveType::TriangleList, 0, 2), "2 triangles from 6 vertices");
    Check(!r.DrawPrimitive(PrimitiveType::TriangleList, 3, 2), "range past end rejected (needs 6, has 3 left)");
    Check(r.DrawPrimitive(PrimitiveType::TriangleStrip, 0, 4), "strip of 4 needs 6 vertices");
    Check(!r.DrawPrimitive(PrimitiveType::TriangleStrip, 0, 5), "strip of 5 needs 7 vertices");
    Check(r.DrawPrimitive(PrimitiveType::LineList, 4, 1), "line from vertices 4..5");
    Check(!r.DrawPrimitive(PrimitiveType::PointList, 0, 0), "zero primitives rejected");

    RenderState s;
    s.blend = true;
    s.srcBlend = BlendFactor::SrcAlpha;
    s.dstBlend = BlendFactor::InvSrcAlpha;
    r.SetRenderState(s);
    Check(r.CurrentState().blend && r.CurrentState().srcBlend == BlendFactor::SrcAlpha, "state stored");

    r.EndFrame();
    Check(r.FramesCompleted() == 1, "one frame completed");
    Check(r.DrawCalls() == 3, "three accepted draws");
    Check(r.PrimitivesDrawn() == 2 + 4 + 1, "primitive count");

    Check(r.Destroy(vb), "destroy buffer");
    Check(!r.Destroy(vb), "destroy stale handle rejected");
    Check(r.LiveResources() == 0, "no live resources after destroy");
}

static void TestNullRendererIndexed() {
    NullRenderer r;
    Handle vb = r.CreateVertexBuffer(12 * 4, 0x002, false);
    Handle ib = r.CreateIndexBuffer(6, false, false);
    Check(ib != kInvalidHandle, "index buffer");
    Check(r.BeginFrame(), "begin");
    Check(r.SetVertexBuffer(vb) && r.SetIndexBuffer(ib), "bind both");
    Check(r.DrawIndexedPrimitive(PrimitiveType::TriangleList, 0, 2), "2 triangles from 6 indices");
    Check(!r.DrawIndexedPrimitive(PrimitiveType::TriangleList, 3, 2), "index range past end rejected");
    Check(r.Destroy(vb), "destroying bound vertex buffer unbinds it");
    Check(!r.DrawIndexedPrimitive(PrimitiveType::TriangleList, 0, 1), "draw after unbind rejected");
    r.EndFrame();
}

static void TestKindIsChecked() {
    // A texture and a vertex buffer can share index and generation; the kind check must keep them apart.
    NullRenderer r;
    Handle vb = r.CreateVertexBuffer(12 * 3, 0x002, false);
    Handle tex = r.CreateTexture2D(4, 4, 1, TextureFormat::Rgba8);
    Check(vb != kInvalidHandle && tex != kInvalidHandle, "create both");
    Check(!r.UpdateTexture2D(vb, 0, "x", 1), "vertex handle is not a texture");
    Check(!r.SetVertexBuffer(tex), "texture handle cannot be bound as vertex buffer");
    Check(r.Destroy(tex), "destroy texture");
    Check(r.LiveResources() == 1, "vertex buffer still alive after destroying the texture");
    Check(r.Destroy(vb), "destroy vertex buffer");
}

static void TestTextureAndUpdates() {
    NullRenderer r;
    Check(r.CreateTexture2D(100, 64, 0, TextureFormat::Rgba8) == kInvalidHandle, "non power-of-two width rejected");
    Handle tex = r.CreateTexture2D(256, 64, 0, TextureFormat::Rgba8);
    Check(tex != kInvalidHandle, "256x64 texture");
    const uint8_t px[4] = {255, 0, 0, 255};
    Check(r.UpdateTexture2D(tex, 0, px, 4), "update level 0");
    Check(!r.UpdateTexture2D(tex, 99, px, 4), "level out of range rejected");
    Handle vb = r.CreateVertexBuffer(12 * 3, 0x002, true);
    const float v[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    Check(r.UpdateVertexBuffer(vb, 0, v, sizeof(v)), "update vertex buffer");
    Check(!r.UpdateVertexBuffer(vb, 30, v, sizeof(v)), "update past end rejected");
    Check(r.SetVertexBuffer(vb) && r.SetVertexBuffer(kInvalidHandle), "unbind with invalid handle");
}

int main() {
    TestFvfSizes();
    TestHandles();
    TestNullRendererFrameAndDraw();
    TestNullRendererIndexed();
    TestTextureAndUpdates();
    TestKindIsChecked();
    if (g_failures != 0) {
        std::printf("renderer tests: %d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("renderer tests: all checks passed");
    return EXIT_SUCCESS;
}
