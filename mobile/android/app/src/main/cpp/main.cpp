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
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "asset_manifest.h"
#include "touch_hud.h"
#include "bridge_api.h"
#include "combat_session.h"
#include "world_map.h"
#include "world_realtime.h"
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
    kLblMiss = kLblDigit0 + 12,      // "พลาด" over a target that dodged the hit
    kLblLevel = kLblDigit0 + 13,     // character window labels
    kLblAtk = kLblDigit0 + 14,
    kLblDef = kLblDigit0 + 15,
    kLblMoney = kLblDigit0 + 16,
    kLblExp = kLblDigit0 + 17,
    kLblNoPotion = kLblDigit0 + 18,
    kLblCount = kLblDigit0 + 19,
};
constexpr int kLabelFontPx = 48;   // em size the labels were rasterised at
constexpr int kFirstSkillId = 101; // placeholder skill ids 101..106 (grid cells 1..6)
constexpr int kItemPotion = 1;     // placeholder item ids; the real item table comes from the server

// Draw/upload the verified placeholder textures. The loader, manifest and checksum verification run
// either way (and their result is logged); this switch only controls whether the accepted images are
// uploaded and drawn over the procedural floor/blocks.
constexpr bool kLoadPlaceholderTextures = true;

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
    GLuint spriteProgram = 0;  // RGBA placeholder textures (verified assets)
    GLuint vbo = 0;
    GLint uScreen = -1;
    GLint uColor = -1;
    GLint tScreen = -1;
    GLint tColor = -1;
    GLint tSampler = -1;
    GLint sScreen = -1;
    GLint sColor = -1;
    GLint sSampler = -1;
    GLuint labelTex[kLblCount] = {};
    int labelW[kLblCount] = {};
    int labelH[kLblCount] = {};

    mobile::TouchHUD hud;
    bool hudReady = false;
    // Manifest-verified placeholder assets (mobile/assets/). Textures are uploaded when present;
    // a missing or mismatched file only means the procedural drawing is used instead.
    mobile::assets::AssetStore assets;
    GLuint groundTex = 0;
    GLuint blockTex = 0;
    bool groundTexReady = false;
    bool blockTexReady = false;
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

    // Real gameplay numbers come from the server (mobile::net::View()). The placeholder hit timer is
    // used only before the first world sync (e.g. right after entering the world).
    float hp = 100.f;
    float hpMax = 100.f;
    float hpTimer = 0.f;      // seconds until the next placeholder hit
    int potions = 12;         // potions in the bag (placeholder stock until the server sends items)
    mobile::net::WorldView::Snapshot world;   // players, monsters, stats, floating numbers
    uint64_t worldSeenView = 0;               // reserved for change detection
    bool targetLocked = false;
    // Skill cooldown for the quick slots (seconds left), driven by the server's answers.
    float skillCd[mobile::TouchHUD::kSlots] = {};

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

// Fragment shader for the verified placeholder textures (RGBA, straight alpha).
const char* kSpriteFragmentSrc = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
uniform vec4 uColor;
in vec2 vUV;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTex, vUV);
    fragColor = vec4(t.rgb * uColor.rgb, t.a * uColor.a);
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

// ---------- placeholder assets (mobile/assets/make_placeholder_assets.py) ----------

// Android source for mobile::assets::Source: reads one file from the APK's assets directory.
bool ReadAssetFile(AAssetManager* mgr, const std::string& name, std::vector<uint8_t>& out, std::string& err) {
    AAsset* asset = AAssetManager_open(mgr, name.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        err = name + ": absent from assets";
        return false;
    }
    const auto* data = static_cast<const unsigned char*>(AAsset_getBuffer(asset));
    const size_t size = size_t(AAsset_getLength(asset));
    if (data && size > 0) out.assign(data, data + size);
    AAsset_close(asset);
    return true;
}

// Uploads a PHP1 placeholder image (magic "PHP1", u16 w, u16 h, RGBA8 rows) as an RGBA texture.
// Returns false (leaving the procedural drawing in place) when the data does not describe an image.
bool UploadPhp1(const std::vector<uint8_t>& bytes, GLuint& tex) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "PHP1", 4) != 0) return false;
    const int w = int(bytes[4]) | (int(bytes[5]) << 8);
    const int h = int(bytes[6]) | (int(bytes[7]) << 8);
    if (w <= 0 || h <= 0 || bytes.size() != 8 + size_t(w) * size_t(h) * 4) return false;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data() + 8);
    return true;
}

// Loads and verifies every manifest asset. Logs one line per problem (never fails the app) and one
// summary line the CI smoke test greps.
void LoadPlaceholderAssets(Game& g) {
    AAssetManager* mgr = g.app->activity->assetManager;
    if (!mgr) return;
    std::vector<uint8_t> manifest;
    std::string err;
    if (!ReadAssetFile(mgr, "assets_manifest.bin", manifest, err)) {
        LOGE("assets: %s (procedural placeholders only)", err.c_str());
        return;
    }
    if (!g.assets.LoadManifest(std::string(manifest.begin(), manifest.end()), err)) {
        LOGE("assets: manifest rejected: %s", err.c_str());
        return;
    }
    mobile::assets::Source source = [mgr](const std::string& name, std::vector<uint8_t>& out,
                                          std::string& e) { return ReadAssetFile(mgr, name, out, e); };
    std::map<std::string, std::vector<uint8_t>> loaded;
    const auto report = g.assets.LoadAll(source, loaded);
    for (const std::string& problem : report.problems) LOGE("assets: %s", problem.c_str());
    if (kLoadPlaceholderTextures) {
        const auto ground = loaded.find("ph_ground.bin");
        if (ground != loaded.end()) g.groundTexReady = UploadPhp1(ground->second, g.groundTex);
        const auto block = loaded.find("ph_block.bin");
        if (block != loaded.end()) g.blockTexReady = UploadPhp1(block->second, g.blockTex);
    }
    LOGI("placeholder assets: %d loaded, %d missing, %d broken (%zu listed)", report.loaded,
         report.missing, report.broken, g.assets.Entries().size());
}

bool CreateGlResources(Game& g) {
    g.program = LinkProgram(kVertexSrc, kFragmentSrc, false);
    g.textProgram = LinkProgram(kTextVertexSrc, kTextFragmentSrc, true);
    g.spriteProgram = LinkProgram(kTextVertexSrc, kSpriteFragmentSrc, true);
    if (!g.program || !g.textProgram || !g.spriteProgram) return false;

    g.uScreen = glGetUniformLocation(g.program, "uScreen");
    g.uColor = glGetUniformLocation(g.program, "uColor");
    g.tScreen = glGetUniformLocation(g.textProgram, "uScreen");
    g.tColor = glGetUniformLocation(g.textProgram, "uColor");
    g.tSampler = glGetUniformLocation(g.textProgram, "uTex");
    g.sScreen = glGetUniformLocation(g.spriteProgram, "uScreen");
    g.sColor = glGetUniformLocation(g.spriteProgram, "uColor");
    g.sSampler = glGetUniformLocation(g.spriteProgram, "uTex");

    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    LoadLabels(g);
    LoadPlaceholderAssets(g);
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
            BridgeRequestAttack();          // sent on the net thread by the next sync tick
        };
        hooks.UseSkill = [](int skillId) {
            if (!g_game) return;
            for (int i = 0; i < mobile::TouchHUD::kSlots; ++i)
                if (g_game->hud.SlotSkill(i) == skillId) {
                    g_game->slotFlash[i] = 0.3f;
                    if (g_game->skillCd[i] > 0.f) return;   // still cooling down (server's value)
                }
            BridgeRequestSkill(skillId);
        };
        // A tap on the playfield selects the monster under the finger (target lock).
        hooks.OnWorldTap = [](mobile::Vec2 p) {
            if (!g_game) return;
            long long best = 0;
            float bestDist = 1e9f;
            const float w = float(g_game->width), h = float(g_game->height);
            for (const auto& m : g_game->world.monsters) {
                if (m.dead) continue;
                const float mx = float(m.x) * w, my = float(m.y) * h;
                const float d = std::hypot(mx - p.x, my - p.y);
                if (d < 64.f * g_game->dp && d < bestDist) {
                    best = m.monsterId;
                    bestDist = d;
                }
            }
            if (best != 0) {
                BridgeSelectTarget(best);
                if (g_game) g_game->targetLocked = true;
                LOGI("tap select monster %lld", best);
            } else {
                BridgeSelectTarget(0);      // tapping empty ground clears the lock
                if (g_game) g_game->targetLocked = false;
                LOGI("tap select cleared");
            }
        };
        hooks.UsePotion = []() -> bool {
            if (!g_game) return false;
            // In the world the server owns HP: the potion is an inventory action (slot 0) and the
            // answer carries the new HP/MP. Offline the placeholder bag is used.
            if (InWorld()) {
                if (BridgeUsePotion(0)) {
                    LOGI("use potion via server");
                    return true;
                }
                LOGI("no potion in slot 0");
                return false;
            }
            return DrinkPotion(*g_game);
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
            // The round-4 panels list server data (party, NPCs, chat) that native widgets show, so
            // Java opens its dialog when the HUD switches to them. The HUD panel itself stays the
            // touch owner; Java defers its dialog by one frame to stay off the input dispatch.
            if (w == mobile::Window::Social) BridgeOpenSocialPanel();
            else if (w == mobile::Window::Chat) BridgeOpenChatPanel();
        };
        // Tapping a row of the social panel: NPC -> talk (then the shop dialog), gate -> use.
        hooks.OnSocialTap = [](int kind, long long id) {
            if (kind == 0) {
                if (BridgeTalkToNpc(id)) BridgeOpenNpcDialog();
            } else if (kind == 1) {
                BridgeUseGate(id);
            }
        };
        hooks.OnPartyAccept = [](long long partyId) {
            if (BridgePartyAction(0, partyId)) BridgeOpenSocialPanel();
        };
        hooks.OnChatSend = [](int channel) {
            if (!g_game) return;
            BridgeSendChat(g_game->hud.ChatInput(), channel);
            g_game->hud.SetChatInput("");
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
        if (g.spriteProgram) glDeleteProgram(g.spriteProgram);
        glDeleteTextures(kLblCount, g.labelTex);
        if (g.groundTex) glDeleteTextures(1, &g.groundTex);
        if (g.blockTex) glDeleteTextures(1, &g.blockTex);
        g.groundTex = g.blockTex = 0;
        g.groundTexReady = g.blockTexReady = false;
        g.vbo = 0;
        g.program = 0;
        g.textProgram = 0;
        g.spriteProgram = 0;
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

// Radius (px) of a player circle: players and monsters are drawn in the same unit as the player.
float ActorRadius(const Game& g) { return 18.f * g.dp; }

// A small HP bar above an actor (red under green), width proportional to the actor size.
void DrawHpBar(const Game& g, float cx, float cy, float width, float ratio, float height) {
    const float x = cx - width * 0.5f;
    DrawRect(g, x, cy, width, height, 0.08f, 0.08f, 0.10f, 0.85f);
    DrawRect(g, x, cy, width * std::max(0.f, std::min(1.f, ratio)), height,
             ratio > 0.5f ? 0.25f : 0.85f, ratio > 0.5f ? 0.80f : 0.30f, 0.30f, 0.95f);
}

// Colour per monster AI state, so the player can read what the monsters are doing.
void MonsterColor(const std::string& state, float& r, float& gr, float& b) {
    if (state == "chase" || state == "attack") { r = 0.85f; gr = 0.35f; b = 0.25f; }
    else if (state == "patrol") { r = 0.70f; gr = 0.55f; b = 0.20f; }
    else if (state == "return") { r = 0.45f; gr = 0.45f; b = 0.60f; }
    else { r = 0.55f; gr = 0.30f; b = 0.65f; }
}

int ButtonLabel(const Game& g, mobile::Button b);

// Number label ("1".."6") for a skill id, or -1 if it has none.
int SkillLabel(int skillId) {
    const int n = skillId - kFirstSkillId;
    return (n >= 0 && n < mobile::TouchHUD::kSkillCells) ? kLblDigit1 + n : -1;
}

void DrawHud(Game& g) {
    const mobile::TouchHUD& hud = g.hud;

    // HP / MP / EXP bars at the top centre, with the level badge on the left.
    const float w = static_cast<float>(g.width);
    const float barW = 300.f * g.dp, barH = 12.f * g.dp;
    const float barX = w * 0.5f - barW * 0.5f, barY = 20.f * g.dp;
    const float ratio = hud.HpRatio();
    DrawRect(g, barX, barY, barW, barH, 0.10f, 0.10f, 0.12f, 0.85f);
    DrawRect(g, barX, barY, barW * ratio, barH, 0.85f * (1.f - ratio) + 0.15f, 0.25f + 0.6f * ratio, 0.25f, 0.95f);
    // MP (blue) and EXP (green) under the HP bar.
    const float mpY = barY + barH + 3.f * g.dp;
    DrawRect(g, barX, mpY, barW, barH * 0.7f, 0.10f, 0.10f, 0.12f, 0.85f);
    DrawRect(g, barX, mpY, barW * hud.MpRatio(), barH * 0.7f, 0.25f, 0.45f, 0.90f, 0.95f);
    const float expY = mpY + barH * 0.7f + 3.f * g.dp;
    DrawRect(g, barX, expY, barW, barH * 0.5f, 0.10f, 0.10f, 0.12f, 0.85f);
    DrawRect(g, barX, expY, barW * hud.ExpRatio(), barH * 0.5f, 0.30f, 0.85f, 0.40f, 0.95f);
    float hpW = 0.f, hpH = 0.f;
    LabelSize(g, kLblHp, 12.f, 60.f, hpW, hpH);
    DrawLabel(g, kLblHp, barX - 8.f * g.dp - hpW * 0.5f, barY + barH * 0.5f, 12.f, 60.f, 1.f, 1.f, 1.f, 0.95f);
    // Level badge left of the bars.
    DrawCircle(g, barX - 26.f * g.dp, barY + barH * 0.5f, 14.f * g.dp, 0.20f, 0.20f, 0.25f, 0.9f);
    DrawNumber(g, hud.Level(), barX - 26.f * g.dp, barY + barH * 0.5f, 11.f, 1.f, 1.f, 1.f, 1.f);

    // Locked target: its name plate is its HP bar above the monster (drawn in DrawWorldActors) and a
    // small bar under the HUD bars here.
    if (hud.TargetId() != 0) {
        const float tw = 120.f * g.dp;
        const float ty = expY + 6.f * g.dp;
        DrawHpBar(g, w * 0.5f, ty, tw, hud.TargetHpRatio(), 5.f * g.dp);
    }

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
        const float cd = hud.SkillCooldown(i);
        if (cd > 0.f) {   // cooling down: darken the slot, the renderer keeps a short sweep
            const float frac = std::min(1.f, cd / 9.f);
            DrawCircle(g, p.x, p.y, hud.SlotRadius(), 0.f, 0.f, 0.f, 0.55f * frac + 0.2f);
        }
        if (g.slotFlash[i] > 0.f) {
            DrawCircle(g, p.x, p.y, hud.SlotRadius(), 1.f, 1.f, 1.f, g.slotFlash[i] * 2.f);
        }
    }

    // Attack button: brighter when a target is locked.
    const mobile::Vec2 atk = hud.AttackCenter();
    if (hud.TargetId() != 0) DrawCircle(g, atk.x, atk.y, hud.AttackRadius() * 1.12f, 1.f, 0.85f, 0.30f, 0.55f);
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
        case mobile::Window::Character: {    // real stats from the server + equipment slots
            const float bw = p.w - 2 * pad;
            const float ratio = hud.HpRatio();
            float hw = 0.f, hh = 0.f;
            LabelSize(g, kLblHp, 14.f, bw, hw, hh);
            DrawLabel(g, kLblHp, p.x + pad + hw * 0.5f, top + hh * 0.5f, 14.f, bw, 1.f, 1.f, 1.f, 0.95f);
            const float barY = top + hh + 8.f * g.dp;
            DrawRect(g, p.x + pad, barY, bw, 20.f * g.dp, 0.10f, 0.10f, 0.12f, 0.9f);
            DrawRect(g, p.x + pad, barY, bw * ratio, 20.f * g.dp, 0.85f * (1.f - ratio) + 0.15f,
                     0.25f + 0.6f * ratio, 0.25f, 0.95f);
            // MP and EXP under it (the same numbers the top bars show).
            const float mpY = barY + 24.f * g.dp;
            DrawRect(g, p.x + pad, mpY, bw, 12.f * g.dp, 0.10f, 0.10f, 0.12f, 0.9f);
            DrawRect(g, p.x + pad, mpY, bw * hud.MpRatio(), 12.f * g.dp, 0.25f, 0.45f, 0.90f, 0.95f);
            const float expY = mpY + 16.f * g.dp;
            DrawRect(g, p.x + pad, expY, bw, 10.f * g.dp, 0.10f, 0.10f, 0.12f, 0.9f);
            DrawRect(g, p.x + pad, expY, bw * hud.ExpRatio(), 10.f * g.dp, 0.30f, 0.85f, 0.40f, 0.95f);
            // Level / ATK / DEF / money, one row each, from world.player.
            const mobile::net::PlayerStats& ps = g.world.stats;
            struct Row { int label; int value; };
            const Row rows[4] = {{kLblLevel, ps.level}, {kLblAtk, ps.atk}, {kLblDef, ps.def},
                                 {kLblMoney, ps.money}};
            float rowY = expY + 26.f * g.dp;
            for (const Row& row : rows) {
                float tw = 0.f, th = 0.f;
                LabelSize(g, row.label, 14.f, bw * 0.5f, tw, th);
                DrawLabel(g, row.label, p.x + pad + tw * 0.5f, rowY + th * 0.5f, 14.f, bw * 0.5f,
                          1.f, 1.f, 1.f, 0.95f);
                DrawNumber(g, row.value, p.x + pad + bw * 0.75f, rowY + th * 0.5f, 14.f, 1.f, 1.f, 1.f, 0.95f);
                rowY += th + 6.f * g.dp;
            }
            // Equipment slots: empty boxes until the server reports what is equipped.
            const float slotTop = rowY + 8.f * g.dp;
            const float slotGap = 16.f * g.dp;
            const float slotW = (bw - slotGap) * 0.5f;
            const float slotH = std::max(0.f, p.y + p.h - pad - slotTop - 12.f * g.dp);
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

// Monsters, other players, the locked target ring and the floating damage numbers.
void DrawWorldActors(Game& g, float w, float h) {
    const float radius = ActorRadius(g);
    // Other players (blue) with their name and level drawn as digits.
    for (const auto& p : g.world.players) {
        const float x = float(p.x) * w, y = float(p.y) * h;
        DrawCircle(g, x, y, radius, 0.30f, 0.55f, 0.90f, 1.f);
        DrawHpBar(g, x, y - radius - 6.f * g.dp, radius * 2.f,
                  p.maxHp > 0 ? float(p.hp) / float(p.maxHp) : 1.f, 4.f * g.dp);
        DrawNumber(g, p.level, x, y - radius - 18.f * g.dp, 10.f, 1.f, 1.f, 1.f, 0.95f);
    }
    // Monsters with an HP bar and a state colour; the locked one gets a ring.
    for (const auto& m : g.world.monsters) {
        if (m.dead) continue;
        const float x = float(m.x) * w, y = float(m.y) * h;
        float r = 0.5f, gr = 0.3f, b = 0.5f;
        MonsterColor(m.state, r, gr, b);
        DrawCircle(g, x, y, radius * 0.9f, r, gr, b, 1.f);
        DrawHpBar(g, x, y - radius * 0.9f - 6.f * g.dp, radius * 1.8f,
                  m.maxHp > 0 ? float(m.hp) / float(m.maxHp) : 1.f, 4.f * g.dp);
        DrawNumber(g, m.level, x, y - radius * 0.9f - 18.f * g.dp, 9.f, 1.f, 1.f, 0.9f, 0.9f);
        if (g.world.targetId == m.monsterId) {
            DrawCircle(g, x, y, radius * 1.5f, 1.f, 0.85f, 0.25f, 0.55f);   // target ring
            DrawCircle(g, x, y, radius * 0.9f, r, gr, b, 1.f);
        }
        // A monster that is attacking a player flashes.
        if (m.state == "attack") DrawCircle(g, x, y, radius * 1.8f, 1.f, 0.25f, 0.20f, 0.45f);
    }
    // Floating damage / EXP numbers (they rise and fade).
    for (const auto& t : g.world.texts) {
        const float life = t.lifeSec > 0.f ? 1.f - t.ageSec / t.lifeSec : 0.f;
        if (life <= 0.f) continue;
        const float x = float(t.x) * w;
        const float y = float(t.y) * h - (1.f - life) * 60.f * g.dp;
        const float alpha = std::min(1.f, life * 1.6f);
        float r = 1.f, gr = 0.95f, b = 0.4f;
        float font = t.crit ? 22.f : 16.f;
        int value = t.value;
        if (t.miss) { r = 0.85f; gr = 0.85f; b = 0.95f; value = 0; }
        else if (t.kind == 1) { r = 1.f; gr = 0.35f; b = 0.30f; }
        else if (t.kind == 2) { r = 0.45f; gr = 1.f; b = 0.55f; font = 13.f; }
        else if (t.kind == 3) { r = 1.f; gr = 0.85f; b = 0.30f; font = 13.f; }
        if (t.miss) DrawLabel(g, kLblMiss, x, y, font, 200.f, r, gr, b, alpha);
        else DrawNumber(g, value, x, y, font, r, gr, b, alpha);
    }
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

// Draws a full texture over a screen rectangle with `alpha`. Uses the label shader program (textured
// quads in pixels, alpha from the texture): the placeholder images are RGBA, so their alpha is
// multiplied by `alpha` through the vertex colour's alpha channel.
void DrawTexturedQuad(const Game& g, float x, float y, float w, float h, GLuint tex, float alpha) {
    // Position (pixels, origin top-left) and UV in one interleaved array, like DrawLabel.
    const float verts[24] = {
        x,     y,     0.f, 0.f,
        x + w, y,     1.f, 0.f,
        x,     y + h, 0.f, 1.f,
        x + w, y,     1.f, 0.f,
        x + w, y + h, 1.f, 1.f,
        x,     y + h, 0.f, 1.f,
    };
    glUseProgram(g.spriteProgram);
    glUniform2f(g.sScreen, float(g.width), float(g.height));
    glUniform4f(g.sColor, 1.f, 1.f, 1.f, alpha);
    glUniform1i(g.sSampler, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(1);
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

    // Verified placeholder textures (mobile/assets/) tinted over the floor and the blocks. When they
    // are missing or failed verification the procedural drawing above is the whole map.
    if (kLoadPlaceholderTextures && (g.groundTexReady || g.blockTexReady)) {
        if (g.groundTexReady) DrawTexturedQuad(g, 0.f, 0.f, w, h, g.groundTex, 0.55f);
        if (g.blockTexReady)
            for (const mobile::net::WorldRect& b : g.blocks)
                DrawTexturedQuad(g, float(b.x) * w, float(b.y) * h, float(b.w) * w, float(b.h) * h,
                                 g.blockTex, 0.45f);
    }
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

    // Real state from the server: players, monsters, stats, floating numbers.
    g.world = mobile::net::View().Get();
    mobile::net::View().SetPlayerPosition(double(g.playerX), double(g.playerY));
    mobile::net::View().AgeTexts(dt);
    if (g.world.stats.valid) {
        // The server's numbers drive the HUD bars and the auto-potion threshold.
        const mobile::net::PlayerStats& st = g.world.stats;
        g.hud.SetStats(float(st.hp), float(st.maxHp), float(st.mp), float(st.maxMp),
                       float(st.exp), float(st.expRequired), st.level);
        g.hp = float(st.hp);
        g.hpMax = float(st.maxHp);
        g.hpTimer = 0.f;
    } else {
        // Before the first world sync (or offline): the placeholder hit timer keeps the HUD alive.
        g.hpTimer += dt;
        if (g.hpTimer >= 6.f) {
            g.hpTimer = 0.f;
            g.hp -= 0.15f * g.hpMax;
            if (g.hp <= 0.f) {
                g.hp = g.hpMax;
                LOGI("respawn");
            }
        }
        g.hud.SetStats(g.hp, g.hpMax, 0.f, 1.f, 0.f, 1.f, 1);
    }

    // World session: while connected, the server's map and start position; the server's
    // corrections are applied here (a rejected move snaps the player back).
    SyncWorld(g);
    if (!g.playerPlaced) {
        g.playerX = 0.5f;
        g.playerY = 0.5f;
        g.playerPlaced = true;
    }

    // Player movement (map fractions, the same speeds and collision as the server; see world_map.h).
    mobile::net::StepInput step;
    step.joyX = g.moveX;
    step.joyY = g.moveY;
    step.running = g.hud.Running();
    step.dtSec = dt;
    step.radiusPx = 18.0 * g.dp;
    step.marginPx = 40.0 * g.dp;
    const float prevX = g.playerX, prevY = g.playerY;
    const mobile::net::WorldPoint next = mobile::net::StepPlayer(
        mobile::net::WorldPoint{g.playerX, g.playerY}, step, g.blocks, mobile::net::MapSize{w, h});
    g.playerX = float(next.x);
    g.playerY = float(next.y);
    g.movedPx += std::hypot((g.playerX - prevX) * w, (g.playerY - prevY) * h);
    mobile::net::World().Publish(mobile::net::WorldPoint{g.playerX, g.playerY});

    if (inWorld) DrawMap(g, w, h);
    if (inWorld) DrawWorldActors(g, w, h);
    const float radius = 18.f * g.dp;  // px, the same radius the movement uses
    if (inWorld) DrawCircle(g, g.playerX * w, g.playerY * h, radius, 0.30f, 0.80f, 0.45f, 1.f);
    if (inWorld && g.attackFlash > 0.f) {
        g.attackFlash -= dt;
        DrawCircle(g, g.playerX * w, g.playerY * h, 42.f * g.dp, 1.f, 0.80f, 0.25f, g.attackFlash * 2.f);
    }
    for (float& f : g.slotFlash) {
        if (f > 0.f) f -= dt;
    }

    // Target lock: the HUD draws its HP bar and the attack button lights up.
    if (g.world.targetId != 0) {
        for (const auto& m : g.world.monsters) {
            if (m.monsterId == g.world.targetId) {
                g.hud.SetTarget(m.monsterId, float(m.hp), float(m.maxHp));
                break;
            }
        }
    } else {
        g.hud.SetTarget(0, 0.f, 0.f);
    }
    if (inWorld) {
        // Skill cooldowns: the server answers carry the real cooldown; the HUD greys the slot.
        for (int i = 0; i < mobile::TouchHUD::kSlots; ++i)
            g.hud.SetSkillCooldown(i, float(BridgeSkillCooldownMs(g.hud.SlotSkill(i))) / 1000.f);
        DrawHud(g);
    }

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
    // Round 4: save the JavaVM + MainActivity so the HUD hooks (this thread) can post jobs to Java,
    // which runs them on its net thread. Done before the first touch event can open a panel.
    BridgeAttachApp(app);

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
