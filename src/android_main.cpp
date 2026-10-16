#ifdef __ANDROID__

#include <jni.h>
#include <errno.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <android/sensor.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "ea_chunks.h"
#include "renderer.h"
#include "vehicle_physics.h"
#include "police_ai.h"
#include "world_streamer.h"

#define LOG_TAG "NFSMW_Native"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

// Vértice 3D com Posição, Normal de Superfície, Coordenadas UV de Textura e Cor/Material
struct LitVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

// Malha decodificada diretamente dos arquivos GEOMETRY.BIN e SCENERY_L2RA.BIN do PS2 (VU1 VIF Chunks)
struct DecodedPS2Mesh {
    std::vector<LitVertex> bodyVerts;   // Chassi, carroceria, fachada, etc.
    std::vector<LitVertex> wheelVerts;  // Rodas
    std::vector<LitVertex> sirenLVerts; // Giroflex esquerdo
    std::vector<LitVertex> sirenRVerts; // Giroflex direito
    bool loaded = false;
};

// Gerenciador de Texturas Reais do PS2 extraídas do Need for Speed: Most Wanted - Black Edition (BR)
struct PS2TextureManager {
    std::map<std::string, GLuint> textures;

    GLuint Get(const std::string& name) const {
        auto it = textures.find(name);
        return (it != textures.end()) ? it->second : 0;
    }

    void LoadPackFromAsset(AAssetManager* mgr, const char* assetPath) {
        if (!mgr) return;
        AAsset* asset = AAssetManager_open(mgr, assetPath, AASSET_MODE_BUFFER);
        if (!asset) {
            LOGW("Arquivo de texturas PS2 nao encontrado: %s", assetPath);
            return;
        }
        off_t len = AAsset_getLength(asset);
        std::vector<u8> buf(static_cast<size_t>(len));
        AAsset_read(asset, buf.data(), len);
        AAsset_close(asset);

        if (buf.size() < 12 || std::memcmp(buf.data(), "NFSWTEX1", 8) != 0) {
            LOGW("Cabecalho de texturas invalido em: %s", assetPath);
            return;
        }

        u32 count = 0;
        std::memcpy(&count, buf.data() + 8, 4);
        size_t p = 12;

        for (u32 i = 0; i < count && p + 44 <= buf.size(); ++i) {
            char nameBuf[33] = {0};
            std::memcpy(nameBuf, buf.data() + p, 32);
            std::string texName(nameBuf);
            p += 32;

            u32 w = 0, h = 0, dataLen = 0;
            std::memcpy(&w, buf.data() + p, 4);
            std::memcpy(&h, buf.data() + p + 4, 4);
            std::memcpy(&dataLen, buf.data() + p + 8, 4);
            p += 12;

            if (p + dataLen > buf.size()) break;
            const u8* rgbaData = buf.data() + p;
            p += dataLen;

            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);

            textures[texName] = tex;
        }

        LOGI("Gerenciador de Texturas: Carregadas %zu texturas reais PS2 do ativo '%s'!", textures.size(), assetPath);
    }
};

struct EngineState {
    struct android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int32_t width = 0;
    int32_t height = 0;
    bool hasFocus = false;

    // Shaders OpenGL ES (Iluminação EAGL4 + Texturização 2D)
    GLuint program = 0;
    GLint aPosLoc = -1;
    GLint aNormalLoc = -1;
    GLint aUVLoc = -1;
    GLint aColorLoc = -1;
    GLint uMvpLoc = -1;
    GLint uModelLoc = -1;
    GLint uUseLitLoc = -1;
    GLint uUseTexLoc = -1;
    GLint uTextureLoc = -1;

    // Subsistemas do Jogo
    VehicleSimulation car;
    PursuitSystem pursuit;
    WorldStreamer world;
    AndroidRenderer renderer;
    PS2TextureManager texMgr;

    // Modelos 3D Reais Extraídos do Need for Speed: Most Wanted - Black Edition (BR)
    DecodedPS2Mesh m3GtrRace;  // CARS\BMWM3GTRE46\GEOMETRY.BIN (9.292 vértices PS2 VU1)
    DecodedPS2Mesh m3Street;   // CARS\BMWM3\GEOMETRY.BIN (8.085 vértices PS2 VU1)
    DecodedPS2Mesh copCruiser; // CARS\COPMIDSIZE\GEOMETRY.BIN (3.866 vértices PS2 VU1)
    
    // Cenário 3D Real de Rockport (Record #28: eLabScenery_L2RA_TC_WestPark_Sunset.bin)
    DecodedPS2Mesh sceneryShop;    // XB_SHOPBLOCKC_1B_00 (Lojas e prédios comerciais)
    DecodedPS2Mesh sceneryHouse;   // XB_HOUSEE_2B_00 (Casas suburbanas)
    DecodedPS2Mesh sceneryFactory; // XB_IF_OLDFACTORYE_1Z_LL_00 (Fábrica antiga de tijolos)
    DecodedPS2Mesh sceneryTruss;   // XO_FWY_SIGNSUPPORTTRUSS_1B_00 (Treliça suspensa de rodovia)
    DecodedPS2Mesh sceneryLight;   // XO_STREETLIGHTCB_1B_00 (Postes de iluminação de Rockport)
    DecodedPS2Mesh sceneryBarrier; // XW_BARRCONCHWY8M_1B_SA_00 (Barreira de concreto real)
    DecodedPS2Mesh sceneryTree;    // XT_CT_FALLTREED_1B_JG_00 (Árvores de outono)
    DecodedPS2Mesh scenerySky;     // SKYDOME (Domo celeste de pôr do sol)

    bool useStreetM3 = false;
    size_t zdirEntries = 0;
    size_t tpkTexturesCount = 0;
    size_t gameplayEventsCount = 0;

    // Controles Multi-Touch e Animação
    f32 touchThrottle = 0.50f;
    f32 touchBrake = 0.0f;
    f32 touchSteer = 0.0f;
    bool touchNos = false;
    f32 animTime = 0.0f;
    f32 wheelSpinAngle = 0.0f;
};

// Lê um arquivo binário real empacotado em android/app/src/main/assets/
static std::vector<u8> ReadAssetBytes(AAssetManager* mgr, const char* assetPath) {
    if (!mgr) return {};
    AAsset* asset = AAssetManager_open(mgr, assetPath, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGW("Ativo nao encontrado no APK: %s", assetPath);
        return {};
    }
    off_t len = AAsset_getLength(asset);
    std::vector<u8> buf(static_cast<size_t>(len));
    AAsset_read(asset, buf.data(), len);
    AAsset_close(asset);
    LOGI("Ativo PS2 carregado do APK: %s (%lld bytes)", assetPath, static_cast<long long>(len));
    return buf;
}

// ============================================================================
// DECODIFICADOR NATIVO DE GEOMETRIA PS2 VU1 VIF (0x80134000 / 0x00134A02)
// Converte os pacotes vetoriais do PS2 (Need for Speed Most Wanted Black Edition BR)
// diretamente para malhas 3D OpenGL ES com normais de iluminação e UVs!
// ============================================================================
static void DecodePS2VIFPart(const u8* a02, size_t a02Len, const std::string& partName,
                             bool isPolice, DecodedPS2Mesh& outMesh) {
    bool isWheel = (partName.find("TIRE") != std::string::npos || partName.find("BRAKE_") != std::string::npos);
    bool isWindow = (partName.find("WINDOW") != std::string::npos);
    bool isBrakelight = (partName.find("BRAKELIGHT") != std::string::npos);
    bool isHeadlight = (partName.find("HEADLIGHT") != std::string::npos);
    bool isInterior = (partName.find("INTERIOR") != std::string::npos || partName.find("DRIVER") != std::string::npos);
    bool isBase = (partName.find("BASE_") != std::string::npos || partName.find("BUMPER") != std::string::npos);
    bool isCopLights = (partName.find("COP_LIGHTS") != std::string::npos);

    size_t p = 0;
    u8 curV0[4] = {0, 0, 0, 0};
    bool hasV0 = false;
    float curScale = 0.0002f;
    float curBbox[6] = {0};
    bool hasBbox = false;

    struct RegShort4 { int comps; int16_t v[4]; };
    struct RegByte4  { int comps; int8_t  v[4]; };
    std::map<int, RegShort4> posRegs;
    std::map<int, RegShort4> uvRegs;
    std::map<int, RegByte4>  normRegs;

    while (p + 4 <= a02Len) {
        u32 w = 0;
        std::memcpy(&w, a02 + p, 4);
        u32 cmd = (w >> 24) & 0x7F;

        if ((cmd & 0x60) == 0x60) { // VIF UNPACK
            int vn = (w >> 26) & 0x3;
            int vl = (w >> 24) & 0x3;
            int num = (w >> 16) & 0xFF;
            if (num == 0) num = 256;
            int addr = w & 0x3FF;
            int usn = (w >> 14) & 1;
            int comps = vn + 1;
            int bits = (vl == 0) ? 32 : ((vl == 1) ? 16 : ((vl == 2) ? 8 : 16));
            size_t byteLen = ((static_cast<size_t>(num) * comps * bits + 31) / 32) * 4;

            if (p + 4 + byteLen > a02Len) break;
            const u8* pay = a02 + p + 4;

            if (addr == 0 && bits == 8 && byteLen >= 4) {
                std::memcpy(curV0, pay, 4);
                hasV0 = true;
            } else if (addr == 1 && bits == 32 && byteLen >= 16) {
                float flts[4];
                std::memcpy(flts, pay, 16);
                if (flts[1] > 0.000001f && flts[1] < 1.0f) curScale = flts[1];
            } else if (addr == 2 && bits == 32 && num >= 2 && byteLen >= 24) {
                std::memcpy(curBbox, pay, 24);
                hasBbox = true;
            } else if (addr >= 5 && addr < 29 && bits == 16 && !usn) {
                const auto* vals = reinterpret_cast<const int16_t*>(pay);
                for (int r = 0; r < num; ++r) {
                    RegShort4 reg{comps, {0, 0, 0, 0}};
                    for (int c = 0; c < comps; ++c) reg.v[c] = vals[r * comps + c];
                    posRegs[addr + r] = reg;
                }
            } else if (addr >= 29 && addr < 70 && bits == 16 && !usn) {
                const auto* vals = reinterpret_cast<const int16_t*>(pay);
                for (int r = 0; r < num; ++r) {
                    RegShort4 reg{comps, {0, 0, 0, 0}};
                    for (int c = 0; c < comps; ++c) reg.v[c] = vals[r * comps + c];
                    uvRegs[addr + r] = reg;
                }
            } else if (addr >= 70 && bits == 8 && !usn) {
                const auto* vals = reinterpret_cast<const int8_t*>(pay);
                for (int r = 0; r < num; ++r) {
                    RegByte4 reg{comps, {0, 0, 127, 0}};
                    for (int c = 0; c < comps; ++c) reg.v[c] = vals[r * comps + c];
                    normRegs[addr + r] = reg;
                }
            }
            p += 4 + byteLen;
        } else if (cmd == 0x14) { // VIF MSCAL (Executa o lote na VU1)
            if (hasV0 && hasBbox && !posRegs.empty()) {
                int nv = curV0[0];
                struct RawV { float x, y, z, nx, ny, nz, u, v; };
                std::vector<RawV> batch;
                batch.reserve(nv);

                int rIdx = 5;
                int uIdx = 29;
                int nIdx = normRegs.empty() ? 77 : normRegs.begin()->first;

                while (static_cast<int>(batch.size()) < nv && posRegs.count(rIdx)) {
                    const auto& rx = posRegs[rIdx];
                    const auto& ry = posRegs[rIdx + 1];
                    const auto& rz = posRegs[rIdx + 2];
                    RegByte4 rnx = normRegs.count(nIdx)     ? normRegs[nIdx]     : RegByte4{4, {0, 0, 0, 0}};
                    RegByte4 rny = normRegs.count(nIdx + 1) ? normRegs[nIdx + 1] : RegByte4{4, {0, 0, 0, 0}};
                    RegByte4 rnz = normRegs.count(nIdx + 2) ? normRegs[nIdx + 2] : RegByte4{4, {127, 127, 127, 127}};
                    RegShort4 ru = uvRegs.count(uIdx)       ? uvRegs[uIdx]       : RegShort4{4, {0, 0, 0, 0}};
                    RegShort4 rv = uvRegs.count(uIdx + 1)   ? uvRegs[uIdx + 1]   : RegShort4{4, {0, 0, 0, 0}};

                    for (int k = 0; k < rx.comps && static_cast<int>(batch.size()) < nv; ++k) {
                        batch.push_back({
                            rx.v[k] * curScale,
                            ry.v[k] * curScale,
                            rz.v[k] * curScale,
                            rnx.v[k] / 127.0f,
                            rny.v[k] / 127.0f,
                            rnz.v[k] / 127.0f,
                            ru.v[k] / 4096.0f,
                            rv.v[k] / 4096.0f
                        });
                    }
                    rIdx += 3;
                    uIdx += 2;
                    nIdx += 3;
                }

                if (batch.size() >= 3) {
                    float minX = batch[0].x, minY = batch[0].y, minZ = batch[0].z;
                    for (const auto& v : batch) {
                        minX = std::min(minX, v.x);
                        minY = std::min(minY, v.y);
                        minZ = std::min(minZ, v.z);
                    }
                    float ox = std::round(curBbox[0] - minX);
                    float oy = std::round(curBbox[1] - minY);
                    float oz = std::round(curBbox[2] - minZ);

                    for (auto& v : batch) {
                        v.x += ox;
                        v.y += oy;
                        v.z += oz;
                    }

                    // Converte Triangle Strip para Triângulos OpenGL
                    for (size_t i = 2; i < batch.size(); ++i) {
                        const RawV& v0 = (i % 2 == 0) ? batch[i - 2] : batch[i - 1];
                        const RawV& v1 = (i % 2 == 0) ? batch[i - 1] : batch[i - 2];
                        const RawV& v2 = batch[i];

                        auto dist3 = [](const RawV& a, const RawV& b) {
                            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
                            return std::sqrt(dx * dx + dy * dy + dz * dz);
                        };
                        float d01 = dist3(v0, v1), d12 = dist3(v1, v2), d02 = dist3(v0, v2);
                        if (std::min({d01, d12, d02}) < 0.0004f) continue;
                        if (std::max({d01, d12, d02}) > 1.25f) continue;

                        const RawV* tri[3] = {&v0, &v1, &v2};
                        for (int tv = 0; tv < 3; ++tv) {
                            const RawV& pt = *tri[tv];
                            // Coordenadas PS2 (X=Frente, Y=Esquerda, Z=Cima) -> OpenGL (X=Direita, Y=Cima, Z=Frente)
                            float gx = -pt.y;
                            float gy = pt.z;
                            float gz = pt.x;
                            float gnx = -pt.ny;
                            float gny = pt.nz;
                            float gnz = pt.nx;
                            float gu = pt.u;
                            float gv = pt.v;

                            // Cores e materiais base
                            float r = 0.90f, g = 0.90f, b = 0.92f, a = 1.0f;

                            if (isWheel) {
                                float rad = std::sqrt(pt.x * pt.x + pt.z * pt.z);
                                if (rad > 0.225f) { r = 0.12f; g = 0.12f; b = 0.14f; }
                                else              { r = 0.85f; g = 0.88f; b = 0.92f; }
                            } else if (isCopLights) {
                                r = 0.98f; g = 0.98f; b = 1.0f;
                            } else if (isBrakelight) {
                                r = 1.0f; g = 0.10f; b = 0.08f;
                            } else if (isHeadlight) {
                                r = 0.95f; g = 0.98f; b = 1.0f;
                            } else if (isWindow) {
                                r = 0.16f; g = 0.22f; b = 0.30f;
                            } else if (isInterior) {
                                r = 0.18f; g = 0.20f; b = 0.24f;
                            } else if (isPolice) {
                                if (std::abs(pt.x) < 1.15f && pt.z > 0.25f) {
                                    r = 0.95f; g = 0.96f; b = 0.98f;
                                } else {
                                    r = 0.10f; g = 0.11f; b = 0.14f;
                                }
                            } else if (isBase) {
                                r = 0.16f; g = 0.17f; b = 0.20f;
                            } else {
                                // Cores M3 GTR
                                float absY = std::abs(pt.y);
                                bool isRearWing = (pt.x < -1.52f && pt.z > 0.80f);
                                bool isFrontSplitter = (pt.x > 1.92f && pt.z < 0.22f);
                                bool isGrille = (pt.x > 2.22f && absY < 0.36f && pt.z < 0.56f);
                                bool isBlueStripe = (pt.z > 0.52f && absY > 0.16f && absY < 0.46f && pt.x > -1.45f);
                                bool isBlueSide = (absY > 0.68f && pt.z > 0.24f && pt.z < 0.66f &&
                                                  ((pt.x > -0.85f && pt.x < 0.95f) || (pt.x > 1.15f && pt.x < 1.95f)));
                                bool isBlueRear = (absY > 0.55f && pt.x < -0.95f && pt.x > -1.85f && pt.z > 0.42f && pt.z < 0.78f);

                                if (isRearWing || isFrontSplitter || isGrille) {
                                    r = 0.12f; g = 0.13f; b = 0.16f;
                                } else if (isBlueStripe || isBlueSide || isBlueRear) {
                                    r = 0.08f; g = 0.34f; b = 0.88f;
                                } else {
                                    r = 0.85f; g = 0.88f; b = 0.93f;
                                }
                            }

                            LitVertex lv{gx, gy, gz, gnx, gny, gnz, gu, gv, r, g, b, a};
                            if (isWheel) {
                                outMesh.wheelVerts.push_back(lv);
                            } else if (isCopLights) {
                                if (gx < 0.0f) outMesh.sirenLVerts.push_back(lv);
                                else           outMesh.sirenRVerts.push_back(lv);
                            } else {
                                outMesh.bodyVerts.push_back(lv);
                            }
                        }
                    }
                }
            }
            hasV0 = false;
            hasBbox = false;
            posRegs.clear();
            uvRegs.clear();
            normRegs.clear();
            p += 4;
        } else {
            p += 4;
        }
    }
}

static DecodedPS2Mesh LoadPS2Geometry(AAssetManager* mgr, const char* assetPath, const std::string& targetPartFilter, bool isPolice) {
    DecodedPS2Mesh mesh;
    std::vector<u8> data = ReadAssetBytes(mgr, assetPath);
    if (data.size() < 16) return mesh;

    mesh.bodyVerts.reserve(25000);

    size_t pos = 8;
    size_t end = data.size();
    int decodedParts = 0;

    while (pos + 8 <= end) {
        u32 cid = 0, clen = 0;
        std::memcpy(&cid, data.data() + pos, 4);
        std::memcpy(&clen, data.data() + pos + 4, 4);
        if (pos + 8 + clen > end) break;

        if (cid == 0x80134010) { // CHUNK_PART_MESH_DATA
            size_t subpos = pos + 8;
            size_t subend = pos + 8 + clen;
            std::string partName;
            const u8* a02Ptr = nullptr;
            size_t a02Len = 0;

            while (subpos + 8 <= subend) {
                u32 sc = 0, sl = 0;
                std::memcpy(&sc, data.data() + subpos, 4);
                std::memcpy(&sl, data.data() + subpos + 4, 4);
                if (subpos + 8 + sl > subend) break;

                if (sc == 0x00134011 && sl > 160) {
                    const char* strStart = reinterpret_cast<const char*>(data.data() + subpos + 8 + 160);
                    size_t maxStr = sl - 160;
                    partName = std::string(strStart, strnlen(strStart, maxStr));
                } else if (sc == 0x80134100) {
                    size_t p2 = subpos + 8;
                    size_t e2 = subpos + 8 + sl;
                    while (p2 + 8 <= e2) {
                        u32 c2 = 0, l2 = 0;
                        std::memcpy(&c2, data.data() + p2, 4);
                        std::memcpy(&l2, data.data() + p2 + 4, 4);
                        if (p2 + 8 + l2 > e2) break;
                        if (c2 == 0x00134A02) {
                            a02Ptr = data.data() + p2 + 8;
                            a02Len = l2;
                        }
                        p2 += 8 + l2;
                    }
                }
                subpos += 8 + sl;
            }

            bool shouldDecode = false;
            if (targetPartFilter.empty()) {
                // Carro: LOD _B
                if (partName.size() >= 2 && partName.substr(partName.size() - 2) == "_B") {
                    if (partName.find("STYLE") == std::string::npos &&
                        partName.find("DAMAGE1") == std::string::npos &&
                        (isPolice || partName.find("DAMAGE0") == std::string::npos)) {
                        shouldDecode = true;
                    }
                }
            } else {
                // Cenário: filtra por nome específico (ex: "XB_SHOPBLOCKC", "XB_HOUSEE", "SKYDOME", etc.)
                if (partName.find(targetPartFilter) != std::string::npos) {
                    shouldDecode = true;
                }
            }

            if (shouldDecode && a02Ptr && a02Len > 0) {
                DecodePS2VIFPart(a02Ptr, a02Len, partName, isPolice, mesh);
                decodedParts++;
            }
        }
        pos += 8 + clen;
    }

    mesh.loaded = !mesh.bodyVerts.empty();
    LOGI("Geometria PS2 '%s' [%s]: %d partes, %zu vertices chassi, %zu vertices roda",
         assetPath, targetPartFilter.c_str(), decodedParts, mesh.bodyVerts.size(), mesh.wheelVerts.size());
    return mesh;
}

// Shaders OpenGL ES com suporte a Textura 2D + Iluminação EAGL4
static const char* kVertexShaderSrc = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUV;
attribute vec4 aColor;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uUseLit;
uniform float uUseTex;
varying vec4 vColor;
varying vec2 vUV;
varying float vUseTex;
void main() {
    gl_Position = uMVP * uModel * vec4(aPos, 1.0);
    vUV = aUV;
    vUseTex = uUseTex;
    if (uUseLit > 0.5) {
        vec3 N = normalize(mat3(uModel[0].xyz, uModel[1].xyz, uModel[2].xyz) * aNormal);
        vec3 L = normalize(vec3(0.45, 0.82, -0.35));
        vec3 V = normalize(vec3(0.0, 0.35, -1.0));
        vec3 H = normalize(L + V);
        float diff = max(dot(N, L), 0.0);
        float hemi = 0.52 + 0.48 * clamp(N.y, -1.0, 1.0);
        float spec = pow(max(dot(N, H), 0.0), 18.0) * 0.42;
        vec3 lit = aColor.rgb * (0.38 * hemi + 0.68 * diff) + vec3(spec);
        vColor = vec4(clamp(lit, 0.0, 1.0), aColor.a);
    } else {
        vColor = aColor;
    }
}
)";

static const char* kFragmentShaderSrc = R"(
precision mediump float;
varying vec4 vColor;
varying vec2 vUV;
varying float vUseTex;
uniform sampler2D uTexture;
void main() {
    if (vUseTex > 0.5) {
        vec4 tex = texture2D(uTexture, vUV);
        gl_FragColor = tex * vColor;
    } else {
        gl_FragColor = vColor;
    }
}
)";

static GLuint CompileGLShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
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

    engine->aPosLoc     = glGetAttribLocation(engine->program, "aPos");
    engine->aNormalLoc  = glGetAttribLocation(engine->program, "aNormal");
    engine->aUVLoc      = glGetAttribLocation(engine->program, "aUV");
    engine->aColorLoc   = glGetAttribLocation(engine->program, "aColor");
    engine->uMvpLoc     = glGetUniformLocation(engine->program, "uMVP");
    engine->uModelLoc   = glGetUniformLocation(engine->program, "uModel");
    engine->uUseLitLoc  = glGetUniformLocation(engine->program, "uUseLit");
    engine->uUseTexLoc  = glGetUniformLocation(engine->program, "uUseTex");
    engine->uTextureLoc = glGetUniformLocation(engine->program, "uTexture");
}

// Matemática de Matrizes 4x4 (Column-Major)
struct Mat4 {
    float m[16];

    static Mat4 Identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 Translate(float tx, float ty, float tz) {
        Mat4 r = Identity();
        r.m[12] = tx; r.m[13] = ty; r.m[14] = tz;
        return r;
    }

    static Mat4 Scale(float sx, float sy, float sz) {
        Mat4 r{};
        r.m[0] = sx; r.m[5] = sy; r.m[10] = sz; r.m[15] = 1.0f;
        return r;
    }

    static Mat4 RotateY(float rad) {
        Mat4 r = Identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] =  c; r.m[8]  = s;
        r.m[2] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 RotateX(float rad) {
        Mat4 r = Identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[5] = c; r.m[9]  = -s;
        r.m[6] = s; r.m[10] =  c;
        return r;
    }

    static Mat4 RotateZ(float rad) {
        Mat4 r = Identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[4] = -s;
        r.m[1] = s; r.m[5] =  c;
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

static void PushQuad3D(std::vector<LitVertex>& v,
                       float x1, float y1, float z1, float u1, float v1,
                       float x2, float y2, float z2, float u2, float v2,
                       float x3, float y3, float z3, float u3, float v3,
                       float x4, float y4, float z4, float u4, float v4,
                       float r, float g, float b, float a = 1.0f) {
    v.push_back({x1, y1, z1, 0.0f, 1.0f, 0.0f, u1, v1, r, g, b, a});
    v.push_back({x2, y2, z2, 0.0f, 1.0f, 0.0f, u2, v2, r, g, b, a});
    v.push_back({x3, y3, z3, 0.0f, 1.0f, 0.0f, u3, v3, r, g, b, a});
    v.push_back({x1, y1, z1, 0.0f, 1.0f, 0.0f, u1, v1, r, g, b, a});
    v.push_back({x3, y3, z3, 0.0f, 1.0f, 0.0f, u3, v3, r, g, b, a});
    v.push_back({x4, y4, z4, 0.0f, 1.0f, 0.0f, u4, v4, r, g, b, a});
}

static void PushBox3D(std::vector<LitVertex>& v,
                      float cx, float cy, float cz,
                      float hx, float hy, float hz,
                      float r, float g, float b, float a = 1.0f) {
    float x0 = cx - hx, x1 = cx + hx;
    float y0 = cy - hy, y1 = cy + hy;
    float z0 = cz - hz, z1 = cz + hz;
    PushQuad3D(v, x0, y1, z0, 0, 0, x1, y1, z0, 1, 0, x1, y1, z1, 1, 1, x0, y1, z1, 0, 1, r * 1.1f, g * 1.1f, b * 1.1f, a);
    PushQuad3D(v, x1, y0, z0, 0, 0, x0, y0, z0, 1, 0, x0, y1, z0, 1, 1, x1, y1, z0, 0, 1, r * 0.90f, g * 0.90f, b * 0.90f, a);
    PushQuad3D(v, x0, y0, z1, 0, 0, x1, y0, z1, 1, 0, x1, y1, z1, 1, 1, x0, y1, z1, 0, 1, r * 0.95f, g * 0.95f, b * 0.95f, a);
    PushQuad3D(v, x0, y0, z0, 0, 0, x0, y0, z1, 1, 0, x0, y1, z1, 1, 1, x0, y1, z0, 0, 1, r * 0.78f, g * 0.78f, b * 0.78f, a);
    PushQuad3D(v, x1, y0, z1, 0, 0, x1, y0, z0, 1, 0, x1, y1, z0, 1, 1, x1, y1, z1, 0, 1, r * 0.78f, g * 0.78f, b * 0.78f, a);
}

static void PushRect2D(std::vector<LitVertex>& v,
                       float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a = 1.0f) {
    PushQuad3D(v, x0, y0, 0.0f, u0, v0, x1, y0, 0.0f, u1, v0, x1, y1, 0.0f, u1, v1, x0, y1, 0.0f, u0, v1, r, g, b, a);
}

static void DrawMeshBuffer(struct EngineState* engine, const std::vector<LitVertex>& verts, const Mat4& modelMat, bool useLit, GLuint textureId = 0) {
    if (verts.empty()) return;
    glUniformMatrix4fv(engine->uModelLoc, 1, GL_FALSE, modelMat.m);
    glUniform1f(engine->uUseLitLoc, useLit ? 1.0f : 0.0f);

    if (textureId != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glUniform1f(engine->uUseTexLoc, 1.0f);
        glUniform1i(engine->uTextureLoc, 0);
    } else {
        glUniform1f(engine->uUseTexLoc, 0.0f);
    }

    glVertexAttribPointer(engine->aPosLoc,    3, GL_FLOAT, GL_FALSE, sizeof(LitVertex), &verts[0].x);
    glVertexAttribPointer(engine->aNormalLoc, 3, GL_FLOAT, GL_FALSE, sizeof(LitVertex), &verts[0].nx);
    glVertexAttribPointer(engine->aUVLoc,     2, GL_FLOAT, GL_FALSE, sizeof(LitVertex), &verts[0].u);
    glVertexAttribPointer(engine->aColorLoc,  4, GL_FLOAT, GL_FALSE, sizeof(LitVertex), &verts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
}

// Renderiza um veículo PS2 real com textura do carro (Vinil Hero / Viatura Policial / Rodas)
static void RenderPS2Vehicle(struct EngineState* engine, const DecodedPS2Mesh& carMesh,
                             float wx, float wy, float wz, float yawRad, float rollRad,
                             float steerRad, float wheelSpinRad, bool isPolice, float animTime, int unitIdx) {
    if (!carMesh.loaded) return;

    Mat4 carTransform = Mat4::Multiply(
        Mat4::Translate(wx, wy, wz),
        Mat4::Multiply(Mat4::RotateY(yawRad), Mat4::RotateZ(rollRad))
    );

    GLuint carTex = isPolice ? engine->texMgr.Get("COPMIDSIZE_SKIN1") : engine->texMgr.Get("BMWM3GTRE46_CE02");
    GLuint tireTex = engine->texMgr.Get("BMWM3GTRE46_TIRE");

    // 1. Renderiza a carroceria/vidros/faróis com a textura do carro
    DrawMeshBuffer(engine, carMesh.bodyVerts, carTransform, true, carTex);

    // 2. Se for viatura policial, pisca o giroflex
    if (isPolice && (!carMesh.sirenLVerts.empty() || !carMesh.sirenRVerts.empty())) {
        bool flash = (static_cast<int>(animTime * 10.0f + unitIdx) % 2) == 0;
        std::vector<LitVertex> sirenVerts;
        sirenVerts.reserve(carMesh.sirenLVerts.size() + carMesh.sirenRVerts.size());
        for (auto v : carMesh.sirenLVerts) {
            v.r = flash ? 1.0f : 0.1f; v.g = 0.05f; v.b = flash ? 0.1f : 1.0f;
            sirenVerts.push_back(v);
        }
        for (auto v : carMesh.sirenRVerts) {
            v.r = flash ? 0.1f : 1.0f; v.g = 0.05f; v.b = flash ? 1.0f : 0.1f;
            sirenVerts.push_back(v);
        }
        DrawMeshBuffer(engine, sirenVerts, carTransform, false, 0);
    }

    // 3. Renderiza as 4 Rodas 3D reais (FRONT_TIRE_B) nos 4 cubos de roda
    if (!carMesh.wheelVerts.empty()) {
        const float wheelX[2] = { -0.76f, +0.76f };
        const float wheelZ[2] = { +1.37f, -1.26f };
        const float wheelY    = 0.335f;

        for (int side = 0; side < 2; ++side) {
            for (int axle = 0; axle < 2; ++axle) {
                float steer = (axle == 0) ? steerRad : 0.0f;
                float mirrorX = (side == 1) ? -1.0f : 1.0f;
                Mat4 wLocal = Mat4::Multiply(
                    Mat4::Translate(wheelX[side], wheelY, wheelZ[axle]),
                    Mat4::Multiply(
                        Mat4::RotateY(steer),
                        Mat4::Multiply(Mat4::RotateX(wheelSpinRad), Mat4::Scale(mirrorX, 1.0f, 1.0f))
                    )
                );
                Mat4 wWorld = Mat4::Multiply(carTransform, wLocal);
                DrawMeshBuffer(engine, carMesh.wheelVerts, wWorld, true, tireTex);
            }
        }
    }
}

static void LoadAllBlackEditionAssets(struct EngineState* engine) {
    if (!engine->app || !engine->app->activity || !engine->app->activity->assetManager) return;
    AAssetManager* mgr = engine->app->activity->assetManager;

    // 1. Carrega todas as texturas reais descompactadas e decodificadas do PS2
    engine->texMgr.LoadPackFromAsset(mgr, "textures/PS2_TEXTURES.BIN");

    // 2. Carrega ZDIR.BIN oficial do Need for Speed: Most Wanted - Black Edition (BR)
    auto zdir = ReadAssetBytes(mgr, "ZDIR.BIN");
    engine->zdirEntries = zdir.size() / 24;

    // 3. Carrega GAMEPLAY.BIN de Rockport
    auto gp = ReadAssetBytes(mgr, "global/GAMEPLAY.BIN");
    engine->gameplayEventsCount = gp.size() / 1024;

    // 4. Decodifica a geometria 3D real dos carros
    engine->m3GtrRace  = LoadPS2Geometry(mgr, "bmwm3gtre46/GEOMETRY.BIN", "", false);
    engine->m3Street   = LoadPS2Geometry(mgr, "bmwm3/GEOMETRY.BIN", "", false);
    engine->copCruiser = LoadPS2Geometry(mgr, "copmidsize/GEOMETRY.BIN", "", true);

    // 5. Decodifica o Cenário 3D Real de Rockport (Record #28: SCENERY_L2RA.BIN)
    engine->sceneryShop    = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XB_SHOPBLOCKC", false);
    engine->sceneryHouse   = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XB_HOUSEE", false);
    engine->sceneryFactory = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XB_IF_OLDFACTORYE", false);
    engine->sceneryTruss   = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XO_FWY_SIGNSUPPORTTRUSS", false);
    engine->sceneryLight   = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XO_STREETLIGHTCB", false);
    engine->sceneryBarrier = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XW_BARRCONCHWY8M", false);
    engine->sceneryTree    = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "XT_CT_FALLTREED", false);
    engine->scenerySky     = LoadPS2Geometry(mgr, "rockport/SCENERY_L2RA.BIN", "SKYDOME", false);
}

static int InitDisplay(struct EngineState* engine) {
    if (!engine->app || !engine->app->window) return -1;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) return -1;
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
        const EGLint fallbackAttribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
        eglChooseConfig(display, fallbackAttribs, &config, 1, &numConfigs);
    }
    if (numConfigs <= 0) return -1;

    EGLint format = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, format);

    EGLint ctxAttribs2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttribs2);

    EGLSurface surface = eglCreateWindowSurface(display, config, engine->app->window, nullptr);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT) return -1;
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) return -1;

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

    if (!engine->m3GtrRace.loaded) {
        LoadAllBlackEditionAssets(engine);
    }

    LOGI("Renderizador EAGL4 -> OpenGL ES pronto (%dx%d) com texturas e cenarios reais da Black Edition (BR)!", w, h);
    return 0;
}

static void DrawFrame(struct EngineState* engine) {
    if (engine->display == EGL_NO_DISPLAY || engine->surface == EGL_NO_SURFACE) return;

    f32 dt = 0.016f;
    engine->animTime += dt;

    // 1. Atualiza Física, Streaming e Perseguição Policial
    engine->car.Update(dt, engine->touchThrottle, engine->touchBrake, engine->touchSteer, false, engine->touchNos);
    Vector3 carPos = engine->car.GetPosition();
    engine->world.Update(carPos);
    engine->pursuit.Update(dt, carPos, engine->car.GetVelocity());

    float speedMps = engine->car.GetSpeedKmh() / 3.6f;
    engine->wheelSpinAngle += (speedMps / 0.34f) * dt;

    // 2. Limpa o Framebuffer com a Cor de Pôr do Sol de Rockport
    glViewport(0, 0, engine->width, engine->height);
    glClearColor(0.82f, 0.58f, 0.30f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(engine->program);
    glEnableVertexAttribArray(engine->aPosLoc);
    glEnableVertexAttribArray(engine->aNormalLoc);
    glEnableVertexAttribArray(engine->aUVLoc);
    glEnableVertexAttribArray(engine->aColorLoc);

    // 3. Configura Câmera de Perseguição em 3ª Pessoa
    float aspect = (engine->height > 0) ? (static_cast<float>(engine->width) / static_cast<float>(engine->height)) : 1.77f;
    bool nosFiring = (engine->touchNos && engine->car.GetNosRemaining() > 1.0f);
    float fovDeg = nosFiring ? 70.0f : 58.0f;
    Mat4 proj = Mat4::Perspective(fovDeg * 3.14159265f / 180.0f, aspect, 0.5f, 320.0f);

    float camX = carPos.x * 0.82f - engine->touchSteer * 0.75f;
    float camY = 1.95f;
    float camZ = carPos.z - 5.5f;
    Mat4 view = Mat4::LookAt(camX, camY, camZ,
                             carPos.x, 0.72f, carPos.z + 10.0f,
                             0.0f, 1.0f, 0.0f);
    Mat4 mvp = Mat4::Multiply(proj, view);
    glUniformMatrix4fv(engine->uMvpLoc, 1, GL_FALSE, mvp.m);

    // 4. Renderiza o Horizonte de Pôr do Sol de Rockport (PAN_COLLEGETOWN_MOUNT_F)
    GLuint skyTex = engine->texMgr.Get("PAN_COLLEGETOWN_MOUNT_F");
    std::vector<LitVertex> skyVerts;
    float skyZ = carPos.z + 240.0f;
    PushQuad3D(skyVerts, -360.0f, 0.0f, skyZ, 0.0f, 1.0f,
                          360.0f, 0.0f, skyZ, 1.0f, 1.0f,
                          360.0f, 95.0f, skyZ, 1.0f, 0.0f,
                         -360.0f, 95.0f, skyZ, 0.0f, 0.0f,
                          1.0f, 1.0f, 1.0f, 1.0f);
    DrawMeshBuffer(engine, skyVerts, Mat4::Identity(), false, skyTex);

    // 5. Renderiza a Pista de Asfalto Real de Rockport (TRNS_ALL_LA_PAVEMENTA_Y)
    GLuint roadTex = engine->texMgr.Get("TRNS_ALL_LA_PAVEMENTA_Y");
    float zBase = std::floor(carPos.z / 10.0f) * 10.0f;
    float zStart = zBase - 50.0f;
    float zEnd = zBase + 210.0f;
    float vReps = (zEnd - zStart) * 0.12f;

    std::vector<LitVertex> roadVerts;
    roadVerts.reserve(256);
    // Terreno ao redor
    PushQuad3D(roadVerts, -140.0f, -0.05f, zStart, 0.0f, 0.0f,
                           140.0f, -0.05f, zStart, 1.0f, 0.0f,
                           140.0f, -0.05f, zEnd,   1.0f, vReps * 0.2f,
                          -140.0f, -0.05f, zEnd,   0.0f, vReps * 0.2f,
                           0.40f, 0.35f, 0.22f);
    // Rodovia de Asfalto Rockport Texturizada
    PushQuad3D(roadVerts, -11.5f, 0.0f, zStart, 0.0f, 0.0f,
                           11.5f, 0.0f, zStart, 1.0f, 0.0f,
                           11.5f, 0.0f, zEnd,   1.0f, vReps,
                          -11.5f, 0.0f, zEnd,   0.0f, vReps,
                           1.0f, 1.0f, 1.0f);
    DrawMeshBuffer(engine, roadVerts, Mat4::Identity(), true, roadTex);

    // 6. Renderiza as Barreiras de Concreto e Setas Neon de Rockport
    GLuint barrierTex = engine->texMgr.Get("OBJ_BARRIERCONCRETE_06_");
    GLuint neonArrowTex = engine->texMgr.Get("SFX_TRACKBARRIERNEONARR");
    std::vector<LitVertex> barrierVerts;
    barrierVerts.reserve(512);

    for (int seg = -4; seg < 21; ++seg) {
        float sz0 = zBase + seg * 10.0f;
        // Barreira esquerda e direita texturizadas
        PushBox3D(barrierVerts, -12.1f, 0.42f, sz0 + 5.0f, 0.32f, 0.42f, 5.0f, 1.0f, 1.0f, 1.0f);
        PushBox3D(barrierVerts,  12.1f, 0.42f, sz0 + 5.0f, 0.32f, 0.42f, 5.0f, 1.0f, 1.0f, 1.0f);
    }
    DrawMeshBuffer(engine, barrierVerts, Mat4::Identity(), true, barrierTex);

    // Setas de Curva Neon Brilhantes (SFX_TRACKBARRIERNEONARR) a cada 40m
    std::vector<LitVertex> neonVerts;
    for (int seg = -4; seg < 21; seg += 4) {
        float sz0 = zBase + seg * 10.0f;
        PushQuad3D(neonVerts, 11.75f, 0.20f, sz0 + 2.0f, 0.0f, 1.0f,
                              11.75f, 0.20f, sz0 + 6.0f, 1.0f, 1.0f,
                              11.75f, 0.85f, sz0 + 6.0f, 1.0f, 0.0f,
                              11.75f, 0.85f, sz0 + 2.0f, 0.0f, 0.0f,
                              1.0f, 0.95f, 0.20f, 1.0f);
    }
    DrawMeshBuffer(engine, neonVerts, Mat4::Identity(), false, neonArrowTex);

    // 7. Renderiza os Prédios e Fábricas de Rockport (3D da Black Edition BR)
    GLuint factoryTex = engine->texMgr.Get("ARC_FACTORYBRICK_01");
    GLuint campusTex  = engine->texMgr.Get("ARC_CAMPUS_HERITAGE4_WA");
    for (int seg = -2; seg < 18; seg += 3) {
        float sz = zBase + seg * 12.0f;
        // Se a malha 3D real da fábrica/prédio de Rockport estiver carregada, posiciona-a
        if (engine->sceneryFactory.loaded) {
            Mat4 bldgL = Mat4::Multiply(Mat4::Translate(-22.0f, 0.0f, sz), Mat4::RotateY(1.57f));
            DrawMeshBuffer(engine, engine->sceneryFactory.bodyVerts, bldgL, true, factoryTex);
        } else {
            std::vector<LitVertex> bldgBox;
            PushBox3D(bldgBox, -22.0f, 8.0f, sz, 5.5f, 8.0f, 5.0f, 1.0f, 1.0f, 1.0f);
            DrawMeshBuffer(engine, bldgBox, Mat4::Identity(), true, factoryTex);
        }

        if (engine->sceneryShop.loaded) {
            Mat4 bldgR = Mat4::Multiply(Mat4::Translate(22.0f, 0.0f, sz), Mat4::RotateY(-1.57f));
            DrawMeshBuffer(engine, engine->sceneryShop.bodyVerts, bldgR, true, campusTex);
        } else {
            std::vector<LitVertex> bldgBox;
            PushBox3D(bldgBox, 22.0f, 9.0f, sz, 5.5f, 9.0f, 5.0f, 1.0f, 1.0f, 1.0f);
            DrawMeshBuffer(engine, bldgBox, Mat4::Identity(), true, campusTex);
        }
    }

    // 8. Treliça Suspensa de Rodovia com Placa "DOWNTOWN ROCKPORT" (a cada 90 metros)
    GLuint signTex = engine->texMgr.Get("SGN_FWY_DOWNTOWNROCKPOR");
    float trussZ = std::floor((carPos.z + 50.0f) / 90.0f) * 90.0f;
    std::vector<LitVertex> trussVerts;
    // Pilares e viga superior
    PushBox3D(trussVerts, -12.2f, 3.8f, trussZ, 0.35f, 3.8f, 0.35f, 0.55f, 0.58f, 0.62f);
    PushBox3D(trussVerts,  12.2f, 3.8f, trussZ, 0.35f, 3.8f, 0.35f, 0.55f, 0.58f, 0.62f);
    PushBox3D(trussVerts,   0.0f, 7.3f, trussZ, 12.5f, 0.35f, 0.35f, 0.55f, 0.58f, 0.62f);
    DrawMeshBuffer(engine, trussVerts, Mat4::Identity(), true, 0);

    // Placa Verde Highway "DOWNTOWN ROCKPORT"
    std::vector<LitVertex> signVerts;
    PushQuad3D(signVerts, -5.5f, 5.2f, trussZ - 0.2f, 0.0f, 1.0f,
                           5.5f, 5.2f, trussZ - 0.2f, 1.0f, 1.0f,
                           5.5f, 7.1f, trussZ - 0.2f, 1.0f, 0.0f,
                          -5.5f, 7.1f, trussZ - 0.2f, 0.0f, 0.0f,
                           1.0f, 1.0f, 1.0f);
    DrawMeshBuffer(engine, signVerts, Mat4::Identity(), false, signTex);

    // 9. Sombra projetada no asfalto e Chamas do Nitro
    std::vector<LitVertex> fxVerts;
    PushQuad3D(fxVerts, carPos.x - 1.05f, 0.02f, carPos.z - 2.25f, 0, 0,
                        carPos.x + 1.05f, 0.02f, carPos.z - 2.25f, 1, 0,
                        carPos.x + 1.05f, 0.02f, carPos.z + 2.35f, 1, 1,
                        carPos.x - 1.05f, 0.02f, carPos.z + 2.35f, 0, 1,
                        0.02f, 0.02f, 0.03f, 0.85f);
    if (nosFiring) {
        float pulse = 0.65f + 0.35f * std::sin(engine->animTime * 45.0f);
        PushBox3D(fxVerts, carPos.x - 0.32f, 0.22f, carPos.z - 2.15f - pulse * 0.65f, 0.09f, 0.07f, pulse * 0.65f, 0.18f, 0.68f, 1.0f);
        PushBox3D(fxVerts, carPos.x + 0.32f, 0.22f, carPos.z - 2.15f - pulse * 0.65f, 0.09f, 0.07f, pulse * 0.65f, 0.18f, 0.68f, 1.0f);
    }
    DrawMeshBuffer(engine, fxVerts, Mat4::Identity(), false, 0);

    // 10. Renderiza a Malha 3D Real PS2 da BMW M3 GTR E46
    const DecodedPS2Mesh& activePlayerCar = (engine->useStreetM3 && engine->m3Street.loaded) ? engine->m3Street : engine->m3GtrRace;
    float carYaw   = engine->touchSteer * 0.14f;
    float carRoll  = -engine->touchSteer * 0.05f;
    float steerRad = engine->touchSteer * 0.42f;
    RenderPS2Vehicle(engine, activePlayerCar, carPos.x, 0.0f, carPos.z,
                     carYaw, carRoll, steerRad, engine->wheelSpinAngle, false, engine->animTime, 0);

    // 11. Renderiza as Viaturas Policiais Crown Victoria Reais
    const auto& cops = engine->pursuit.GetUnits();
    for (size_t i = 0; i < cops.size(); ++i) {
        if (cops[i].state == COP_WRECKED) continue;
        RenderPS2Vehicle(engine, engine->copCruiser, cops[i].position.x, 0.0f, cops[i].position.z,
                         0.0f, 0.0f, 0.0f, engine->wheelSpinAngle, true, engine->animTime, static_cast<int>(i));
    }

    // 12. Renderiza o HUD Autêntico do Need for Speed: Most Wanted
    glDisable(GL_DEPTH_TEST);
    Mat4 hudMvp = Mat4::Identity();
    glUniformMatrix4fv(engine->uMvpLoc, 1, GL_FALSE, hudMvp.m);

    // A) Barra Policial BUSTED / EVADE no Topo Central (PURSUIT_BACKING_TOP)
    GLuint pursuitBarTex = engine->texMgr.Get("PURSUIT_BACKING_TOP");
    std::vector<LitVertex> pbarVerts;
    PushRect2D(pbarVerts, -0.46f, 0.83f, 0.46f, 0.94f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.90f);
    DrawMeshBuffer(engine, pbarVerts, hudMvp, false, pursuitBarTex);

    // Luzes estroboscópicas de perseguição no topo
    bool hudFlash = (static_cast<int>(engine->animTime * 8.0f) % 2) == 0;
    std::vector<LitVertex> strobeVerts;
    PushRect2D(strobeVerts, -0.42f, 0.86f, -0.02f, 0.92f, 0, 0, 1, 1, hudFlash ? 0.95f : 0.20f, 0.10f, 0.10f, 0.85f);
    PushRect2D(strobeVerts,  0.02f, 0.86f,  0.42f, 0.92f, 0, 0, 1, 1, 0.10f, 0.35f, hudFlash ? 0.20f : 0.98f, 0.85f);
    DrawMeshBuffer(engine, strobeVerts, hudMvp, false, 0);

    // B) Ícone de Bounty e Nível de Heat Policial (MAIN_ICON_BOUNTY)
    GLuint bountyTex = engine->texMgr.Get("MAIN_ICON_BOUNTY");
    std::vector<LitVertex> bountyVerts;
    PushRect2D(bountyVerts, -0.95f, 0.82f, -0.84f, 0.95f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.70f, 0.15f, 1.0f);
    DrawMeshBuffer(engine, bountyVerts, hudMvp, false, bountyTex);

    int heat = static_cast<int>(engine->pursuit.GetCurrentHeat());
    std::vector<LitVertex> heatVerts;
    for (int h = 0; h < 5; ++h) {
        float hx0 = -0.82f + h * 0.055f;
        bool active = (h < heat);
        PushRect2D(heatVerts, hx0, 0.85f, hx0 + 0.045f, 0.92f, 0, 0, 1, 1,
                   active ? 1.0f : 0.22f, active ? 0.55f : 0.22f, active ? 0.10f : 0.22f, 1.0f);
    }
    DrawMeshBuffer(engine, heatVerts, hudMvp, false, 0);

    // C) Selo de Autenticidade Black Edition BR no Topo Direito (Alternador M3 Race / Street)
    std::vector<LitVertex> badgeVerts;
    PushRect2D(badgeVerts, 0.58f, 0.83f, 0.95f, 0.94f, 0, 0, 1, 1,
               engine->useStreetM3 ? 0.12f : 0.05f,
               engine->useStreetM3 ? 0.75f : 0.42f,
               engine->useStreetM3 ? 0.30f : 0.92f, 0.90f);
    DrawMeshBuffer(engine, badgeVerts, hudMvp, false, 0);

    // D) Tacômetro e Velocímetro Autêntico do NFS Most Wanted (Inferior Direito)
    // Mostrador Circular com Números de RPM (8000_LINES_00)
    GLuint tachLinesTex = engine->texMgr.Get("8000_LINES_00");
    std::vector<LitVertex> dialVerts;
    PushRect2D(dialVerts, 0.48f, -0.96f, 0.96f, -0.38f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.92f);
    DrawMeshBuffer(engine, dialVerts, hudMvp, false, tachLinesTex);

    // Ponteiro Giratório Laranja de RPM (TACH_NEEDLE_00)
    GLuint needleTex = engine->texMgr.Get("TACH_NEEDLE_00");
    float rpmRatio = std::clamp(engine->car.GetRPM() / 8500.0f, 0.0f, 1.0f);
    // Ângulo do ponteiro: de -140 graus (0 RPM) a +90 graus (8500 RPM)
    float needleAngleRad = (-140.0f + rpmRatio * 230.0f) * 3.14159265f / 180.0f;
    float dialCenterX = 0.72f;
    float dialCenterY = -0.67f;
    float needleLen = 0.22f;
    float nTipX = dialCenterX + std::cos(needleAngleRad) * needleLen;
    float nTipY = dialCenterY + std::sin(needleAngleRad) * needleLen;
    float nPerpX = -std::sin(needleAngleRad) * 0.02f;
    float nPerpY =  std::cos(needleAngleRad) * 0.02f;

    std::vector<LitVertex> needleVerts;
    needleVerts.push_back({dialCenterX - nPerpX, dialCenterY - nPerpY, 0, 0, 1, 0, 0.0f, 1.0f, 1.0f, 0.35f, 0.05f, 1.0f});
    needleVerts.push_back({dialCenterX + nPerpX, dialCenterY + nPerpY, 0, 0, 1, 0, 1.0f, 1.0f, 1.0f, 0.35f, 0.05f, 1.0f});
    needleVerts.push_back({nTipX,                nTipY,                0, 0, 1, 0, 0.5f, 0.0f, 1.0f, 0.15f, 0.02f, 1.0f});
    DrawMeshBuffer(engine, needleVerts, hudMvp, false, needleTex);

    // Barras de Velocidade (KM/H) e Nitro (N2O)
    float speedRatio = std::clamp(engine->car.GetSpeedKmh() / 320.0f, 0.0f, 1.0f);
    float nosRatio   = std::clamp(engine->car.GetNosRemaining() / 100.0f, 0.0f, 1.0f);
    std::vector<LitVertex> barVerts;
    // Fundo das barras
    PushRect2D(barVerts, 0.28f, -0.94f, 0.46f, -0.62f, 0, 0, 1, 1, 0.06f, 0.07f, 0.09f, 0.85f);
    // Barra de Velocidade
    PushRect2D(barVerts, 0.30f, -0.74f, 0.30f + speedRatio * 0.14f, -0.68f, 0, 0, 1, 1,
               0.20f + speedRatio * 0.80f, 0.90f - speedRatio * 0.30f, 0.15f, 1.0f);
    // Barra de Nitro
    PushRect2D(barVerts, 0.30f, -0.86f, 0.30f + nosRatio * 0.14f, -0.80f, 0, 0, 1, 1,
               0.15f, 0.70f, 1.0f, 1.0f);
    DrawMeshBuffer(engine, barVerts, hudMvp, false, 0);

    // E) Botões Virtuais de Direção Esquerda/Direita, Acelerador e Nitro
    std::vector<LitVertex> btnVerts;
    bool steerLeftActive = (engine->touchSteer < -0.15f);
    PushRect2D(btnVerts, -0.94f, -0.88f, -0.64f, -0.48f, 0, 0, 1, 1,
               steerLeftActive ? 0.95f : 0.20f, steerLeftActive ? 0.65f : 0.24f, 0.12f, 0.75f);
    bool steerRightActive = (engine->touchSteer > 0.15f);
    PushRect2D(btnVerts, -0.58f, -0.88f, -0.28f, -0.48f, 0, 0, 1, 1,
               steerRightActive ? 0.95f : 0.20f, steerRightActive ? 0.65f : 0.24f, 0.12f, 0.75f);
    PushRect2D(btnVerts, 0.62f, -0.15f, 0.94f, 0.25f, 0, 0, 1, 1,
               engine->touchNos ? 0.25f : 0.08f, engine->touchNos ? 0.85f : 0.42f, 1.0f, 0.80f);
    bool gasActive = (engine->touchThrottle > 0.6f);
    PushRect2D(btnVerts, 0.62f, -0.54f, 0.94f, -0.20f, 0, 0, 1, 1,
               0.15f, gasActive ? 0.92f : 0.48f, 0.22f, 0.80f);
    DrawMeshBuffer(engine, btnVerts, hudMvp, false, 0);

    eglSwapBuffers(engine->display, engine->surface);
}

static int32_t HandleInput(struct android_app* app, AInputEvent* event) {
    auto* engine = static_cast<struct EngineState*>(app->userData);
    if (!engine || engine->width <= 0 || engine->height <= 0) return 0;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t flags = action & AMOTION_EVENT_ACTION_MASK;
        int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        size_t pointerCount = AMotionEvent_getPointerCount(event);

        if (flags == AMOTION_EVENT_ACTION_DOWN) {
            float tx = AMotionEvent_getX(event, 0);
            float ty = AMotionEvent_getY(event, 0);
            if (tx < engine->width * 0.35f && ty < engine->height * 0.18f) {
                int nextHeat = (static_cast<int>(engine->pursuit.GetCurrentHeat()) % 5) + 1;
                engine->pursuit.SetHeatLevel(static_cast<HeatLevel>(nextHeat));
            }
            if (tx > engine->width * 0.70f && ty < engine->height * 0.18f) {
                engine->useStreetM3 = !engine->useStreetM3;
            }
        }

        f32 newThrottle = 0.50f;
        f32 newSteer = 0.0f;
        bool newNos = false;

        if (flags != AMOTION_EVENT_ACTION_UP && flags != AMOTION_EVENT_ACTION_CANCEL) {
            for (size_t i = 0; i < pointerCount; ++i) {
                if (flags == AMOTION_EVENT_ACTION_POINTER_UP && static_cast<int32_t>(i) == pointerIndex) {
                    continue;
                }
                float x = AMotionEvent_getX(event, i);
                float y = AMotionEvent_getY(event, i);
                if (y < engine->height * 0.18f) continue;

                if (x > engine->width * 0.5f) {
                    newThrottle = 1.0f;
                    if (y < engine->height * 0.58f) {
                        newNos = true;
                    }
                } else {
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

void android_main(struct android_app* state) {
    EngineState engine;
    state->userData = &engine;
    state->onAppCmd = HandleCmd;
    state->onInputEvent = HandleInput;
    engine.app = state;

    LOGI("NFS: Most Wanted - Black Edition (BR) Native Port v1.3.0 iniciado!");

    while (true) {
        int ident;
        int events;
        struct android_poll_source* source = nullptr;

        while ((ident = ALooper_pollOnce(engine.hasFocus ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) {
                source->process(state, source);
            }
            if (state->destroyRequested != 0) {
                return;
            }
        }

        if (engine.hasFocus && engine.display != EGL_NO_DISPLAY) {
            DrawFrame(&engine);
        }
    }
}

#endif
