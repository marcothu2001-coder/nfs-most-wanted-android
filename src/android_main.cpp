#ifdef __ANDROID__

#include <jni.h>
#include <errno.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/sensor.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "renderer.h"
#include "vehicle_physics.h"
#include "police_ai.h"
#include "world_streamer.h"

#define LOG_TAG "NFSMW_Native"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

struct ColoredVertex {
    float x, y, z;
    float r, g, b, a;
};

struct EngineState {
    struct android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int32_t width = 0;
    int32_t height = 0;
    bool hasFocus = false;

    // OpenGL Shader Program & Locations
    GLuint program = 0;
    GLint aPosLoc = -1;
    GLint aColorLoc = -1;
    GLint uMvpLoc = -1;

    // Subsistemas do Jogo (Inicializados via construtores C++ seguros)
    VehicleSimulation car;
    PursuitSystem pursuit;
    WorldStreamer world;
    AndroidRenderer renderer;

    // Controles de Toque (Multi-touch)
    f32 touchThrottle = 0.45f; // Aceleração de cruzeiro inicial para ação imediata
    f32 touchBrake = 0.0f;
    f32 touchSteer = 0.0f;
    bool touchNos = false;
    f32 animTime = 0.0f;
};

// Shaders compatíveis com qualquer GPU Android (Adreno, Mali, PowerVR)
static const char* kVertexShaderSrc = R"(
attribute vec3 aPos;
attribute vec4 aColor;
uniform mat4 uMVP;
varying vec4 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

static const char* kFragmentShaderSrc = R"(
precision mediump float;
varying vec4 vColor;
void main() {
    gl_FragColor = vColor;
}
)";

static GLuint CompileGLShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info[512];
        glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        LOGE("Erro ao compilar shader: %s", info);
    }
    return shader;
}

static void InitSceneShaders(struct EngineState* engine) {
    GLuint vs = CompileGLShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    GLuint fs = CompileGLShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    engine->program = glCreateProgram();
    glAttachShader(engine->program, vs);
    glAttachShader(engine->program, fs);
    glLinkProgram(engine->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    engine->aPosLoc = glGetAttribLocation(engine->program, "aPos");
    engine->aColorLoc = glGetAttribLocation(engine->program, "aColor");
    engine->uMvpLoc = glGetUniformLocation(engine->program, "uMVP");
}

// Matemática de Matrizes 4x4 (Column-Major para OpenGL)
struct Mat4 {
    float m[16];

    static Mat4 Identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 Perspective(float fovYRad, float aspect, float zn, float zf) {
        Mat4 r{};
        float f = 1.0f / std::tan(fovYRad * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zf * zn) / (zn - zf);
        return r;
    }

    static Mat4 LookAt(float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
        float fx = cx - ex, fy = cy - ey, fz = cz - ez;
        float flen = std::sqrt(fx*fx + fy*fy + fz*fz);
        if (flen > 0.0001f) { fx /= flen; fy /= flen; fz /= flen; }

        float sx = fy * uz - fz * uy;
        float sy = fz * ux - fx * uz;
        float sz = fx * uy - fy * ux;
        float slen = std::sqrt(sx*sx + sy*sy + sz*sz);
        if (slen > 0.0001f) { sx /= slen; sy /= slen; sz /= slen; }

        float vx = sy * fz - sz * fy;
        float vy = sz * fx - sx * fz;
        float vz = sx * fy - sy * fx;

        Mat4 r = Identity();
        r.m[0] = sx; r.m[4] = sy; r.m[8]  = sz; r.m[12] = -(sx*ex + sy*ey + sz*ez);
        r.m[1] = vx; r.m[5] = vy; r.m[9]  = vz; r.m[13] = -(vx*ex + vy*ey + vz*ez);
        r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz; r.m[14] = (fx*ex + fy*ey + fz*ez);
        return r;
    }

    static Mat4 Multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                r.m[c * 4 + row] =
                    a.m[0 * 4 + row] * b.m[c * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[c * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[c * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[c * 4 + 3];
            }
        }
        return r;
    }
};

// Construtores de Geometria 3D Dinâmica
static void PushTri(std::vector<ColoredVertex>& v,
                    float x1, float y1, float z1,
                    float x2, float y2, float z2,
                    float x3, float y3, float z3,
                    float r, float g, float b, float a = 1.0f) {
    v.push_back({x1, y1, z1, r, g, b, a});
    v.push_back({x2, y2, z2, r, g, b, a});
    v.push_back({x3, y3, z3, r, g, b, a});
}

static void PushQuad3D(std::vector<ColoredVertex>& v,
                       float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float x3, float y3, float z3,
                       float x4, float y4, float z4,
                       float r, float g, float b, float a = 1.0f) {
    PushTri(v, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b, a);
    PushTri(v, x1, y1, z1, x3, y3, z3, x4, y4, z4, r, g, b, a);
}

// Desenha uma caixa 3D (chassi, prédios, rodas, aerofólio) com sombreamento por face
static void PushBox3D(std::vector<ColoredVertex>& v,
                      float cx, float cy, float cz,
                      float hx, float hy, float hz,
                      float r, float g, float b, float a = 1.0f) {
    float x0 = cx - hx, x1 = cx + hx;
    float y0 = cy - hy, y1 = cy + hy;
    float z0 = cz - hz, z1 = cz + hz;

    // Topo (mais claro)
    PushQuad3D(v, x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1,
               std::min(1.0f, r * 1.15f), std::min(1.0f, g * 1.15f), std::min(1.0f, b * 1.15f), a);
    // Traseira (-Z, visível para a câmera)
    PushQuad3D(v, x1, y0, z0, x0, y0, z0, x0, y1, z0, x1, y1, z0,
               r * 0.90f, g * 0.90f, b * 0.90f, a);
    // Frente (+Z)
    PushQuad3D(v, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1,
               r * 0.95f, g * 0.95f, b * 0.95f, a);
    // Lateral Esquerda (-X)
    PushQuad3D(v, x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0,
               r * 0.78f, g * 0.78f, b * 0.78f, a);
    // Lateral Direita (+X)
    PushQuad3D(v, x1, y0, z1, x1, y0, z0, x1, y1, z0, x1, y1, z1,
               r * 0.78f, g * 0.78f, b * 0.78f, a);
}

// Desenha a BMW M3 GTR 3D (Prata com Faixas Azuis icônicas do Most Wanted)
static void BuildBMWM3GTR(std::vector<ColoredVertex>& v, float cx, float cy, float cz, float steer, bool nosActive, float animTime) {
    // Sombra no asfalto
    PushQuad3D(v, cx - 1.15f, 0.02f, cz - 2.4f, cx + 1.15f, 0.02f, cz - 2.4f,
                  cx + 1.15f, 0.02f, cz + 2.4f, cx - 1.15f, 0.02f, cz + 2.4f,
                  0.02f, 0.02f, 0.03f, 0.75f);

    float bodyY = cy + 0.42f;
    // Chassi Principal Prata Metálico
    PushBox3D(v, cx, bodyY, cz, 0.98f, 0.28f, 2.20f, 0.82f, 0.85f, 0.88f);

    // Para-lamas alargados (Widebody Kit GTR)
    PushBox3D(v, cx - 0.96f, bodyY - 0.04f, cz + 1.25f, 0.10f, 0.22f, 0.55f, 0.12f, 0.35f, 0.78f);
    PushBox3D(v, cx + 0.96f, bodyY - 0.04f, cz + 1.25f, 0.10f, 0.22f, 0.55f, 0.12f, 0.35f, 0.78f);
    PushBox3D(v, cx - 0.96f, bodyY - 0.04f, cz - 1.25f, 0.10f, 0.22f, 0.55f, 0.12f, 0.35f, 0.78f);
    PushBox3D(v, cx + 0.96f, bodyY - 0.04f, cz - 1.25f, 0.10f, 0.22f, 0.55f, 0.12f, 0.35f, 0.78f);

    // Vinil Azul Royal (Faixas centrais e laterais da BMW M3 GTR)
    PushBox3D(v, cx - 0.38f, bodyY + 0.285f, cz + 0.2f, 0.18f, 0.01f, 1.95f, 0.08f, 0.32f, 0.82f);
    PushBox3D(v, cx + 0.38f, bodyY + 0.285f, cz + 0.2f, 0.18f, 0.01f, 1.95f, 0.08f, 0.32f, 0.82f);
    PushBox3D(v, cx, bodyY + 0.02f, cz, 1.01f, 0.11f, 1.40f, 0.08f, 0.32f, 0.82f);

    // Cabine e Vidros Fumê
    PushBox3D(v, cx, bodyY + 0.42f, cz - 0.15f, 0.76f, 0.24f, 1.05f, 0.12f, 0.15f, 0.22f);
    // Teto Prata com faixa azul
    PushBox3D(v, cx, bodyY + 0.67f, cz - 0.15f, 0.70f, 0.03f, 0.85f, 0.82f, 0.85f, 0.88f);
    PushBox3D(v, cx, bodyY + 0.705f, cz - 0.15f, 0.25f, 0.01f, 0.82f, 0.08f, 0.32f, 0.82f);

    // Aerofólio Traseiro de Fibra de Carbono (GTR Racing Wing)
    PushBox3D(v, cx - 0.65f, bodyY + 0.45f, cz - 2.00f, 0.04f, 0.22f, 0.12f, 0.10f, 0.10f, 0.12f);
    PushBox3D(v, cx + 0.65f, bodyY + 0.45f, cz - 2.00f, 0.04f, 0.22f, 0.12f, 0.10f, 0.10f, 0.12f);
    PushBox3D(v, cx, bodyY + 0.66f, cz - 2.05f, 0.96f, 0.04f, 0.22f, 0.12f, 0.12f, 0.15f);

    // Rodas (4 pneus + aros esportivos)
    float wheelX[2] = { cx - 0.98f, cx + 0.98f };
    float wheelZ[2] = { cz - 1.30f, cz + 1.30f };
    for (int ix = 0; ix < 2; ++ix) {
        for (int iz = 0; iz < 2; ++iz) {
            PushBox3D(v, wheelX[ix], cy + 0.28f, wheelZ[iz], 0.14f, 0.28f, 0.32f, 0.08f, 0.08f, 0.09f);
            PushBox3D(v, wheelX[ix] + (ix == 0 ? -0.02f : 0.02f), cy + 0.28f, wheelZ[iz], 0.13f, 0.17f, 0.19f, 0.75f, 0.78f, 0.82f);
        }
    }

    // Lanternas Traseiras Vermelhas Brilhantes
    PushBox3D(v, cx - 0.65f, bodyY + 0.08f, cz - 2.21f, 0.24f, 0.10f, 0.03f, 1.0f, 0.12f, 0.08f);
    PushBox3D(v, cx + 0.65f, bodyY + 0.08f, cz - 2.21f, 0.24f, 0.10f, 0.03f, 1.0f, 0.12f, 0.08f);
    // Placa "NFSMW"
    PushBox3D(v, cx, bodyY - 0.04f, cz - 2.21f, 0.26f, 0.09f, 0.02f, 0.95f, 0.80f, 0.15f);

    // Faróis Dianteiros Xenon
    PushBox3D(v, cx - 0.65f, bodyY + 0.06f, cz + 2.21f, 0.22f, 0.09f, 0.03f, 0.95f, 0.98f, 1.0f);
    PushBox3D(v, cx + 0.65f, bodyY + 0.06f, cz + 2.21f, 0.22f, 0.09f, 0.03f, 0.95f, 0.98f, 1.0f);

    // Chamas Azuis do Nitro (N2O) saindo do escapamento quando ativado!
    if (nosActive) {
        float pulse = 0.65f + 0.35f * std::sin(animTime * 45.0f);
        PushBox3D(v, cx - 0.45f, bodyY - 0.14f, cz - 2.25f - pulse * 0.6f, 0.10f, 0.08f, pulse * 0.65f, 0.20f, 0.65f, 1.0f);
        PushBox3D(v, cx + 0.45f, bodyY - 0.14f, cz - 2.25f - pulse * 0.6f, 0.10f, 0.08f, pulse * 0.65f, 0.20f, 0.65f, 1.0f);
        PushBox3D(v, cx - 0.45f, bodyY - 0.14f, cz - 2.25f - pulse * 0.3f, 0.06f, 0.05f, pulse * 0.35f, 0.85f, 0.95f, 1.0f);
        PushBox3D(v, cx + 0.45f, bodyY - 0.14f, cz - 2.25f - pulse * 0.3f, 0.06f, 0.05f, pulse * 0.35f, 0.85f, 0.95f, 1.0f);
    }
}

// Desenha Viatura Policial de Rockport com Giroflex Vermelho/Azul Piscante
static void BuildPoliceCruiser(std::vector<ColoredVertex>& v, float cx, float cy, float cz, float animTime, int unitIdx) {
    float bodyY = cy + 0.42f;
    // Chassi Preto & Branco (Rockport PD)
    PushBox3D(v, cx, bodyY, cz, 0.95f, 0.27f, 2.15f, 0.10f, 0.10f, 0.12f);
    // Portas e Teto Brancos
    PushBox3D(v, cx, bodyY + 0.02f, cz, 0.97f, 0.25f, 1.05f, 0.92f, 0.94f, 0.96f);
    PushBox3D(v, cx, bodyY + 0.40f, cz - 0.10f, 0.75f, 0.22f, 0.95f, 0.15f, 0.18f, 0.24f);
    PushBox3D(v, cx, bodyY + 0.63f, cz - 0.10f, 0.70f, 0.04f, 0.80f, 0.92f, 0.94f, 0.96f);

    // Giroflex Policial (Sirene Piscante Vermelha e Azul)
    bool flashPhase = (static_cast<int>(animTime * 10.0f + unitIdx) % 2) == 0;
    float rL = flashPhase ? 1.0f : 0.1f;
    float bL = flashPhase ? 0.1f : 1.0f;
    float rR = flashPhase ? 0.1f : 1.0f;
    float bR = flashPhase ? 1.0f : 0.1f;

    PushBox3D(v, cx - 0.28f, bodyY + 0.72f, cz - 0.10f, 0.24f, 0.07f, 0.12f, rL, 0.08f, bL);
    PushBox3D(v, cx + 0.28f, bodyY + 0.72f, cz - 0.10f, 0.24f, 0.07f, 0.12f, rR, 0.08f, bR);

    // Faróis e Quebra-Mato Frontal
    PushBox3D(v, cx, bodyY - 0.02f, cz + 2.20f, 0.45f, 0.22f, 0.06f, 0.18f, 0.18f, 0.20f);
    PushBox3D(v, cx - 0.62f, bodyY + 0.05f, cz + 2.16f, 0.20f, 0.08f, 0.03f, 1.0f, 0.98f, 0.90f);
    PushBox3D(v, cx + 0.62f, bodyY + 0.05f, cz + 2.16f, 0.20f, 0.08f, 0.03f, 1.0f, 0.98f, 0.90f);
}

// Helper para desenhar retângulos 2D no HUD (coordenadas normalizadas -1..+1)
static void PushRect2D(std::vector<ColoredVertex>& v,
                       float x0, float y0, float x1, float y1,
                       float r, float g, float b, float a = 1.0f) {
    PushQuad3D(v, x0, y0, 0.0f, x1, y0, 0.0f, x1, y1, 0.0f, x0, y1, 0.0f, r, g, b, a);
}

// Inicializa a superfície EGL com fallback automático (GLES 3.0 -> GLES 2.0, Depth 24 -> 16)
static int InitDisplay(struct EngineState* engine) {
    if (!engine->app || !engine->app->window) {
        LOGE("Janela nativa nula ao tentar inicializar EGL!");
        return -1;
    }

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("Falha em eglGetDisplay");
        return -1;
    }
    eglInitialize(display, nullptr, nullptr);

    const EGLint attribs16[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display, attribs16, &config, 1, &numConfigs) || numConfigs <= 0) {
        // Fallback universal mínimo
        const EGLint fallbackAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE
        };
        eglChooseConfig(display, fallbackAttribs, &config, 1, &numConfigs);
    }

    if (numConfigs <= 0) {
        LOGE("Nenhuma configuracao EGL encontrada!");
        return -1;
    }

    EGLint format = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, format);

    // Tenta criar contexto OpenGL ES 3.0 primeiro; se não suportado, usa OpenGL ES 2.0
    EGLint ctxAttribs3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttribs3);
    if (context == EGL_NO_CONTEXT) {
        EGLint ctxAttribs2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttribs2);
    }

    EGLSurface surface = eglCreateWindowSurface(display, config, engine->app->window, nullptr);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT) {
        LOGE("Falha ao criar superficie ou contexto EGL!");
        return -1;
    }

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGW("Impossivel ativar contexto EGL!");
        return -1;
    }

    EGLint w = 0, h = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    engine->display = display;
    engine->context = context;
    engine->surface = surface;
    engine->width = w;
    engine->height = h;

    InitSceneShaders(engine);
    engine->renderer.Init(w, h);

    LOGI("Contexto OpenGL ES ativo: %dx%d", w, h);
    return 0;
}

// Renderiza a cena 3D completa + HUD 2D a 60 FPS
static void DrawFrame(struct EngineState* engine) {
    if (engine->display == EGL_NO_DISPLAY || engine->surface == EGL_NO_SURFACE) return;

    f32 dt = 0.016f;
    engine->animTime += dt;

    // 1. Atualiza Física da BMW M3 GTR, Streaming de Rockport e IA Policial
    engine->car.Update(dt, engine->touchThrottle, engine->touchBrake, engine->touchSteer, false, engine->touchNos);
    Vector3 carPos = engine->car.GetPosition();
    engine->world.Update(carPos);
    engine->pursuit.Update(dt, carPos, engine->car.GetVelocity());

    // 2. Configura Viewport e Céu Dourado/Sépia de Rockport ("Visual Most Wanted PS2")
    glViewport(0, 0, engine->width, engine->height);
    glClearColor(0.76f, 0.58f, 0.34f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(engine->program);

    // 3. Constrói Geometria 3D do Mundo (Rodovia + Prédios + Carros)
    std::vector<ColoredVertex> worldVerts;
    worldVerts.reserve(4096);

    float zBase = std::floor(carPos.z / 10.0f) * 10.0f;
    float zStart = zBase - 40.0f;
    float zEnd = zBase + 180.0f;

    // Solo / Gramado e Calçadas de Rockport
    PushQuad3D(worldVerts, -120.0f, -0.05f, zStart, 120.0f, -0.05f, zStart,
                           120.0f, -0.05f, zEnd, -120.0f, -0.05f, zEnd,
                           0.32f, 0.28f, 0.18f);

    // Pista de Asfalto (Largura: -12m a +12m)
    PushQuad3D(worldVerts, -12.0f, 0.0f, zStart, 12.0f, 0.0f, zStart,
                           12.0f, 0.0f, zEnd, -12.0f, 0.0f, zEnd,
                           0.16f, 0.17f, 0.19f);

    // Faixas da Pista, Zebras Laterais, Guardrails, Postes e Prédios de Rockport
    for (int seg = -4; seg < 18; ++seg) {
        float sz0 = zBase + seg * 10.0f;
        float sz1 = sz0 + 5.0f;
        float sz2 = sz0 + 10.0f;

        // Faixa Dupla Amarela Central
        PushQuad3D(worldVerts, -0.25f, 0.02f, sz0, -0.08f, 0.02f, sz0,
                               -0.08f, 0.02f, sz2, -0.25f, 0.02f, sz2,
                               0.95f, 0.75f, 0.12f);
        PushQuad3D(worldVerts, 0.08f, 0.02f, sz0, 0.25f, 0.02f, sz0,
                               0.25f, 0.02f, sz2, 0.08f, 0.02f, sz2,
                               0.95f, 0.75f, 0.12f);

        // Faixas Tracejadas Brancas das Pistas Laterais
        PushQuad3D(worldVerts, -6.1f, 0.02f, sz0, -5.9f, 0.02f, sz0,
                               -5.9f, 0.02f, sz1, -6.1f, 0.02f, sz1,
                               0.88f, 0.88f, 0.90f);
        PushQuad3D(worldVerts, 5.9f, 0.02f, sz0, 6.1f, 0.02f, sz0,
                               6.1f, 0.02f, sz1, 5.9f, 0.02f, sz1,
                               0.88f, 0.88f, 0.90f);

        // Guardrails / Muretas Laterais Vermelhas e Brancas
        bool isRed = (seg % 2 == 0);
        float gr = isRed ? 0.85f : 0.92f;
        float gg = isRed ? 0.18f : 0.92f;
        float gb = isRed ? 0.15f : 0.92f;
        PushBox3D(worldVerts, -12.4f, 0.35f, sz0 + 5.0f, 0.35f, 0.35f, 5.0f, gr, gg, gb);
        PushBox3D(worldVerts,  12.4f, 0.35f, sz0 + 5.0f, 0.35f, 0.35f, 5.0f, gr, gg, gb);

        // Prédios e Galpões Industriais de Rockport nas margens da rodovia
        int worldSegIdx = static_cast<int>(std::floor(sz0 / 10.0f));
        if (worldSegIdx % 2 == 0) {
            float bHeight = 6.0f + (std::abs(worldSegIdx * 7) % 10);
            float tint = 0.35f + (std::abs(worldSegIdx * 13) % 4) * 0.08f;
            PushBox3D(worldVerts, -22.0f, bHeight * 0.5f, sz0 + 5.0f, 5.5f, bHeight * 0.5f, 4.2f,
                      tint, tint * 0.88f, tint * 0.72f);
            PushBox3D(worldVerts,  22.0f, bHeight * 0.5f, sz0 + 5.0f, 5.5f, bHeight * 0.5f, 4.2f,
                      tint * 0.90f, tint * 0.82f, tint * 0.70f);
        }
    }

    // 4. Constrói a BMW M3 GTR do Jogador
    BuildBMWM3GTR(worldVerts, carPos.x, 0.0f, carPos.z, engine->touchSteer,
                  engine->touchNos && engine->car.GetNosRemaining() > 1.0f, engine->animTime);

    // 5. Constrói as Viaturas Policiais em Perseguição Ativa
    const auto& cops = engine->pursuit.GetUnits();
    for (size_t i = 0; i < cops.size(); ++i) {
        if (cops[i].state == COP_WRECKED) continue;
        BuildPoliceCruiser(worldVerts, cops[i].position.x, 0.0f, cops[i].position.z, engine->animTime, static_cast<int>(i));
    }

    // 6. Calcula Câmera Dinâmica de Perseguição (3ª Pessoa estilo NFS Most Wanted)
    float aspect = (engine->height > 0) ? (static_cast<float>(engine->width) / static_cast<float>(engine->height)) : 1.77f;
    float fovDeg = (engine->touchNos && engine->car.GetNosRemaining() > 1.0f) ? 72.0f : 60.0f;
    Mat4 proj = Mat4::Perspective(fovDeg * 3.14159265f / 180.0f, aspect, 0.5f, 220.0f);

    float camX = carPos.x * 0.78f - engine->touchSteer * 0.85f;
    float camY = 2.35f;
    float camZ = carPos.z - 6.4f;
    Mat4 view = Mat4::LookAt(camX, camY, camZ,
                             carPos.x, 0.85f, carPos.z + 12.0f,
                             0.0f, 1.0f, 0.0f);
    Mat4 mvp = Mat4::Multiply(proj, view);

    glUniformMatrix4fv(engine->uMvpLoc, 1, GL_FALSE, mvp.m);
    glEnableVertexAttribArray(engine->aPosLoc);
    glEnableVertexAttribArray(engine->aColorLoc);
    glVertexAttribPointer(engine->aPosLoc, 3, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), &worldVerts[0].x);
    glVertexAttribPointer(engine->aColorLoc, 4, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), &worldVerts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(worldVerts.size()));

    // 7. Renderiza HUD 2D (Velocímetro, RPM, Nitro, Barra Policial Busted/Evade e Pedais Touch)
    glDisable(GL_DEPTH_TEST);
    std::vector<ColoredVertex> hudVerts;
    hudVerts.reserve(512);

    // Topo Central: Medidor de Perseguição Policial (Vermelho = BUSTED / Azul = EVADE)
    PushRect2D(hudVerts, -0.45f, 0.84f, 0.45f, 0.93f, 0.08f, 0.08f, 0.10f, 0.85f);
    float bustedRatio = std::clamp(engine->pursuit.GetBustedMeter() / 100.0f, 0.0f, 1.0f);
    float evadeRatio  = std::clamp(engine->pursuit.GetEvadeMeter() / 100.0f, 0.0f, 1.0f);
    // Sirene decorativa central na barra de perseguição
    bool hudFlash = (static_cast<int>(engine->animTime * 8.0f) % 2) == 0;
    PushRect2D(hudVerts, -0.44f, 0.85f, -0.01f, 0.92f, hudFlash ? 0.90f : 0.25f, 0.10f, 0.10f, 1.0f);
    PushRect2D(hudVerts,  0.01f, 0.85f,  0.44f, 0.92f, 0.10f, 0.35f, hudFlash ? 0.25f : 0.95f, 1.0f);
    if (bustedRatio > 0.01f) {
        PushRect2D(hudVerts, 0.0f - bustedRatio * 0.44f, 0.85f, 0.0f, 0.92f, 1.0f, 0.05f, 0.05f, 1.0f);
    }
    if (evadeRatio > 0.01f) {
        PushRect2D(hudVerts, 0.0f, 0.85f, evadeRatio * 0.44f, 0.92f, 0.15f, 0.75f, 1.0f, 1.0f);
    }

    // Indicadores de Nível de Heat (1 a 5 estrelas/blocos laranja no topo esquerdo)
    int heat = static_cast<int>(engine->pursuit.GetCurrentHeat());
    for (int h = 0; h < 5; ++h) {
        float hx0 = -0.92f + h * 0.07f;
        bool activeHeat = (h < heat);
        PushRect2D(hudVerts, hx0, 0.84f, hx0 + 0.055f, 0.93f,
                   activeHeat ? 1.0f : 0.22f,
                   activeHeat ? 0.55f : 0.22f,
                   activeHeat ? 0.08f : 0.25f, 1.0f);
    }

    // Painel Direito Inferior: Velocímetro, RPM e Nitro (N2O)
    float speedRatio = std::clamp(engine->car.GetSpeedKmh() / 320.0f, 0.0f, 1.0f);
    float rpmRatio   = std::clamp(engine->car.GetRPM() / 8500.0f, 0.0f, 1.0f);
    float nosRatio   = std::clamp(engine->car.GetNosRemaining() / 100.0f, 0.0f, 1.0f);

    // Fundo do painel de instrumentos
    PushRect2D(hudVerts, 0.32f, -0.94f, 0.94f, -0.58f, 0.06f, 0.07f, 0.09f, 0.85f);
    // Barra de Velocidade (Verde -> Amarelo -> Laranja)
    PushRect2D(hudVerts, 0.35f, -0.68f, 0.35f + speedRatio * 0.56f, -0.61f,
               0.20f + speedRatio * 0.80f, 0.90f - speedRatio * 0.30f, 0.15f, 1.0f);
    // Barra de RPM (Branco -> Vermelho no Redline)
    PushRect2D(hudVerts, 0.35f, -0.79f, 0.35f + rpmRatio * 0.56f, -0.72f,
               rpmRatio > 0.82f ? 1.0f : 0.85f, rpmRatio > 0.82f ? 0.15f : 0.85f, 0.20f, 1.0f);
    // Barra de Nitro N2O (Azul Ciano)
    PushRect2D(hudVerts, 0.35f, -0.90f, 0.35f + nosRatio * 0.56f, -0.83f,
               0.12f, 0.68f, 1.0f, 1.0f);

    // Botões Virtuais de Toque na Tela (Direção Esquerda/Direita + Pedal Acelerador + Botão N2O)
    // Botão Virar Esquerda (<)
    bool steerLeftActive = (engine->touchSteer < -0.15f);
    PushRect2D(hudVerts, -0.94f, -0.88f, -0.64f, -0.48f,
               steerLeftActive ? 0.95f : 0.20f, steerLeftActive ? 0.65f : 0.24f, 0.12f, 0.75f);
    // Botão Virar Direita (>)
    bool steerRightActive = (engine->touchSteer > 0.15f);
    PushRect2D(hudVerts, -0.58f, -0.88f, -0.28f, -0.48f,
               steerRightActive ? 0.95f : 0.20f, steerRightActive ? 0.65f : 0.24f, 0.12f, 0.75f);

    // Botão NITRO N2O (Direita Acima)
    PushRect2D(hudVerts, 0.62f, -0.15f, 0.94f, 0.25f,
               engine->touchNos ? 0.25f : 0.08f, engine->touchNos ? 0.85f : 0.42f, 1.0f, 0.80f);

    // Pedal ACELERADOR (Direita Meio)
    bool gasActive = (engine->touchThrottle > 0.6f);
    PushRect2D(hudVerts, 0.62f, -0.54f, 0.94f, -0.20f,
               0.15f, gasActive ? 0.92f : 0.48f, 0.22f, 0.80f);

    Mat4 hudMvp = Mat4::Identity();
    glUniformMatrix4fv(engine->uMvpLoc, 1, GL_FALSE, hudMvp.m);
    glVertexAttribPointer(engine->aPosLoc, 3, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), &hudVerts[0].x);
    glVertexAttribPointer(engine->aColorLoc, 4, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), &hudVerts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(hudVerts.size()));

    eglSwapBuffers(engine->display, engine->surface);
}

// Processamento de Multi-Touch na tela (Direção simultânea com Acelerador e Nitro)
static int32_t HandleInput(struct android_app* app, AInputEvent* event) {
    auto* engine = static_cast<struct EngineState*>(app->userData);
    if (!engine || engine->width <= 0 || engine->height <= 0) return 0;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t flags = action & AMOTION_EVENT_ACTION_MASK;
        int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        size_t pointerCount = AMotionEvent_getPointerCount(event);

        // Toque no canto superior esquerdo alterna o Heat Level Policial (1 a 5)
        if (flags == AMOTION_EVENT_ACTION_DOWN) {
            float tx = AMotionEvent_getX(event, 0);
            float ty = AMotionEvent_getY(event, 0);
            if (tx < engine->width * 0.35f && ty < engine->height * 0.18f) {
                int nextHeat = (static_cast<int>(engine->pursuit.GetCurrentHeat()) % 5) + 1;
                engine->pursuit.SetHeatLevel(static_cast<HeatLevel>(nextHeat));
            }
        }

        f32 newThrottle = 0.45f; // Mantém cruzeiro dinâmico quando solto
        f32 newSteer = 0.0f;
        bool newNos = false;

        if (flags != AMOTION_EVENT_ACTION_UP && flags != AMOTION_EVENT_ACTION_CANCEL) {
            for (size_t i = 0; i < pointerCount; ++i) {
                if (flags == AMOTION_EVENT_ACTION_POINTER_UP && static_cast<int32_t>(i) == pointerIndex) {
                    continue;
                }
                float x = AMotionEvent_getX(event, i);
                float y = AMotionEvent_getY(event, i);

                if (x > engine->width * 0.5f) {
                    // Lado direito: Acelerador Total + Nitro na metade superior
                    newThrottle = 1.0f;
                    if (y < engine->height * 0.58f) {
                        newNos = true;
                    }
                } else {
                    // Lado esquerdo: Direção proporcional ou botões < / >
                    float normX = (x / (engine->width * 0.45f)) * 2.0f - 1.0f;
                    newSteer = std::clamp(normX, -1.0f, 1.0f);
                }
            }
        }

        engine->touchThrottle = newThrottle;
        engine->touchSteer = newSteer;
        engine->touchNos = newNos;
        return 1;
    }
    return 0;
}

// Comandos de Ciclo de Vida do Android
static void HandleCmd(struct android_app* app, int32_t cmd) {
    auto* engine = static_cast<struct EngineState*>(app->userData);
    if (!engine) return;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (engine->app && engine->app->window != nullptr) {
                if (InitDisplay(engine) == 0) {
                    engine->hasFocus = true;
                    DrawFrame(engine);
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            engine->hasFocus = false;
            if (engine->display != EGL_NO_DISPLAY) {
                eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (engine->context != EGL_NO_CONTEXT) {
                    eglDestroyContext(engine->display, engine->context);
                }
                if (engine->surface != EGL_NO_SURFACE) {
                    eglDestroySurface(engine->display, engine->surface);
                }
                eglTerminate(engine->display);
            }
            engine->display = EGL_NO_DISPLAY;
            engine->context = EGL_NO_CONTEXT;
            engine->surface = EGL_NO_SURFACE;
            break;
        case APP_CMD_GAINED_FOCUS:
            engine->hasFocus = true;
            break;
        case APP_CMD_LOST_FOCUS:
            engine->hasFocus = false;
            break;
    }
}

// Ponto de entrada nativo chamado por ANativeActivity_onCreate (android_native_app_glue.c)
void android_main(struct android_app* state) {
    // Inicialização C++ segura (SEM memset sobre objetos std::vector/std::string!)
    EngineState engine;
    state->userData = &engine;
    state->onAppCmd = HandleCmd;
    state->onInputEvent = HandleInput;
    engine.app = state;

    LOGI("NFS: Most Wanted Native Port (3D OpenGL ES) iniciado com sucesso!");

    while (true) {
        int ident;
        int events;
        struct android_poll_source* source = nullptr;

        while ((ident = ALooper_pollOnce(engine.hasFocus ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) {
                source->process(state, source);
            }
            if (state->destroyRequested != 0) {
                LOGI("Encerrando atividade nativa.");
                return;
            }
        }

        if (engine.hasFocus && engine.display != EGL_NO_DISPLAY) {
            DrawFrame(&engine);
        }
    }
}

#endif
