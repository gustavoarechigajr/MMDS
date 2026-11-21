// Second-screen panels for dual-screen Android handhelds (AYN Thor and similar).
//
// The Java side (DualScreenManager / DualScreenPresentation) puts a SurfaceView on the secondary
// display and hands its Surface to us. Each game frame, Play_PostWorldDraw renders the panels into an
// off-screen framebuffer as part of the normal display list (DualScreen_Draw). After the frame has
// been executed, DualScreen_PresentFrame copies that framebuffer onto the secondary Surface with a
// context that shares textures with the game's. The copy never waits for vsync, so the main screen
// is never held back by the second one.
#include "DualScreen.h"

#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
void Message_ResetOcarinaButtonAlphas(void);
}

#include <atomic>
#include <chrono>
#include <deque>
#include <cmath>
#include <mutex>

#define CVAR_DUAL_SCREEN "gEnhancements.DualScreen.Enabled"

namespace {

// Touch events from the UI thread, drained by the game thread each update
std::mutex sTouchMutex;
std::deque<DualScreenTouchEvent> sTouchQueue;
constexpr size_t kTouchQueueMax = 64;
std::atomic<int32_t> sSurfaceWidth{ 1240 };
std::atomic<int32_t> sSurfaceHeight{ 1080 };

int32_t sFrameBufferId = -1;
int32_t sFrameBufferWidth = 0;
int32_t sFrameBufferHeight = 0;

} // namespace

extern "C" int32_t DualScreen_GetCanvasHeight(void) {
    int32_t w = sSurfaceWidth.load();
    int32_t h = sSurfaceHeight.load();
    if (w <= 0 || h <= 0) {
        return 480;
    }
    return (int32_t)lroundf(DUAL_SCREEN_CANVAS_WIDTH * (float)h / (float)w);
}

extern "C" int32_t DualScreen_GetFrameBufferId(void) {
    int32_t w = sSurfaceWidth.load();
    int32_t h = sSurfaceHeight.load();
    if (sFrameBufferId == -1 || w != sFrameBufferWidth || h != sFrameBufferHeight) {
        auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
        auto interpreter = wnd != nullptr ? wnd->GetInterpreterWeak().lock() : nullptr;
        if (interpreter == nullptr || interpreter->mCurDimensions.width == 0 || interpreter->mCurDimensions.height == 0) {
            return -1;
        }
        // Coordinates are in canvas units (640 wide), but libultraship scales a resizable framebuffer by
        // current/native dimensions, so pick native dimensions that make it render at the panel's real size
        int32_t canvasH = DualScreen_GetCanvasHeight();
        uint32_t nativeW = (uint32_t)lroundf(DUAL_SCREEN_CANVAS_WIDTH * (float)interpreter->mCurDimensions.width / w);
        uint32_t nativeH = (uint32_t)lroundf(canvasH * (float)interpreter->mCurDimensions.height / h);
        sFrameBufferId = gfx_create_framebuffer(DUAL_SCREEN_CANVAS_WIDTH, canvasH, nativeW, nativeH, true, true);
        sFrameBufferWidth = w;
        sFrameBufferHeight = h;
    }
    return sFrameBufferId;
}

// #region Songs played from the touch screen

namespace {
constexpr uint8_t kNoSong = 0xFF;
// Long enough for Link to pull out the ocarina; cleared early once the staff takes the song
constexpr int32_t kSongRequestFrames = 20;
uint8_t sRequestedSong = kNoSong;
int32_t sSongRequestFramesLeft = 0;
} // namespace

extern "C" void DualScreen_RequestSong(uint8_t song) {
    sRequestedSong = song;
    sSongRequestFramesLeft = kSongRequestFrames;
}

static void RegisterDualScreenSongs() {
    COND_VB_SHOULD(VB_GET_ITEM_ON_BUTTON, true, {
        int32_t slot = va_arg(args, int);
        ItemId* item = va_arg(args, ItemId*);

        if (sSongRequestFramesLeft > 0 && slot == EQUIP_SLOT_C_LEFT) {
            *item = ITEM_OCARINA_OF_TIME;
        }
    });

    COND_VB_SHOULD(VB_OVERRIDE_OCARINA_STAFF_STATE, true, {
        if (sRequestedSong != kNoSong && sSongRequestFramesLeft > 0) {
            OcarinaStaff* staff = va_arg(args, OcarinaStaff*);
            staff->state = sRequestedSong;
            sRequestedSong = kNoSong;
            sSongRequestFramesLeft = 0;
            Message_ResetOcarinaButtonAlphas();
            *should = true;
        }
    });

    COND_HOOK(OnGameStateUpdate, true, []() {
        if (sSongRequestFramesLeft > 0 && --sSongRequestFramesLeft == 0) {
            sRequestedSong = kNoSong;
        }
    });
}

static RegisterShipInitFunc sDualScreenSongsInit(RegisterDualScreenSongs, {});

#if defined(__ANDROID__)
#include <android/log.h>
extern "C" PlayState* gPlayState;

// Gyro diagnostics (gDebug.GyroLog): where Link is actually aiming, to compare with the turn the gyro measured
static void RegisterGyroAimLog() {
    COND_HOOK(OnGameStateUpdate, CVarGetInteger("gDebug.GyroLog", 0), []() {
        static int32_t sFrames = 0;
        if (gPlayState == nullptr || (++sFrames % 10) != 0) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        __android_log_print(ANDROID_LOG_INFO, "DSGyro", "aim focusYaw %+.1f bodyYaw %+.1f pitch %+.1f deg",
                            player->actor.focus.rot.y * (360.0f / 65536.0f), player->actor.shape.rot.y * (360.0f / 65536.0f),
                            player->actor.focus.rot.x * (360.0f / 65536.0f));
    });
}

static RegisterShipInitFunc sGyroAimLogInit(RegisterGyroAimLog, { "gDebug.GyroLog" });
#endif

// #endregion

// #region Frame rate and target health

namespace {
std::atomic<int32_t> sFps{ 0 };
int32_t sFramesThisSecond = 0;
std::chrono::steady_clock::time_point sSecondStart = std::chrono::steady_clock::now();
} // namespace

extern "C" void DualScreen_CountFrame(void) {
    auto now = std::chrono::steady_clock::now();
    sFramesThisSecond++;
    if (now - sSecondStart >= std::chrono::seconds(1)) {
        sFps = sFramesThisSecond;
        sFramesThisSecond = 0;
        sSecondStart = now;
    }
}

extern "C" int32_t DualScreen_GetFPS(void) {
    return sFps;
}

u8 GetActorMaxHealth(Actor* actor); // EnemyHealthBars.cpp
extern "C" bool FrameInterpolation_SetRecording(bool on); // FrameInterpolation.cpp

extern "C" uint8_t DualScreen_SetInterpolationRecording(uint8_t on) {
    return FrameInterpolation_SetRecording(on);
}

extern "C" uint8_t DualScreen_ActorMaxHealth(Actor* actor) {
    return GetActorMaxHealth(actor);
}

// #endregion

extern "C" uint8_t DualScreen_PollTouch(DualScreenTouchEvent* event) {
    std::lock_guard<std::mutex> lock(sTouchMutex);
    if (sTouchQueue.empty()) {
        return 0;
    }
    *event = sTouchQueue.front();
    sTouchQueue.pop_front();
    return 1;
}

static void PushTouch(int32_t type, float x, float y, float x2, float y2, int32_t pointers) {
    std::lock_guard<std::mutex> lock(sTouchMutex);
    if (type == DUAL_SCREEN_TOUCH_MOVE && !sTouchQueue.empty() && sTouchQueue.back().type == DUAL_SCREEN_TOUCH_MOVE) {
        // Only the latest position of a drag matters
        sTouchQueue.back() = { type, x, y, x2, y2, pointers };
        return;
    }
    if (sTouchQueue.size() >= kTouchQueueMax) {
        sTouchQueue.pop_front();
    }
    sTouchQueue.push_back({ type, x, y, x2, y2, pointers });
}

#if defined(__ANDROID__)

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <condition_variable>
#include <thread>
#include <GLES2/gl2.h>
#include <android/log.h>

#define DS_LOG(...) __android_log_print(ANDROID_LOG_INFO, "DualScreen", __VA_ARGS__)

namespace {

// The copy to the second screen runs on its own thread with its own context (shared with the game's), so the
// game thread only drops a fence and moves on: no context switches or waits on the second screen's vsync.
struct PresentState {
    // Guarded by mutex: the surface handed over by the UI thread
    std::mutex mutex;
    ANativeWindow* pendingWindow = nullptr;
    bool pendingDestroy = false;
    ANativeWindow* window = nullptr;

    // Owned by the game thread
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;

    // Hand-off to the present thread, guarded by frameMutex
    std::thread thread;
    std::mutex frameMutex;
    std::condition_variable frameCv;
    bool running = false;
    bool hasFrame = false;
    GLuint frameTexture = 0;
    EGLSyncKHR frameSync = EGL_NO_SYNC_KHR;
};

PresentState sPresent;

const char* kVertexShader = "attribute vec2 aPosition;\n"
                            "attribute vec2 aTexCoord;\n"
                            "varying vec2 vTexCoord;\n"
                            "void main() {\n"
                            "    vTexCoord = aTexCoord;\n"
                            "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
                            "}\n";

const char* kFragmentShader = "precision mediump float;\n"
                              "varying vec2 vTexCoord;\n"
                              "uniform sampler2D uTexture;\n"
                              "void main() {\n"
                              "    gl_FragColor = vec4(texture2D(uTexture, vTexCoord).rgb, 1.0);\n"
                              "}\n";

GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    return shader;
}

void PresentThreadMain() {
    EGLDisplay display = sPresent.display;
    EGLSurface surface = sPresent.surface;

    if (!eglMakeCurrent(display, surface, surface, sPresent.context)) {
        DS_LOG("present thread: make current failed: egl 0x%x", eglGetError());
        return;
    }
    // Off the game thread, so waiting for the second screen's vsync costs nothing and avoids tearing
    eglSwapInterval(display, 1);

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Fast3D framebuffer textures are stored top-down
    static const float kQuad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, //
        1.0f,  -1.0f, 1.0f, 1.0f, //
        -1.0f, 1.0f,  0.0f, 0.0f, //
        1.0f,  1.0f,  1.0f, 0.0f, //
    };
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);

    while (true) {
        GLuint texture;
        EGLSyncKHR sync;
        {
            std::unique_lock<std::mutex> lock(sPresent.frameMutex);
            sPresent.frameCv.wait(lock, [] { return !sPresent.running || sPresent.hasFrame; });
            if (!sPresent.running) {
                break;
            }
            texture = sPresent.frameTexture;
            sync = sPresent.frameSync;
            sPresent.frameSync = EGL_NO_SYNC_KHR;
            sPresent.hasFrame = false;
        }
        // Wait until the game's GPU work has finished drawing the panels
        if (sync != EGL_NO_SYNC_KHR) {
            eglClientWaitSyncKHR(display, sync, 0, 100000000ULL);
            eglDestroySyncKHR(display, sync);
        }

        EGLint width = 0;
        EGLint height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        glViewport(0, 0, width, height);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(display, surface);
    }

    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

// Serialises starting and stopping the present thread (the UI and game threads can both stop it)
std::mutex sThreadMutex;

// Safe from any thread; the UI thread calls it before the surface goes away
void StopPresentThread() {
    std::lock_guard<std::mutex> threadLock(sThreadMutex);
    {
        std::lock_guard<std::mutex> lock(sPresent.frameMutex);
        if (!sPresent.running) {
            return;
        }
        sPresent.running = false;
    }
    sPresent.frameCv.notify_one();
    if (sPresent.thread.joinable()) {
        sPresent.thread.join();
    }
}

void DestroySurfaceLocked() {
    StopPresentThread();
    if (sPresent.frameSync != EGL_NO_SYNC_KHR) {
        eglDestroySyncKHR(sPresent.display, sPresent.frameSync);
        sPresent.frameSync = EGL_NO_SYNC_KHR;
    }
    if (sPresent.surface != EGL_NO_SURFACE) {
        eglDestroySurface(sPresent.display, sPresent.surface);
    }
    if (sPresent.context != EGL_NO_CONTEXT) {
        eglDestroyContext(sPresent.display, sPresent.context);
    }
    if (sPresent.window != nullptr) {
        ANativeWindow_release(sPresent.window);
    }
    sPresent.display = EGL_NO_DISPLAY;
    sPresent.context = EGL_NO_CONTEXT;
    sPresent.surface = EGL_NO_SURFACE;
    sPresent.window = nullptr;
    sPresent.hasFrame = false;
}

bool CreateSurfaceLocked(ANativeWindow* window) {
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext mainContext = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || mainContext == EGL_NO_CONTEXT) {
        return false;
    }

    const EGLint configAttribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                     EGL_RED_SIZE,        8,                   EGL_GREEN_SIZE,   8,
                                     EGL_BLUE_SIZE,       8,                   EGL_NONE };
    EGLConfig config;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        return false;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, mainContext, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        return false;
    }
    EGLSurface surface = eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        eglDestroyContext(display, context);
        return false;
    }

    sPresent.display = display;
    sPresent.context = context;
    sPresent.surface = surface;
    sPresent.window = window;
    std::lock_guard<std::mutex> threadLock(sThreadMutex);
    sPresent.running = true;
    sPresent.thread = std::thread(PresentThreadMain);
    return true;
}

} // namespace

extern "C" uint8_t DualScreen_IsActive(void) {
    if (CVarGetInteger(CVAR_DUAL_SCREEN, 1) == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(sPresent.mutex);
    return sPresent.window != nullptr || sPresent.pendingWindow != nullptr;
}

extern "C" void DualScreen_PresentFrame(void) {
    std::lock_guard<std::mutex> lock(sPresent.mutex);

    if (sPresent.pendingDestroy) {
        DestroySurfaceLocked();
        sPresent.pendingDestroy = false;
    }
    if (sPresent.pendingWindow != nullptr) {
        ANativeWindow* window = sPresent.pendingWindow;
        sPresent.pendingWindow = nullptr;
        DestroySurfaceLocked();
        if (!CreateSurfaceLocked(window)) {
            DS_LOG("create surface failed: egl 0x%x", eglGetError());
            ANativeWindow_release(window);
        }
    }
    if (sPresent.surface == EGL_NO_SURFACE || sFrameBufferId == -1 || CVarGetInteger(CVAR_DUAL_SCREEN, 1) == 0 ||
        CVarGetInteger("gDebug.DualScreenSkipPresent", 0)) { // audio tests: measure the hand-off's cost
        return;
    }

    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (wnd == nullptr) {
        return;
    }
    auto interpreter = wnd->GetInterpreterWeak().lock();
    if (interpreter == nullptr || interpreter->GetCurrentRenderingAPI() == nullptr) {
        return;
    }
    GLuint texture = (GLuint)(uintptr_t)interpreter->GetCurrentRenderingAPI()->GetFramebufferTextureId(sFrameBufferId);

    // Fence after the panel draw, so the present thread samples a finished framebuffer
    EGLSyncKHR sync = eglCreateSyncKHR(sPresent.display, EGL_SYNC_FENCE_KHR, nullptr);
    glFlush();
    {
        std::lock_guard<std::mutex> frameLock(sPresent.frameMutex);
        if (sPresent.frameSync != EGL_NO_SYNC_KHR) {
            eglDestroySyncKHR(sPresent.display, sPresent.frameSync);
        }
        sPresent.frameTexture = texture;
        sPresent.frameSync = sync;
        sPresent.hasFrame = true;
    }
    sPresent.frameCv.notify_one();
}

extern "C" JNIEXPORT void JNICALL Java_com_twoshipfork_mm_MainActivity_nativeDualScreenSurfaceChanged(
    JNIEnv* env, jclass clazz, jobject surface, jint width, jint height) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return;
    }
    sSurfaceWidth = width;
    sSurfaceHeight = height;
    std::lock_guard<std::mutex> lock(sPresent.mutex);
    if (sPresent.pendingWindow != nullptr) {
        ANativeWindow_release(sPresent.pendingWindow);
    }
    sPresent.pendingWindow = window;
    sPresent.pendingDestroy = false;
}

extern "C" JNIEXPORT void JNICALL Java_com_twoshipfork_mm_MainActivity_nativeDualScreenSurfaceDestroyed(JNIEnv* env,
                                                                                                       jclass clazz) {
    // The surface may not be drawn to once this returns
    StopPresentThread();
    std::lock_guard<std::mutex> lock(sPresent.mutex);
    if (sPresent.pendingWindow != nullptr) {
        ANativeWindow_release(sPresent.pendingWindow);
        sPresent.pendingWindow = nullptr;
    }
    sPresent.pendingDestroy = true;
}

extern "C" JNIEXPORT void JNICALL Java_com_twoshipfork_mm_MainActivity_nativeDualScreenTouch(
    JNIEnv* env, jclass clazz, jint action, jfloat x, jfloat y, jfloat x2, jfloat y2, jint pointers) {
    // MotionEvent actions: DOWN 0, UP 1, MOVE 2, CANCEL 3, POINTER_DOWN 5, POINTER_UP 6
    float h = (float)DualScreen_GetCanvasHeight();
    float w = (float)DUAL_SCREEN_CANVAS_WIDTH;
    switch (action) {
        case 0:
            PushTouch(DUAL_SCREEN_TOUCH_DOWN, x * w, y * h, x2 * w, y2 * h, pointers);
            break;
        case 2:
        case 5:
        case 6:
            PushTouch(DUAL_SCREEN_TOUCH_MOVE, x * w, y * h, x2 * w, y2 * h, pointers);
            break;
        case 1:
        case 3:
            PushTouch(DUAL_SCREEN_TOUCH_UP, x * w, y * h, x2 * w, y2 * h, pointers);
            break;
    }
}

#else // !defined(__ANDROID__)

extern "C" uint8_t DualScreen_IsActive(void) {
    return false;
}

extern "C" void DualScreen_PresentFrame(void) {
}

#endif
