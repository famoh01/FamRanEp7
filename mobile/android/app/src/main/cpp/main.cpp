// AI EP7 - Android entry point (original code).
// NativeActivity + OpenGL ES 3.0. Draws the touch HUD (mobile/touch_hud) and a
// placeholder player. Gameplay, data and text come later.
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "touch_hud.h"
#include "world_state.h"

#include <vector>

#define LOG_TAG "AiEp7"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Defined in net_bridge.cpp: true after Enter World (a character is selected on the server).
bool InWorld();

namespace {

constexpr float kPi = 3.14159265f;

// Text labels, rendered offline by tools/make_labels.py into assets/labels.bin.
// The order must match LABELS in that script.
enum LabelId : int {
    kLblMenu, kLblCharacter, kLblClub, kLblBag, kLblSkill,
    kLblRun, kLblWalk, kLblPotion, kLblAuto, kLblHp, kLblPercentSign,
    kLblPercent10,   // 10%, 20%, ... 90%: kLblPercent10 + (percent / 10 - 1)
    kLblDigit0 = kLblPercent10 + 9,   // "0" .. "9"
    kLblDigit1 = kLblDigit0 + 1,      // skill numbers "1" .. "6" start here
    kLblWeapon = kLblDigit0 + 10,    // equipment slot titles (empty until the server sends items)
    kLblArmor = kLblDigit0 + 11,
    kLblCount = kLblDigit0 + 12,
};
constexpr int kLabelFontPx = 48;   // em size the labels were rasterised at
constexpr int kFirstSkillId = 101; // placeholder skill ids 101..106 (grid cells 1..6)
constexpr int kItemPotion = 1;     // placeholder item ids; the real item table comes from the server

// Placeholder test map: a checkerboard floor and a few blocks the player cannot walk through.
// Blocks are fractions of the screen so they stay in place at any size.
// They are test geometry only, not data from the original game.
struct MapBlock { float x, y, w, h; };
constexpr MapBlock kMapBlocks[] = {
    {0.20f, 0.22f, 0.10f, 0.18f},
    {0.70f, 0.55f, 0.12f, 0.16f},
    {0.42f, 0.12f, 0.16f, 0.06f},
};
constexpr float kTileDp = 80.f;

// Placeholder blocks, used only when no server world is entered.
std::vector<mobile::net::WorldRect> PlaceholderBlocks() {
    std::vector<mobile::net::WorldRect> out;
    for (const MapBlock& b : kMapBlocks) out.push_back({b.x, b.y, b.w, b.h});
    return out;
}

// True if a circle at (cx, cy) with radius r overlaps the rectangle. Pixels; the rectangle is in map fractions.
bool CircleHitsRect(float cx, float cy, float r, const mobile::net::WorldRect& b, float w, float h) {
    const float rx = float(b.x) * w, ry = float(b.y) * h, rw = float(b.w) * w, rh = float(b.h) * h;
    const float nx = std::max(rx, std::min(cx, rx + rw));
    const float ny = std::max(ry, std::min(cy, ry + rh));
    const float dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy < r * r;
}

bool CircleBlocked(const std::vector<mobile::net::WorldRect>& blocks, float cx, float cy, float r, float w, float h) {
    for (const mobile::net::WorldRect& b : blocks)
        if (CircleHitsRect(cx, cy, r, b, w, h)) return true;
    return false;
}

struct Game {
    android_app* app = nullptr;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    float dp = 1.f;  // pixels per density-independent pixel

    GLuint program = 0;        // coloured shapes
    GLuint textProgram = 0;    // textured labels
    GLuint vbo = 0;
    GLint uScreen = -1;
    GLint uColor = -1;
    GLint tScreen = -1;
    GLint tColor = -1;
    GLint tSampler = -1;
    GLuint labelTex[kLblCount] = {};
    int labelW[kLblCount] = {};
    int labelH[kLblCount] = {};

    mobile::TouchHUD hud;
    bool hudReady = false;
    float moveX = 0.f;  // joystick output, [-1, 1]; dy < 0 = up on screen
    float moveY = 0.f;
    // Player position in map fractions (0..1, top-left = 0,0): the same units as the server.
    float playerX = 0.5f;
    float playerY = 0.5f;
    bool playerPlaced = false;
    // Blocks the player collides with: the placeholder map, or the server's map while in the world.
    std::vector<mobile::net::WorldRect> blocks = PlaceholderBlocks();
    uint64_t worldSeen = 0;   // last world version this frame has copied
    float attackFlash = 0.f;  // seconds left on the attack flash
    float slotFlash[mobile::TouchHUD::kSlots] = {};  // tap feedback per quick slot
    bool joyWasActive = false;
    float movedPx = 0.f;      // distance the player travelled during the current drag

    // Placeholder character state. Real HP comes from the game later.
    float hp = 100.f;
    float hpMax = 100.f;
    float hpTimer = 0.f;      // seconds until the next placeholder hit
    int potions = 12;         // potions in the bag (placeholder stock)

    long frames = 0;
};

Game* g_game = nullptr;  // the HUD hooks are plain function pointers, so they reach the game here

const char* WindowName(mobile::Window w) {
    switch (w) {
        case mobile::Window::Menu:      return "menu";
        case mobile::Window::Character: return "character";
        case mobile::Window::Club:      return "club";
        case mobile::Window::Skill:     return "skill";
        case mobile::Window::Bag:       return "bag";
        default:                        return "none";
    }
}

// ---------- shaders ----------

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;   // pixels, origin top-left
uniform vec2 uScreen;
void main() {
    vec2 ndc = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

const char* kTextVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;   // pixels, origin top-left
layout(location = 1) in vec2 aUV;
uniform vec2 uScreen;
out vec2 vUV;
void main() {
    vec2 ndc = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    vUV = aUV;
}
)";

const char* kTextFragmentSrc = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;   // alpha-only label bitmap
uniform vec4 uColor;
in vec2 vUV;
out vec4 fragColor;
void main() {
    float a = texture(uTex, vUV).r;
    fragColor = vec4(uColor.rgb, uColor.a * a);
}
)";

GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint LinkProgram(const char* vsSrc, const char* fsSrc, bool textured) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    if (textured) glBindAttribLocation(prog, 1, "aUV");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Reads assets/labels.bin and uploads each label as an alpha texture.
// On failure the HUD still runs; the labels are just not drawn.
void LoadLabels(Game& g) {
    AAsset* asset = AAssetManager_open(g.app->activity->assetManager, "labels.bin", AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("labels.bin missing from assets");
        return;
    }
    const auto* data = static_cast<const unsigned char*>(AAsset_getBuffer(asset));
    const size_t size = size_t(AAsset_getLength(asset));
    size_t pos = 0;
    auto readU32 = [&](uint32_t& out) -> bool {
        if (!data || pos + 4 > size) return false;
        out = uint32_t(data[pos]) | (uint32_t(data[pos + 1]) << 8) |
              (uint32_t(data[pos + 2]) << 16) | (uint32_t(data[pos + 3]) << 24);
        pos += 4;
        return true;
    };

    uint32_t count = 0;
    bool ok = readU32(count) && count == static_cast<uint32_t>(kLblCount);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; ok && i < kLblCount; ++i) {
        uint32_t w = 0, h = 0;
        ok = readU32(w) && readU32(h) && w > 0 && h > 0 && pos + size_t(w) * h <= size;
        if (!ok) break;
        glGenTextures(1, &g.labelTex[i]);
        glBindTexture(GL_TEXTURE_2D, g.labelTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, int(w), int(h), 0, GL_RED, GL_UNSIGNED_BYTE, data + pos);
        g.labelW[i] = int(w);
        g.labelH[i] = int(h);
        pos += size_t(w) * h;
    }
    AAsset_close(asset);
    if (!ok) LOGE("labels.bin is invalid");
    else LOGI("labels loaded: %d", kLblCount);
}

bool CreateGlResources(Game& g) {
    g.program = LinkProgram(kVertexSrc, kFragmentSrc, false);
    g.textProgram = LinkProgram(kTextVertexSrc, kTextFragmentSrc, true);
    if (!g.program || !g.textProgram) return false;

    g.uScreen = glGetUniformLocation(g.program, "uScreen");
    g.uColor = glGetUniformLocation(g.program, "uColor");
    g.tScreen = glGetUniformLocation(g.textProgram, "uScreen");
    g.tColor = glGetUniformLocation(g.textProgram, "uColor");
    g.tSampler = glGetUniformLocation(g.textProgram, "uTex");

    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    LoadLabels(g);
    return true;
}

// ---------- bag and potions ----------

// Pushes the bag contents to the HUD. Sample items other than potions are placeholders.
void RefreshBag(Game& g) {
    int ids[mobile::TouchHUD::kBagCells] = {};
    int counts[mobile::TouchHUD::kBagCells] = {};
    if (g.potions > 0) {
        ids[0] = kItemPotion;
        counts[0] = g.potions;
    }
    ids[1] = 2; counts[1] = 3;   // sample item
    ids[2] = 3; counts[2] = 1;   // sample item
    g.hud.SetBagItems(ids, counts, mobile::TouchHUD::kBagCells);
}

// Drinks one potion from the bag. Returns false if there are none.
bool DrinkPotion(Game& g) {
    if (g.potions <= 0) {
        LOGI("no potions");
        return false;
    }
    --g.potions;
    g.hp = std::min(g.hpMax, g.hp + 0.4f * g.hpMax);   // placeholder: heal 40%
    RefreshBag(g);
    LOGI("use potion hp=%.0f left=%d", g.hp, g.potions);
    return true;
}

// ---------- HUD setup (first start; the size is reapplied after every resize) ----------

void SetupHud(Game& g) {
    const float w = static_cast<float>(g.width);
    const float h = static_cast<float>(g.height);
    if (!g.hudReady) {
        mobile::Hooks hooks;
        hooks.MoveDir = [](float dx, float dy) {
            if (g_game) { g_game->moveX = dx; g_game->moveY = dy; }
        };
        hooks.BasicAttack = []() {
            if (g_game) g_game->attackFlash = 0.25f;
            LOGI("basic attack");
        };
        hooks.UseSkill = [](int skillId) {
            LOGI("use skill %d", skillId);
            if (!g_game) return;
            for (int i = 0; i < mobile::TouchHUD::kSlots; ++i)
                if (g_game->hud.SlotSkill(i) == skillId) g_game->slotFlash[i] = 0.3f;
        };
        hooks.UsePotion = []() -> bool {
            return g_game != nullptr && DrinkPotion(*g_game);
        };
        hooks.UseItem = [](int itemId) {
            if (itemId == kItemPotion) {
                if (g_game) DrinkPotion(*g_game);
            } else {
                LOGI("use item %d", itemId);
            }
        };
        hooks.OnWindow = [](mobile::Window w) {
            if (w == mobile::Window::None) LOGI("window closed");
            else LOGI("window open %s", WindowName(w));
        };
        hooks.OnRunChanged = [](bool running) {
            if (running) LOGI("run on");
            else LOGI("run off");
        };
        g.hud.Init(w, h, g.dp, hooks);
        for (int i = 0; i < mobile::TouchHUD::kSlots; ++i) g.hud.SetSlot(i, i + 1);
        int skills[mobile::TouchHUD::kSkillCells];
        for (int i = 0; i < mobile::TouchHUD::kSkillCells; ++i) skills[i] = kFirstSkillId + i;
        g.hud.SetSkills(skills, mobile::TouchHUD::kSkillCells);
        RefreshBag(g);
        g.hudReady = true;
    } else {
        g.hud.Resize(w, h, g.dp);
    }
    const mobile::Vec2 bag = g.hud.ButtonCenter(mobile::Button::Bag);
    const mobile::Vec2 run = g.hud.ButtonCenter(mobile::Button::Run);
    LOGI("hud bag=%.0f,%.0f run=%.0f,%.0f", bag.x, bag.y, run.x, run.y);
}

// Reads the window size and pixel density. Returns true if the size changed.
bool UpdateSize(Game& g) {
    const int w = ANativeWindow_getWidth(g.app->window);
    const int h = ANativeWindow_getHeight(g.app->window);
    float density = static_cast<float>(AConfiguration_getDensity(g.app->config));
    if (density <= 0.f || density == static_cast<float>(ACONFIGURATION_DENSITY_ANY)) density = 160.f;
    const bool changed = (w != g.width || h != g.height);
    g.width = w;
    g.height = h;
    g.dp = density / 160.f;
    return changed;
}

// ---------- EGL ----------

bool InitEgl(Game& g) {
    ANativeWindow* window = g.app->window;
    g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g.display == EGL_NO_DISPLAY || !eglInitialize(g.display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint cfgAttr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE};
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(g.display, cfgAttr, &config, 1, &numConfigs) || numConfigs < 1) {
        LOGE("no GLES 3 config");
        return false;
    }

    EGLint visualId = 0;
    eglGetConfigAttrib(g.display, config, EGL_NATIVE_VISUAL_ID, &visualId);
    ANativeWindow_setBuffersGeometry(window, 0, 0, visualId);

    g.surface = eglCreateWindowSurface(g.display, config, window, nullptr);
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g.context = eglCreateContext(g.display, config, EGL_NO_CONTEXT, ctxAttr);
    if (g.surface == EGL_NO_SURFACE || g.context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(g.display, g.surface, g.surface, g.context)) {
        LOGE("EGL surface/context setup failed");
        return false;
    }

    UpdateSize(g);
    if (!CreateGlResources(g)) return false;
    SetupHud(g);

    LOGI("EGL ready: %dx%d dp=%.2f", g.width, g.height, g.dp);
    return true;
}

// Order matters: delete GL objects while the context is still current.
void TermEgl(Game& g) {
    if (g.display == EGL_NO_DISPLAY) return;
    if (g.context != EGL_NO_CONTEXT && g.surface != EGL_NO_SURFACE &&
        eglMakeCurrent(g.display, g.surface, g.surface, g.context)) {
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
        if (g.program) glDeleteProgram(g.program);
        if (g.textProgram) glDeleteProgram(g.textProgram);
        glDeleteTextures(kLblCount, g.labelTex);
        g.vbo = 0;
        g.program = 0;
        g.textProgram = 0;
        for (GLuint& t : g.labelTex) t = 0;
        for (int i = 0; i < kLblCount; ++i) g.labelW[i] = g.labelH[i] = 0;
    }
    eglMakeCurrent(g.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g.context != EGL_NO_CONTEXT) eglDestroyContext(g.display, g.context);
    if (g.surface != EGL_NO_SURFACE) eglDestroySurface(g.display, g.surface);
    eglTerminate(g.display);
    g.display = EGL_NO_DISPLAY;
    g.context = EGL_NO_CONTEXT;
    g.surface = EGL_NO_SURFACE;
}

// ---------- drawing ----------

void BindPlain(const Game& g) {
    glUseProgram(g.program);
    glUniform2f(g.uScreen, float(g.width), float(g.height));
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
}

// Text vertices are {x, y, u, v} (16 bytes).
void BindText(const Game& g) {
    glUseProgram(g.textProgram);
    glUniform2f(g.tScreen, float(g.width), float(g.height));
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<const void*>(8));
}

void DrawCircle(const Game& g, float cx, float cy, float r, float red, float green, float blue, float alpha) {
    BindPlain(g);
    constexpr int kSeg = 32;
    float verts[(kSeg + 2) * 2];
    verts[0] = cx;
    verts[1] = cy;
    for (int i = 0; i <= kSeg; ++i) {
        float a = 2.f * kPi * float(i) / float(kSeg);
        verts[2 + 2 * i] = cx + r * std::cos(a);
        verts[3 + 2 * i] = cy + r * std::sin(a);
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glUniform4f(g.uColor, red, green, blue, alpha);
    glDrawArrays(GL_TRIANGLE_FAN, 0, kSeg + 2);
}

void DrawRect(const Game& g, float x, float y, float w, float h, float red, float green, float blue, float alpha) {
    BindPlain(g);
    const float verts[8] = {x, y, x + w, y, x, y + h, x + w, y + h};
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glUniform4f(g.uColor, red, green, blue, alpha);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// Size of a label drawn with its em size at fontDp, shrunk to fit maxWidthDp.
void LabelSize(const Game& g, int id, float fontDp, float maxWidthDp, float& outW, float& outH) {
    outW = outH = 0.f;
    if (id < 0 || id >= kLblCount || g.labelW[id] == 0) return;
    float scale = fontDp * g.dp / float(kLabelFontPx);
    const float maxW = maxWidthDp * g.dp;
    if (g.labelW[id] * scale > maxW) scale = maxW / float(g.labelW[id]);
    outW = g.labelW[id] * scale;
    outH = g.labelH[id] * scale;
}

// Draws a label centred on (cx, cy).
void DrawLabel(const Game& g, int id, float cx, float cy, float fontDp, float maxWidthDp,
               float red, float green, float blue, float alpha) {
    float w = 0.f, h = 0.f;
    LabelSize(g, id, fontDp, maxWidthDp, w, h);
    if (w <= 0.f) return;
    const float x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
    const float verts[24] = {
        x0,     y0,     0.f, 0.f,
        x0 + w, y0,     1.f, 0.f,
        x0,     y0 + h, 0.f, 1.f,
        x0 + w, y0,     1.f, 0.f,
        x0 + w, y0 + h, 1.f, 1.f,
        x0,     y0 + h, 0.f, 1.f,
    };
    BindText(g);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.labelTex[id]);
    glUniform1i(g.tSampler, 0);
    glUniform4f(g.tColor, red, green, blue, alpha);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Draws a whole number centred on (cx, cy) from the digit labels.
void DrawNumber(const Game& g, int value, float cx, float cy, float fontDp,
                float red, float green, float blue, float alpha) {
    char text[12];
    std::snprintf(text, sizeof(text), "%d", value);
    const int n = int(std::strlen(text));
    float widths[12] = {};
    float total = 0.f;
    for (int i = 0; i < n; ++i) {
        float h = 0.f;
        LabelSize(g, kLblDigit0 + (text[i] - '0'), fontDp, 1000.f, widths[i], h);
        total += widths[i];
    }
    float x = cx - total * 0.5f;
    for (int i = 0; i < n; ++i) {
        DrawLabel(g, kLblDigit0 + (text[i] - '0'), x + widths[i] * 0.5f, cy, fontDp, 1000.f,
                  red, green, blue, alpha);
        x += widths[i];
    }
}

// Colour of each small button when it is off (on-state colours are set in DrawHud).
struct ButtonStyle {
    mobile::Button button;
    float r, g, b;
};

constexpr ButtonStyle kButtonStyles[] = {
    {mobile::Button::Menu,          0.35f, 0.35f, 0.40f},
    {mobile::Button::Character,     0.25f, 0.45f, 0.65f},
    {mobile::Button::Bag,           0.45f, 0.35f, 0.20f},
    {mobile::Button::Skill,         0.25f, 0.55f, 0.35f},
    {mobile::Button::Run,           0.30f, 0.30f, 0.30f},
    {mobile::Button::Potion,        0.80f, 0.20f, 0.25f},
    {mobile::Button::AutoPotion,    0.30f, 0.30f, 0.30f},
    {mobile::Button::PotionPercent, 0.30f, 0.30f, 0.30f},
};

int ButtonLabel(const Game& g, mobile::Button b);

// Number label ("1".."6") for a skill id, or -1 if it has none.
int SkillLabel(int skillId) {
    const int n = skillId - kFirstSkillId;
    return (n >= 0 && n < mobile::TouchHUD::kSkillCells) ? kLblDigit1 + n : -1;
}

void DrawHud(Game& g) {
    const mobile::TouchHUD& hud = g.hud;

    // HP bar at the top centre (placeholder character).
    const float w = static_cast<float>(g.width);
    const float barW = 300.f * g.dp, barH = 14.f * g.dp;
    const float barX = w * 0.5f - barW * 0.5f, barY = 24.f * g.dp;
    const float ratio = hud.HpRatio();
    DrawRect(g, barX, barY, barW, barH, 0.10f, 0.10f, 0.12f, 0.85f);
    DrawRect(g, barX, barY, barW * ratio, barH, 0.85f * (1.f - ratio) + 0.15f, 0.25f + 0.6f * ratio, 0.25f, 0.95f);
    float hpW = 0.f, hpH = 0.f;
    LabelSize(g, kLblHp, 12.f, 60.f, hpW, hpH);
    DrawLabel(g, kLblHp, barX - 8.f * g.dp - hpW * 0.5f, barY + barH * 0.5f, 12.f, 60.f, 1.f, 1.f, 1.f, 0.95f);

    // Skill slots.
    for (int i = 0; i < mobile::TouchHUD::kSlots; ++i) {
        const mobile::Vec2 p = hud.SlotCenter(i);
        DrawCircle(g, p.x, p.y, hud.SlotRadius(), 0.15f, 0.15f, 0.20f, 0.90f);
        if (hud.SlotSkill(i) > 0) {
            DrawCircle(g, p.x, p.y, hud.SlotRadius() * 0.6f, 0.90f, 0.60f, 0.20f, 1.f);
            DrawLabel(g, SkillLabel(hud.SlotSkill(i)), p.x, p.y, 12.f, 30.f, 0.1f, 0.1f, 0.1f, 1.f);
        }
        if (hud.Dragging()) {
            DrawCircle(g, p.x, p.y, hud.SlotRadius(), 1.f, 0.80f, 0.30f, 0.35f);   // drop target
        }
        if (g.slotFlash[i] > 0.f) {
            DrawCircle(g, p.x, p.y, hud.SlotRadius(), 1.f, 1.f, 1.f, g.slotFlash[i] * 2.f);
        }
    }

    // Attack button.
    const mobile::Vec2 atk = hud.AttackCenter();
    DrawCircle(g, atk.x, atk.y, hud.AttackRadius(), 0.20f, 0.60f, 1.00f, 0.85f);

    // Small buttons.
    const float br = hud.ButtonRadius();
    for (const ButtonStyle& s : kButtonStyles) {
        const mobile::Vec2 c = hud.ButtonCenter(s.button);
        float r = s.r, gr = s.g, b = s.b;
        if (hud.ButtonOn(s.button)) { r = 0.20f; gr = 0.75f; b = 0.35f; }   // toggled on
        DrawCircle(g, c.x, c.y, br, r, gr, b, 0.88f);
        DrawLabel(g, ButtonLabel(g, s.button), c.x, c.y, 11.f, 40.f, 1.f, 1.f, 1.f, 0.97f);
        if (s.button == mobile::Button::Potion && hud.PotionCooldownLeft() > 0.f) {
            const float frac = hud.PotionCooldownLeft() / mobile::TouchHUD::kPotionCooldown;
            DrawCircle(g, c.x, c.y, br, 0.f, 0.f, 0.f, 0.6f * frac);
        }
    }

    // Joystick: the base is always visible (bottom-left); the knob follows the thumb.
    const mobile::Vec2 jb = hud.JoystickOrigin();
    const mobile::Vec2 jk = hud.JoystickKnob();
    const bool active = hud.JoystickActive();
    DrawCircle(g, jb.x, jb.y, hud.JoystickRadius(), 1.f, 1.f, 1.f, active ? 0.22f : 0.12f);
    DrawCircle(g, jk.x, jk.y, 26.f * g.dp, 1.f, 1.f, 1.f, active ? 0.70f : 0.35f);
}

int ButtonLabel(const Game& g, mobile::Button b) {
    switch (b) {
        case mobile::Button::Menu:          return kLblMenu;
        case mobile::Button::Character:     return kLblCharacter;
        case mobile::Button::Bag:           return kLblBag;
        case mobile::Button::Skill:         return kLblSkill;
        case mobile::Button::Run:           return g.hud.Running() ? kLblRun : kLblWalk;
        case mobile::Button::Potion:        return kLblPotion;
        case mobile::Button::AutoPotion:    return kLblAuto;
        case mobile::Button::PotionPercent: return kLblPercent10 + g.hud.PotionPercent() / 10 - 1;
    }
    return kLblMenu;
}

// Full-screen panel drawn over everything while one is open.
void DrawWindow(Game& g) {
    const mobile::TouchHUD& hud = g.hud;
    const mobile::Window win = hud.OpenWindow();
    if (win == mobile::Window::None) return;
    if (hud.Dragging()) return;   // hide the panel so the quick slots can be seen

    const float w = static_cast<float>(g.width);
    const float h = static_cast<float>(g.height);
    DrawRect(g, 0.f, 0.f, w, h, 0.f, 0.f, 0.f, 0.55f);

    const mobile::Rect p = hud.PanelRect();
    float pr = 0.25f, pg = 0.25f, pb = 0.25f;
    int title = kLblMenu;
    switch (win) {
        case mobile::Window::Bag:       pr = 0.35f; pg = 0.25f; pb = 0.15f; title = kLblBag; break;
        case mobile::Window::Character: pr = 0.15f; pg = 0.30f; pb = 0.45f; title = kLblCharacter; break;
        case mobile::Window::Club:      pr = 0.30f; pg = 0.20f; pb = 0.40f; title = kLblClub; break;
        case mobile::Window::Skill:     pr = 0.20f; pg = 0.35f; pb = 0.25f; title = kLblSkill; break;
        default:                        title = kLblMenu; break;
    }
    DrawRect(g, p.x, p.y, p.w, p.h, pr, pg, pb, 0.95f);

    const float pad = 20.f * g.dp;
    const float top = p.y + 64.f * g.dp;   // below the title and close button row
    {
        float tw = 0.f, th = 0.f;
        LabelSize(g, title, 18.f, p.w * 0.6f, tw, th);
        DrawLabel(g, title, p.x + pad + tw * 0.5f, p.y + pad + th * 0.5f, 18.f, p.w * 0.6f, 1.f, 1.f, 1.f, 0.95f);
    }
    switch (win) {
        case mobile::Window::Bag: {          // 6 x 4 item cells with counts
            for (int i = 0; i < mobile::TouchHUD::kBagCells; ++i) {
                const mobile::Vec2 c = hud.BagCellCenter(i);
                const int id = hud.BagItemAt(i);
                if (id <= 0) {
                    DrawCircle(g, c.x, c.y, hud.BagCellRadius(), 0.12f, 0.12f, 0.14f, 0.95f);
                    continue;
                }
                const bool potion = (id == kItemPotion);
                DrawCircle(g, c.x, c.y, hud.BagCellRadius(),
                           potion ? 0.80f : 0.30f, potion ? 0.20f : 0.50f, potion ? 0.25f : 0.70f, 0.95f);
                DrawNumber(g, hud.BagCountAt(i), c.x, c.y, 10.f, 1.f, 1.f, 1.f, 0.97f);
            }
            break;
        }
        case mobile::Window::Character: {    // HP label and bar
            const float bw = p.w - 2 * pad;
            const float ratio = hud.HpRatio();
            float hw = 0.f, hh = 0.f;
            LabelSize(g, kLblHp, 14.f, bw, hw, hh);
            DrawLabel(g, kLblHp, p.x + pad + hw * 0.5f, top + hh * 0.5f, 14.f, bw, 1.f, 1.f, 1.f, 0.95f);
            const float barY = top + hh + 8.f * g.dp;
            DrawRect(g, p.x + pad, barY, bw, 28.f * g.dp, 0.10f, 0.10f, 0.12f, 0.9f);
            DrawRect(g, p.x + pad, barY, bw * ratio, 28.f * g.dp, 0.85f * (1.f - ratio) + 0.15f, 0.25f + 0.6f * ratio, 0.25f, 0.95f);
            // Equipment slots: empty boxes until the server reports what is equipped.
            const float slotTop = barY + 28.f * g.dp + 16.f * g.dp;
            const float slotGap = 16.f * g.dp;
            const float slotW = (bw - slotGap) * 0.5f;
            const float slotH = std::max(0.f, p.y + p.h - pad - slotTop - 30.f * g.dp);
            const int titles[2] = { kLblWeapon, kLblArmor };
            for (int s = 0; s < 2; ++s) {
                const float sx = p.x + pad + s * (slotW + slotGap);
                float tw = 0.f, th = 0.f;
                LabelSize(g, titles[s], 14.f, slotW, tw, th);
                DrawLabel(g, titles[s], sx + slotW * 0.5f, slotTop + th * 0.5f, 14.f, slotW, 1.f, 1.f, 1.f, 0.95f);
                DrawRect(g, sx, slotTop + th + 8.f * g.dp, slotW, slotH, 0.10f, 0.10f, 0.12f, 0.9f);
            }
            break;
        }
        case mobile::Window::Skill: {        // skill grid: drag a skill onto a quick slot
            for (int i = 0; i < mobile::TouchHUD::kSkillCells; ++i) {
                const mobile::Vec2 c = hud.SkillCellCenter(i);
                const int id = hud.SkillAt(i);
                if (id > 0) {
                    DrawCircle(g, c.x, c.y, hud.SkillCellRadius(), 0.90f, 0.60f, 0.20f, 0.95f);
                    DrawLabel(g, SkillLabel(id), c.x, c.y, 16.f, 40.f, 0.1f, 0.1f, 0.1f, 1.f);
                } else {
                    DrawCircle(g, c.x, c.y, hud.SkillCellRadius(), 0.10f, 0.10f, 0.12f, 0.95f);
                }
            }
            break;
        }
        case mobile::Window::Menu: {         // the club entry
            const mobile::Vec2 c = hud.MenuClubCenter();
            const float r = hud.MenuClubRadius();
            DrawCircle(g, c.x, c.y, r, 0.45f, 0.30f, 0.60f, 1.f);
            DrawLabel(g, kLblClub, c.x, c.y, 16.f, r * 1.6f, 1.f, 1.f, 1.f, 0.95f);
            break;
        }
        default:
            // The club window has no contents yet.
            break;
    }

    const mobile::Vec2 cc = hud.PanelCloseCenter();
    DrawCircle(g, cc.x, cc.y, hud.PanelCloseRadius(), 0.85f, 0.20f, 0.20f, 1.f);
}

// Copies the world session into the game when it changes, and applies the server's correction.
void SyncWorld(Game& g) {
    auto& world = mobile::net::World();
    mobile::net::WorldSession::Snapshot snap;
    if (world.Refresh(g.worldSeen, snap)) {
        if (snap.inWorld) {
            g.blocks = snap.blocks;
            g.playerX = float(snap.start.x);
            g.playerY = float(snap.start.y);
            g.playerPlaced = true;
            LOGI("world: %zu blocks, start %.3f %.3f", g.blocks.size(), g.playerX, g.playerY);
        } else {
            g.blocks = PlaceholderBlocks();
        }
    }
    mobile::net::WorldPoint corr;
    if (world.TakeCorrection(corr)) {
        g.playerX = float(corr.x);
        g.playerY = float(corr.y);
        LOGI("server correction %.3f %.3f", corr.x, corr.y);
    }
}

void DrawMap(const Game& g, float w, float h) {
    const float tile = kTileDp * g.dp;
    const int cols = int(std::ceil(w / tile)), rows = int(std::ceil(h / tile));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const bool dark = ((r + c) & 1) == 0;
            DrawRect(g, c * tile, r * tile, tile, tile,
                     dark ? 0.16f : 0.19f, dark ? 0.24f : 0.27f, dark ? 0.16f : 0.19f, 1.f);
        }
    for (const mobile::net::WorldRect& b : g.blocks)
        DrawRect(g, float(b.x) * w, float(b.y) * h, float(b.w) * w, float(b.h) * h, 0.36f, 0.33f, 0.30f, 1.f);
}

void DrawFrame(Game& g, float dt) {
    const float w = static_cast<float>(g.width);
    const float h = static_cast<float>(g.height);
    // Login and character screens are Java dialogs on top; the HUD and map show after Enter World.
    const bool inWorld = InWorld();

    glViewport(0, 0, g.width, g.height);
    glClearColor(0.05f, 0.07f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Placeholder hits: the character loses 15% every 6 s and respawns at 0.
    g.hpTimer += dt;
    if (g.hpTimer >= 6.f) {
        g.hpTimer = 0.f;
        g.hp -= 0.15f * g.hpMax;
        if (g.hp <= 0.f) {
            g.hp = g.hpMax;
            LOGI("respawn");
        } else {
            LOGI("hit hp=%.0f", g.hp);
        }
    }
    g.hud.SetHp(g.hp, g.hpMax);

    // World session: while connected, the server's map and start position; the server's
    // corrections are applied here (a rejected move snaps the player back).
    SyncWorld(g);
    if (!g.playerPlaced) {
        g.playerX = 0.5f;
        g.playerY = 0.5f;
        g.playerPlaced = true;
    }

    // Placeholder player: walks and runs in map fractions, the same units and speeds the server checks.
    const float speed = float(g.hud.Running() ? mobile::net::kRunSpeed : mobile::net::kWalkSpeed);
    float mx = g.moveX, my = g.moveY;
    const float len = std::hypot(mx, my);
    if (len > 1.f) {  // a diagonal push is never faster than a straight one
        mx /= len;
        my /= len;
    }
    const float prevX = g.playerX, prevY = g.playerY;
    const float radius = 18.f * g.dp;  // px
    const float marginX = 40.f * g.dp / w, marginY = 40.f * g.dp / h;  // fractions, keeps the player on screen
    // Move one axis at a time so the player slides along a block instead of sticking.
    const float nx = std::max(marginX, std::min(1.f - marginX, g.playerX + mx * speed * dt));
    if (!CircleBlocked(g.blocks, nx * w, g.playerY * h, radius, w, h)) g.playerX = nx;
    const float ny = std::max(marginY, std::min(1.f - marginY, g.playerY + my * speed * dt));
    if (!CircleBlocked(g.blocks, g.playerX * w, ny * h, radius, w, h)) g.playerY = ny;
    g.movedPx += std::hypot((g.playerX - prevX) * w, (g.playerY - prevY) * h);
    mobile::net::World().Publish(mobile::net::WorldPoint{g.playerX, g.playerY});

    if (inWorld) DrawMap(g, w, h);
    if (inWorld) DrawCircle(g, g.playerX * w, g.playerY * h, radius, 0.30f, 0.80f, 0.45f, 1.f);
    if (inWorld && g.attackFlash > 0.f) {
        g.attackFlash -= dt;
        DrawCircle(g, g.playerX * w, g.playerY * h, 42.f * g.dp, 1.f, 0.80f, 0.25f, g.attackFlash * 2.f);
    }
    for (float& f : g.slotFlash) {
        if (f > 0.f) f -= dt;
    }

    if (inWorld) DrawHud(g);

    // Joystick state changes are logged so the smoke test can see them.
    const bool active = g.hud.JoystickActive();
    if (active != g.joyWasActive) {
        g.joyWasActive = active;
        if (active) {
            g.movedPx = 0.f;
            LOGI("joystick start");
        } else {
            LOGI("joystick end, player moved %.0f px (moveX=%.2f)", g.movedPx, g.moveX);
        }
    }

    if (inWorld) DrawWindow(g);

    if (inWorld && g.hud.Dragging()) {   // the skill being dragged follows the finger
        const mobile::Vec2 d = g.hud.DragPos();
        DrawCircle(g, d.x, d.y, g.hud.SkillCellRadius(), 0.90f, 0.60f, 0.20f, 0.85f);
        DrawLabel(g, SkillLabel(g.hud.DragSkill()), d.x, d.y, 16.f, 40.f, 0.1f, 0.1f, 0.1f, 1.f);
    }

    const EGLBoolean swapped = eglSwapBuffers(g.display, g.surface);
    g.frames++;
    if (g.frames == 1) {
        LOGI("first frame drawn (swap=%d, gl error=0x%x)", int(swapped), glGetError());
    } else if (g.frames % 300 == 0) {
        LOGI("frames=%ld", g.frames);
    }
}

// ---------- input ----------

int32_t OnInputEvent(android_app* app, AInputEvent* event) {
    Game* g = static_cast<Game*>(app->userData);
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;

    const int32_t action = AMotionEvent_getAction(event);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    auto pointAt = [event](size_t i) {
        return mobile::Vec2{AMotionEvent_getX(event, i), AMotionEvent_getY(event, i)};
    };

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            const size_t idx = size_t((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            const mobile::Vec2 p = pointAt(idx);
            const bool used = g->hud.OnTouch(mobile::Touch::Down, AMotionEvent_getPointerId(event, idx), p);
            LOGI("touch down %.0f,%.0f used=%d", p.x, p.y, int(used));
            return 1;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            const size_t idx = size_t((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            const mobile::Vec2 p = pointAt(idx);
            g->hud.OnTouch(mobile::Touch::Up, AMotionEvent_getPointerId(event, idx), p);
            LOGI("touch up %.0f,%.0f", p.x, p.y);
            return 1;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            const size_t count = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < count; ++i) {
                g->hud.OnTouch(mobile::Touch::Move, AMotionEvent_getPointerId(event, i), pointAt(i));
            }
            return 1;
        }
        case AMOTION_EVENT_ACTION_CANCEL: {
            const size_t count = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < count; ++i) {
                g->hud.OnTouch(mobile::Touch::Up, AMotionEvent_getPointerId(event, i), pointAt(i));
            }
            return 1;
        }
        default:
            return 0;
    }
}

// ---------- lifecycle ----------

void OnAppCmd(android_app* app, int32_t cmd) {
    Game* g = static_cast<Game*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window && g->surface == EGL_NO_SURFACE) {
                if (!InitEgl(*g)) {
                    LOGE("InitEgl failed");
                    TermEgl(*g);
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            TermEgl(*g);
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (g->surface != EGL_NO_SURFACE && UpdateSize(*g)) {
                LOGI("resized: %dx%d", g->width, g->height);
                SetupHud(*g);
            }
            break;
        default:
            break;
    }
}

}  // namespace

void android_main(struct android_app* app) {
    Game game;
    game.app = app;
    app->userData = &game;
    app->onAppCmd = OnAppCmd;
    app->onInputEvent = OnInputEvent;
    g_game = &game;
    LOGI("android_main start");

    auto last = std::chrono::steady_clock::now();
    while (true) {
        // Drain pending events. The timeout is recomputed for every poll: block only
        // while there is no surface to draw to, and stop as soon as the queue is empty.
        // (Computing it once before the loop left the loop blocked right after the
        // window appeared, so nothing was ever drawn.)
        while (true) {
            const int timeoutMs = (game.surface != EGL_NO_SURFACE) ? 0 : -1;
            int events = 0;
            android_poll_source* source = nullptr;
            const int ret = ALooper_pollOnce(timeoutMs, nullptr, &events,
                                             reinterpret_cast<void**>(&source));
            if (ret < 0) break;  // timeout or error: nothing more to process now
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                TermEgl(game);
                g_game = nullptr;
                return;
            }
        }

        if (game.surface != EGL_NO_SURFACE) {
            const auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (dt > 0.1f) dt = 0.1f;  // avoid a big jump after a pause
            game.hud.Update(dt);
            DrawFrame(game, dt);
        } else {
            last = std::chrono::steady_clock::now();
        }
    }
}
