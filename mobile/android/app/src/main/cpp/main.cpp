// AI EP7 - Android entry point (original code).
// NativeActivity + OpenGL ES 3.0. Draws the touch HUD (mobile/touch_hud) and a
// placeholder player that moves with the joystick. Gameplay and data come later.
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <chrono>
#include <cmath>

#include "touch_hud.h"

#define LOG_TAG "AiEp7"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr float kPi = 3.14159265f;

struct Game {
    android_app* app = nullptr;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    float dp = 1.f;  // pixels per density-independent pixel

    GLuint program = 0;
    GLuint vbo = 0;
    GLint uScreen = -1;
    GLint uColor = -1;

    mobile::TouchHUD hud;
    float moveX = 0.f;  // joystick output, [-1, 1]; dy < 0 = up on screen
    float moveY = 0.f;
    float playerX = 0.f;
    float playerY = 0.f;
    bool playerPlaced = false;
    float attackFlash = 0.f;  // seconds left on the attack flash
    bool joyWasActive = false;
    float movedPx = 0.f;      // distance the player travelled during the current drag
    long frames = 0;
};

Game* g_game = nullptr;  // the HUD hooks are plain function pointers, so they reach the game here

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

bool CreateGlResources(Game& g) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
    if (!vs || !fs) return false;

    g.program = glCreateProgram();
    glAttachShader(g.program, vs);
    glAttachShader(g.program, fs);
    glBindAttribLocation(g.program, 0, "aPos");
    glLinkProgram(g.program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(g.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(g.program, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
        return false;
    }

    g.uScreen = glGetUniformLocation(g.program, "uScreen");
    g.uColor = glGetUniformLocation(g.program, "uColor");

    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    return true;
}

// ---------- HUD layout (runs at start and after every resize) ----------

void LayoutHud(Game& g) {
    mobile::Hooks hooks;
    hooks.MoveDir = [](float dx, float dy) {
        if (g_game) { g_game->moveX = dx; g_game->moveY = dy; }
    };
    hooks.BasicAttack = []() {
        if (g_game) g_game->attackFlash = 0.25f;
        LOGI("basic attack");
    };
    hooks.UseSkill = [](int skillId) { LOGI("use skill %d", skillId); };
    hooks.OpenInventory = []() { LOGI("open inventory"); };
    g.hud.Init(static_cast<float>(g.width), static_cast<float>(g.height), g.dp, hooks);
    for (int i = 0; i < mobile::TouchHUD::kSlots; ++i) g.hud.SetSlot(i, i + 1);
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
    LayoutHud(g);

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
        g.vbo = 0;
        g.program = 0;
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

void DrawCircle(const Game& g, float cx, float cy, float r, float red, float green, float blue, float alpha) {
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

void DrawFrame(Game& g, float dt) {
    const float w = static_cast<float>(g.width);
    const float h = static_cast<float>(g.height);

    glViewport(0, 0, g.width, g.height);
    glClearColor(0.05f, 0.07f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g.program);
    glUniform2f(g.uScreen, w, h);

    // Placeholder player: moves with the joystick, clamped to the screen.
    if (!g.playerPlaced) {
        g.playerX = w * 0.5f;
        g.playerY = h * 0.5f;
        g.playerPlaced = true;
    }
    const float speed = 160.f * g.dp;
    const float prevX = g.playerX, prevY = g.playerY;
    g.playerX += g.moveX * speed * dt;
    g.playerY += g.moveY * speed * dt;
    const float margin = 40.f * g.dp;
    if (g.playerX < margin) g.playerX = margin;
    if (g.playerX > w - margin) g.playerX = w - margin;
    if (g.playerY < margin) g.playerY = margin;
    if (g.playerY > h - margin) g.playerY = h - margin;
    g.movedPx += std::hypot(g.playerX - prevX, g.playerY - prevY);

    DrawCircle(g, g.playerX, g.playerY, 18.f * g.dp, 0.30f, 0.80f, 0.45f, 1.f);
    if (g.attackFlash > 0.f) {
        g.attackFlash -= dt;
        DrawCircle(g, g.playerX, g.playerY, 42.f * g.dp, 1.f, 0.80f, 0.25f, g.attackFlash * 2.f);
    }

    // Touch HUD.
    const mobile::Vec2 atk = g.hud.AttackCenter();
    DrawCircle(g, atk.x, atk.y, g.hud.AttackRadius(), 0.20f, 0.60f, 1.00f, 0.85f);

    for (int i = 0; i < mobile::TouchHUD::kSlots; ++i) {
        const mobile::Vec2 p = g.hud.SlotCenter(i);
        DrawCircle(g, p.x, p.y, g.hud.SlotRadius(), 0.15f, 0.15f, 0.20f, 0.90f);
        if (g.hud.SlotSkill(i) > 0) {
            DrawCircle(g, p.x, p.y, g.hud.SlotRadius() * 0.6f, 0.90f, 0.60f, 0.20f, 1.f);
        }
    }

    const mobile::Vec2 bag = g.hud.BagCenter();
    DrawCircle(g, bag.x, bag.y, g.hud.BagRadius(), 0.40f, 0.40f, 0.40f, 0.90f);

    // Joystick: the base is always visible (bottom-left); the knob follows the thumb.
    const mobile::Vec2 jb = g.hud.JoystickOrigin();
    const mobile::Vec2 jk = g.hud.JoystickKnob();
    const bool active = g.hud.JoystickActive();
    DrawCircle(g, jb.x, jb.y, g.hud.JoystickRadius(), 1.f, 1.f, 1.f, active ? 0.22f : 0.12f);
    DrawCircle(g, jk.x, jk.y, 26.f * g.dp, 1.f, 1.f, 1.f, active ? 0.70f : 0.35f);
    if (active != g.joyWasActive) {
        g.joyWasActive = active;
        if (active) {
            g.movedPx = 0.f;
            LOGI("joystick start");
        } else {
            LOGI("joystick end, player moved %.0f px", g.movedPx);
        }
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
            g->hud.OnTouch(mobile::Touch::Down, AMotionEvent_getPointerId(event, idx), pointAt(idx));
            return 1;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            const size_t idx = size_t((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            g->hud.OnTouch(mobile::Touch::Up, AMotionEvent_getPointerId(event, idx), pointAt(idx));
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
                LayoutHud(*g);
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
