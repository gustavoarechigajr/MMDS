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

#include <atomic>
#include <cmath>
#include <mutex>

#define CVAR_DUAL_SCREEN "gEnhancements.DualScreen.Enabled"

namespace {

std::atomic<uint8_t> sPendingTap{ 0 };
std::atomic<int32_t> sPendingSwipe{ DUAL_SCREEN_SWIPE_NONE };
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

extern "C" void DualScreen_ConsumeInput(uint8_t* tapped, int32_t* swipe) {
    *tapped = sPendingTap.exchange(0);
    *swipe = sPendingSwipe.exchange(DUAL_SCREEN_SWIPE_NONE);
}

#if defined(__ANDROID__)

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>

#define DS_LOG(...) __android_log_print(ANDROID_LOG_INFO, "DualScreen", __VA_ARGS__)

namespace {

struct PresentState {
    std::mutex mutex;
    ANativeWindow* pendingWindow = nullptr;
    bool pendingDestroy = false;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    ANativeWindow* window = nullptr;
    GLuint program = 0;
    GLuint vbo = 0;
    GLint textureUniform = -1;
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

void EnsureGlResources() {
    if (sPresent.program != 0) {
        return;
    }
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    sPresent.program = glCreateProgram();
    glAttachShader(sPresent.program, vs);
    glAttachShader(sPresent.program, fs);
    glBindAttribLocation(sPresent.program, 0, "aPosition");
    glBindAttribLocation(sPresent.program, 1, "aTexCoord");
    glLinkProgram(sPresent.program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    sPresent.textureUniform = glGetUniformLocation(sPresent.program, "uTexture");

    // Fast3D framebuffer textures are stored top-down
    static const float kQuad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, //
        1.0f,  -1.0f, 1.0f, 1.0f, //
        -1.0f, 1.0f,  0.0f, 0.0f, //
        1.0f,  1.0f,  1.0f, 0.0f, //
    };
    glGenBuffers(1, &sPresent.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sPresent.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
}

void DestroySurfaceLocked() {
    if (sPresent.display != EGL_NO_DISPLAY) {
        EGLDisplay prevDisplay = eglGetCurrentDisplay();
        EGLContext prevContext = eglGetCurrentContext();
        EGLSurface prevDraw = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface prevRead = eglGetCurrentSurface(EGL_READ);
        if (sPresent.context != EGL_NO_CONTEXT && sPresent.surface != EGL_NO_SURFACE &&
            eglMakeCurrent(sPresent.display, sPresent.surface, sPresent.surface, sPresent.context)) {
            if (sPresent.program != 0) {
                glDeleteProgram(sPresent.program);
                glDeleteBuffers(1, &sPresent.vbo);
            }
            eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
        }
        if (sPresent.surface != EGL_NO_SURFACE) {
            eglDestroySurface(sPresent.display, sPresent.surface);
        }
        if (sPresent.context != EGL_NO_CONTEXT) {
            eglDestroyContext(sPresent.display, sPresent.context);
        }
    }
    if (sPresent.window != nullptr) {
        ANativeWindow_release(sPresent.window);
    }
    sPresent.display = EGL_NO_DISPLAY;
    sPresent.context = EGL_NO_CONTEXT;
    sPresent.surface = EGL_NO_SURFACE;
    sPresent.window = nullptr;
    sPresent.program = 0;
    sPresent.vbo = 0;
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

    // Never block the game thread on the second screen's vsync
    EGLSurface prevDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface prevRead = eglGetCurrentSurface(EGL_READ);
    if (eglMakeCurrent(display, surface, surface, context)) {
        eglSwapInterval(display, 0);
        eglMakeCurrent(display, prevDraw, prevRead, mainContext);
    }
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
        } else {
            DS_LOG("surface created");
        }
    }
    if (sPresent.surface == EGL_NO_SURFACE || sFrameBufferId == -1 || CVarGetInteger(CVAR_DUAL_SCREEN, 1) == 0) {
        static int sSkipCount = 0;
        if (sSkipCount++ % 600 == 0) {
            DS_LOG("skip: surface=%d fb=%d", sPresent.surface != EGL_NO_SURFACE, sFrameBufferId);
        }
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

    // Make the panel draw visible to the shared context before sampling it
    glFlush();

    EGLDisplay prevDisplay = eglGetCurrentDisplay();
    EGLSurface prevDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface prevRead = eglGetCurrentSurface(EGL_READ);
    EGLContext prevContext = eglGetCurrentContext();
    if (!eglMakeCurrent(sPresent.display, sPresent.surface, sPresent.surface, sPresent.context)) {
        return;
    }

    EnsureGlResources();

    EGLint width = 0;
    EGLint height = 0;
    eglQuerySurface(sPresent.display, sPresent.surface, EGL_WIDTH, &width);
    eglQuerySurface(sPresent.display, sPresent.surface, EGL_HEIGHT, &height);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glUseProgram(sPresent.program);
    glBindBuffer(GL_ARRAY_BUFFER, sPresent.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(sPresent.textureUniform, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    eglSwapBuffers(sPresent.display, sPresent.surface);
    static int sLogCount = 0;
    if (sLogCount++ % 600 == 0) {
        DS_LOG("present fb=%d tex=%u %dx%d gl=0x%x egl=0x%x", sFrameBufferId, texture, width, height, glGetError(),
               eglGetError());
    }
    eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
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
    std::lock_guard<std::mutex> lock(sPresent.mutex);
    if (sPresent.pendingWindow != nullptr) {
        ANativeWindow_release(sPresent.pendingWindow);
        sPresent.pendingWindow = nullptr;
    }
    sPresent.pendingDestroy = true;
}

extern "C" JNIEXPORT void JNICALL Java_com_twoshipfork_mm_MainActivity_nativeDualScreenTouch(JNIEnv* env,
                                                                                             jclass clazz,
                                                                                             jint action, jfloat x,
                                                                                             jfloat y) {
    // Gesture recognition: a short movement is a tap, a mostly vertical one is a swipe
    static float sDownX = 0.0f;
    static float sDownY = 0.0f;
    constexpr jint kActionDown = 0;
    constexpr jint kActionUp = 1;
    constexpr float kSwipeThreshold = 0.06f;

    if (action == kActionDown) {
        sDownX = x;
        sDownY = y;
    } else if (action == kActionUp) {
        float dx = x - sDownX;
        float dy = y - sDownY;
        if (fabsf(dy) >= kSwipeThreshold && fabsf(dy) > fabsf(dx)) {
            sPendingSwipe = (dy < 0.0f) ? DUAL_SCREEN_SWIPE_UP : DUAL_SCREEN_SWIPE_DOWN;
        } else if (fabsf(dx) < kSwipeThreshold && fabsf(dy) < kSwipeThreshold) {
            sPendingTap = 1;
        }
    }
}

#else // !defined(__ANDROID__)

extern "C" uint8_t DualScreen_IsActive(void) {
    return false;
}

extern "C" void DualScreen_PresentFrame(void) {
}

#endif
