#ifdef __ANDROID__

#include <jni.h>
#include <errno.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/sensor.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "renderer.h"
#include "vehicle_physics.h"
#include "police_ai.h"
#include "world_streamer.h"

#define LOG_TAG "NFSMW_Native"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))

struct EngineState {
    struct android_app* app;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;
    bool hasFocus;

    // Subsistemas do Jogo
    VehicleSimulation car;
    PursuitSystem pursuit;
    WorldStreamer world;
    AndroidRenderer renderer;

    // Controles de Toque
    f32 touchThrottle;
    f32 touchSteer;
    bool touchNos;
};

// Inicializa a superfície EGL e o contexto OpenGL ES 3.0
static int InitDisplay(struct EngineState* engine) {
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);

    EGLint format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, format);

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);
    EGLSurface surface = eglCreateWindowSurface(display, config, engine->app->window, nullptr);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGW("Impossivel conectar contexto EGL!");
        return -1;
    }

    EGLint w, h;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    engine->display = display;
    engine->context = context;
    engine->surface = surface;
    engine->width = w;
    engine->height = h;

    LOGI("Superficie EGL inicializada com sucesso: %dx%d", w, h);

    // Inicializa o renderizador
    engine->renderer.Init(w, h);
    return 0;
}

// Renderiza um frame do jogo na tela do celular
static void DrawFrame(struct EngineState* engine) {
    if (engine->display == nullptr) return;

    // Limpa a tela
    glClearColor(0.25f, 0.28f, 0.35f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Atualiza simulação de física e IA
    f32 dt = 0.016f; // ~60 FPS
    engine->car.Update(dt, engine->touchThrottle, 0.0f, engine->touchSteer, false, engine->touchNos);
    engine->world.Update(engine->car.GetPosition());
    engine->pursuit.Update(dt, engine->car.GetPosition(), engine->car.GetVelocity());

    // Apresenta o buffer na tela do celular
    eglSwapBuffers(engine->display, engine->surface);
}

// Processamento de toque na tela (Controles virtuais)
static int32_t HandleInput(struct android_app* app, AInputEvent* event) {
    auto* engine = (struct EngineState*)app->userData;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);

        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_MOVE) {
            // Lado direito da tela = Acelerador e Nitro
            if (x > engine->width * 0.5f) {
                engine->touchThrottle = 1.0f;
                engine->touchNos = (y < engine->height * 0.4f); // Topo direito = Nitro
            } else {
                // Lado esquerdo da tela = Direção (Esquerda / Direita)
                float normX = (x / (engine->width * 0.5f)) * 2.0f - 1.0f; // -1.0 a +1.0
                engine->touchSteer = normX;
            }
        } else if (action == AMOTION_EVENT_ACTION_UP) {
            engine->touchThrottle = 0.0f;
            engine->touchSteer = 0.0f;
            engine->touchNos = false;
        }
        return 1;
    }
    return 0;
}

// Comandos de Ciclo de Vida do Android (Pausar, Retomar, Fechar)
static void HandleCmd(struct android_app* app, int32_t cmd) {
    auto* engine = (struct EngineState*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (engine->app->window != nullptr) {
                InitDisplay(engine);
                DrawFrame(engine);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            if (engine->display != EGL_NO_DISPLAY) {
                eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(engine->display, engine->context);
                eglDestroySurface(engine->display, engine->surface);
                eglTerminate(engine->display);
            }
            engine->display = EGL_NO_DISPLAY;
            break;
        case APP_CMD_GAINED_FOCUS:
            engine->hasFocus = true;
            break;
        case APP_CMD_LOST_FOCUS:
            engine->hasFocus = false;
            break;
    }
}

// Ponto de entrada nativo do Android
void android_main(struct android_app* state) {
    struct EngineState engine{};
    memset(&engine, 0, sizeof(engine));
    state->userData = &engine;
    state->onAppCmd = HandleCmd;
    state->onInputEvent = HandleInput;
    engine.app = state;

    LOGI("NFS: Most Wanted Native Port iniciado no Android!");

    // Loop de eventos e renderização contínua
    while (true) {
        int ident;
        int events;
        struct android_poll_source* source;

        while ((ident = ALooper_pollOnce(engine.hasFocus ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) {
                source->process(state, source);
            }
            if (state->destroyRequested != 0) {
                return;
            }
        }

        if (engine.hasFocus) {
            DrawFrame(&engine);
        }
    }
}

#endif
