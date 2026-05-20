// ============================================================
//  Simulasi Peternakan Ayam Petelur  v4  (REFACTORED)
//  OpenGL 3.3 Core Profile | C++17 | CMake
//
//  PERBAIKAN & PENINGKATAN v4:
//   [FIX]  Hardware Instancing ayam diperbaiki (VAO setup benar)
//   [NEW]  Texture loading via stb_image (tanah, kayu, atap, rumput)
//   [NEW]  Directional Shadow Mapping (depth framebuffer + PCF softening)
//   [NEW]  Blinn-Phong lighting model
//   [NEW]  Day/Night cycle (posisi & warna cahaya berubah seiring waktu)
//
//  KONTROL:
//   W/A/S/D          : gerak kamera
//   Q / E            : naik / turun
//   Scroll Mouse     : putar arah pandang
//   F                : beri pakan → ayam bobbing + telur muncul
//   H                : panen → gerobak datang, ambil telur
//   ESC              : keluar
// ============================================================

#include "Mesh.h"
#include "Shapes.h"

// (Tekstur gambar eksternal telah dihapus, beralih ke tekstur prosedural C++)

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ──────────────────────────────────────────────────────────
//  KONSTANTA
// ──────────────────────────────────────────────────────────
constexpr int   SCR_W      = 1280;
constexpr int   SCR_H      = 720;
constexpr int   NUM_AYAM   = 50;
constexpr int   BARIS_AYAM = 25;
constexpr int   KOLOM_AYAM = 1;
constexpr float JARAK_X    = 0.55f;
constexpr float JARAK_Z    = 0.60f;
constexpr float LORONG_W   = 2.4f;
constexpr float OFFSET_SISI = LORONG_W * 0.5f + 0.3f;
constexpr float HALF_W     = OFFSET_SISI + KOLOM_AYAM * JARAK_X + 0.55f;
constexpr float TOTAL_W    = HALF_W * 2.0f;
constexpr float TOTAL_D    = BARIS_AYAM * JARAK_Z + 2.0f;
constexpr int   JML_SEKSI      = 5;
constexpr float TINGGI_DINDING = 2.5f;
constexpr float ROOF_PITCH_DEG = 20.0f;

// Shadow map resolution
constexpr int SHADOW_W = 2048;
constexpr int SHADOW_H = 2048;

// ──────────────────────────────────────────────────────────
//  KAMERA
// ──────────────────────────────────────────────────────────
glm::vec3 cameraPos   = {0.0f, 8.0f, 20.0f};
glm::vec3 cameraFront = {0.0f, -0.30f, -1.0f};
glm::vec3 cameraUp    = {0.0f,  1.0f,  0.0f};
float yaw = -90.0f, pitch = -12.5f;
float deltaTime = 0.0f, lastFrame = 0.0f;

// ──────────────────────────────────────────────────────────
//  STATE
// ──────────────────────────────────────────────────────────
enum class SimState { IDLE, FEEDING, HARVESTING };
SimState simState = SimState::IDLE;

struct EggInfo { bool visible=false; float spawnTime=0.0f; glm::vec3 pos; };
EggInfo eggs[NUM_AYAM];

float ayamBobs[NUM_AYAM]     = {};
float ayamBobPhase[NUM_AYAM] = {};
bool  ayamFeeding[NUM_AYAM]  = {};
float feedStartTime = 0.0f;

struct Peternak {
    glm::vec3 pos      = {0.6f, 0.0f, 3.8f};
    float     rot      = 180.0f;
    float     walkAnim = 0.0f;
    float     bendAngle= 0.0f;
    bool      walking  = false;
    float     targetZ  = 0;
    float     speed    = 3.5f;
    int       phase    = 0;
    float     phaseTimer = 0.0f;
    int       currentChickenIdx = 0;
    int       totalChickenProcessed = 0;
    bool      arrived  = false;
    // Animasi memberi pakan: 0=tidak, 1=lempar kiri, 2=lempar kanan
    int       feedSide    = 0;   // sisi ayam yang sedang diberi pakan
    float     throwAnim   = 0.0f;  // 0..1, animasi lempar pakan
    float     bucketTilt  = 0.0f;  // kemiringan ember/wadah pakan
    bool      scattering  = false; // sedang menaburkan pakan
} peternak;

struct Gerobak {
    glm::vec3 pos        = {0.0f, 0.0f, 4.0f};
    glm::vec3 velocity   = {0.0f, 0.0f, 0.0f}; // kecepatan fisika dorong
    float     heading    = 180.0f;              // arah hadap gerobak (derajat Y)
    bool      active     = false;
    bool      returning  = false;
    float     speed      = 4.5f;
    float     wheelAngle = 0.0f;
    float     wheelVel   = 0.0f;    // kecepatan sudut roda saat ini (derajat/s)
    int       eggsHarvested = 0;
    float     targetZ    = 0.0f;
    // Fisika dorong
    bool      beingPushed   = false; // tangan peternak sedang kontak
    float     bumpPhase     = 0.0f;  // fase guncangan lantai
    float     lateralWobble = 0.0f;  // goyang ke samping
    // Animasi telur terbang ke gerobak
    bool      eggFlying  = false;
    glm::vec3 eggFlyFrom = {};
    float     eggFlyT    = 0.0f;
    float     eggFlyDur  = 0.35f;
} gerobak;

// State animasi panen telur
struct HarvestAnim {
    float bendAngle  = 0.0f;   // badan membungkuk (derajat, ke depan)
    float kneeBend   = 0.0f;   // lutut tekuk saat jongkok
    float liftAnim   = 0.0f;   // 0..1 — tangan terangkat (sedang bawa telur ke gerobak)
    bool  carryingEgg= false;  // apakah sedang membawa telur
    int   subPhase   = 0;      // 0=jalan, 1=jongkok turun, 2=ambil, 3=berdiri, 4=jalan ke gerobak, 5=taruh
    float subTimer   = 0.0f;
} harvestAnim;

bool harvestDone = false;   // flag: proses panen telah selesai

glm::vec3 ayamBasePos[NUM_AYAM];

// ──────────────────────────────────────────────────────────
//  PROCEDURAL TEXTURE GENERATOR
// ──────────────────────────────────────────────────────────
static unsigned int makeProceduralTexture(int type, unsigned char r, unsigned char g, unsigned char b) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    const int size = 128;
    std::vector<unsigned char> data(size * size * 3);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int idx = (y * size + x) * 3;
            float noise = ((rand() % 100) / 100.0f) * 0.3f + 0.7f; // 0.7 - 1.0

            unsigned char cr = r, cg = g, cb = b;

            if (type == 1) { // 1 = Rumput / Tanah (Noise pattern)
                cr = (unsigned char)(r * noise);
                cg = (unsigned char)(g * noise);
                cb = (unsigned char)(b * noise);
            } else if (type == 2) { // 2 = Kayu (Wood lines pattern)
                float line = sinf((x + (rand()%5)) * 0.2f) * 0.15f + 0.85f;
                cr = (unsigned char)(r * line);
                cg = (unsigned char)(g * line);
                cb = (unsigned char)(b * line);
            } else if (type == 3) { // 3 = Genteng (Roof tiles pattern)
                bool edge = (x % 32 < 2) || (y % 32 < 2);
                float dark = edge ? 0.6f : (1.0f - (y%32)*0.015f);
                cr = (unsigned char)(r * dark);
                cg = (unsigned char)(g * dark);
                cb = (unsigned char)(b * dark);
            }

            data[idx]     = cr;
            data[idx + 1] = cg;
            data[idx + 2] = cb;
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

// ──────────────────────────────────────────────────────────
//  SHADOW MAP FRAMEBUFFER
// ──────────────────────────────────────────────────────────
struct ShadowMap {
    unsigned int fbo      = 0;
    unsigned int depthTex = 0;

    void init() {
        glGenFramebuffers(1, &fbo);

        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                     SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // Clamp to border = no shadow luar frustum
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
        // Tidak ada color attachment — hanya depth
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "[FBO] Shadow framebuffer NOT complete!\n";

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
} shadowMap;

// ──────────────────────────────────────────────────────────
//  SHADERS SOURCE
// ──────────────────────────────────────────────────────────

// ── Depth-only pass (shadow map generation) ──────────────
static const char* VS_DEPTH = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)glsl";

static const char* FS_DEPTH = R"glsl(
#version 330 core
void main() { /* depth auto-written */ }
)glsl";

// ── Depth-only pass untuk INSTANCED (ayam) ───────────────
static const char* VS_DEPTH_INST = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
// Per-instance
layout(location=3) in vec3  iOff;   // offset posisi dunia
layout(location=4) in float iBob;   // bob offset (Y)
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
void main() {
    float angle = (iOff.x < 0.0) ? radians(90.0) : radians(-90.0);
    float c = cos(angle); float s = sin(angle);
    mat3 rot = mat3(c, 0.0, s,  0.0, 1.0, 0.0,  -s, 0.0, c);
    
    vec3 localPos = rot * (model * vec4(aPos, 1.0)).xyz;
    vec3 worldPos = localPos + iOff + vec3(0.0, iBob, 0.0);
    gl_Position = lightSpaceMatrix * vec4(worldPos, 1.0);
}
)glsl";

// ── Standard lit shader (Blinn-Phong + tekstur + bayangan) ─
static const char* VS_STD = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

out vec3  fragPos;
out vec3  fragNorm;
out vec2  fragUV;
out vec4  fragPosLS;   // posisi dalam light space (untuk shadow)

void main() {
    vec4 worldPos  = model * vec4(aPos, 1.0);
    fragPos        = worldPos.xyz;
    fragNorm       = mat3(transpose(inverse(model))) * aNorm;
    fragUV         = aUV;
    fragPosLS      = lightSpaceMatrix * worldPos;
    gl_Position    = projection * view * worldPos;
}
)glsl";

static const char* FS_STD = R"glsl(
#version 330 core
in vec3  fragPos;
in vec3  fragNorm;
in vec2  fragUV;
in vec4  fragPosLS;

out vec4 FragColor;

uniform sampler2D texDiffuse;
uniform sampler2D shadowMap;
uniform int       useTexture;

uniform vec3  objectColor;
uniform vec3  lightDir;
uniform vec3  lightColor;
uniform vec3  viewPos;
uniform float ambientStr;
uniform float shadowStrength;

// ── Multi Point Lights ──────────────────────────────────────
#define MAX_POINT_LIGHTS 8
struct PointLight {
    vec3  pos;
    vec3  color;
    float isOn;   // 1.0 = nyala, 0.0 = mati
};
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform float      nightBlend;   // 0=siang, 1=malam (untuk boost lampu)

// PCF Shadow — 3×3 kernel
float shadowCalc(vec4 posLS, float bias) {
    vec3 proj = posLS.xyz / posLS.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(shadowMap, proj.xy + vec2(x,y)*texelSize).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

vec3 calcPointLight(PointLight pl, vec3 N, vec3 V, vec3 baseColor) {
    if (pl.isOn < 0.5) return vec3(0.0);
    vec3  toLight = pl.pos - fragPos;
    float dist    = length(toLight);
    vec3  L       = toLight / dist;

    // Attenuation (konstanta Ogre/OpenGL cookbook, jangkauan ~10 m)
    float Kc = 1.0, Kl = 0.22, Kq = 0.20;
    float atten = 1.0 / (Kc + Kl * dist + Kq * dist * dist);

    float diff  = max(dot(N, L), 0.0);
    vec3  H     = normalize(L + V);
    float spec  = pow(max(dot(N, H), 0.0), 48.0);

    vec3 diffuse  = diff  * pl.color * baseColor * atten;
    vec3 specular = spec  * pl.color * 0.25       * atten;
    return (diffuse + specular) * (1.0 + nightBlend * 0.8);
}

void main() {
    vec3 baseColor = (useTexture == 1)
        ? texture(texDiffuse, fragUV).rgb : objectColor;

    vec3  N = normalize(fragNorm);
    vec3  L = normalize(-lightDir);
    vec3  V = normalize(viewPos - fragPos);
    vec3  H = normalize(L + V);

    vec3  ambient  = ambientStr * lightColor * baseColor;
    float diff     = max(dot(N, L), 0.0);
    vec3  diffuse  = diff * lightColor * baseColor;
    float spec     = pow(max(dot(N, H), 0.0), 64.0);
    vec3  specular = spec * lightColor * 0.15;

    float bias   = max(0.005 * (1.0 - dot(N, L)), 0.0005);
    float shadow = shadowCalc(fragPosLS, bias) * shadowStrength;

    vec3 totalPoint = vec3(0.0);
    for (int i = 0; i < MAX_POINT_LIGHTS; i++)
        totalPoint += calcPointLight(pointLights[i], N, V, baseColor);

    vec3 result = ambient
                + (1.0 - shadow) * (diffuse + specular)
                + totalPoint;
    FragColor = vec4(result, 1.0);
}
)glsl";

// ── Instanced shader (Blinn-Phong + bayangan, tanpa per-mesh textur) ─
// Catatan: tekstur ayam bisa ditambahkan nanti; untuk sekarang pakai objectColor.
static const char* VS_INST = R"glsl(
#version 330 core
layout(location=0) in vec3  aPos;
layout(location=1) in vec3  aNorm;
layout(location=2) in vec2  aUV;
// Per-instance attributes (divisor=1)
layout(location=3) in vec3  iOff;   // world-space offset (X,Y,Z base position)
layout(location=4) in float iBob;   // bob displacement (Y)

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

out vec3  fragPos;
out vec3  fragNorm;
out vec4  fragPosLS;
out float bobVal;

void main() {
    float angle = (iOff.x < 0.0) ? radians(90.0) : radians(-90.0);
    float c = cos(angle); float s = sin(angle);
    mat3 rot = mat3(c, 0.0, s,  0.0, 1.0, 0.0,  -s, 0.0, c);
    
    vec3 localPos  = rot * (model * vec4(aPos, 1.0)).xyz;
    vec3 worldPos  = localPos + iOff + vec3(0.0, iBob, 0.0);
    fragPos        = worldPos;
    fragNorm       = rot * mat3(transpose(inverse(model))) * aNorm;
    fragPosLS      = lightSpaceMatrix * vec4(worldPos, 1.0);
    bobVal         = iBob;
    gl_Position    = projection * view * vec4(worldPos, 1.0);
}
)glsl";

static const char* FS_INST = R"glsl(
#version 330 core
in vec3  fragPos;
in vec3  fragNorm;
in vec4  fragPosLS;
in float bobVal;

out vec4 FragColor;

uniform sampler2D shadowMap;
uniform vec3  objectColor;
uniform vec3  lightDir;
uniform vec3  lightColor;
uniform vec3  viewPos;
uniform float ambientStr;
uniform float shadowStrength;

#define MAX_POINT_LIGHTS 8
struct PointLight {
    vec3  pos;
    vec3  color;
    float isOn;
};
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform float      nightBlend;

float shadowCalc(vec4 posLS, float bias) {
    vec3 proj = posLS.xyz / posLS.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(shadowMap, proj.xy + vec2(x,y)*texelSize).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

vec3 calcPointLight(PointLight pl, vec3 N, vec3 V, vec3 baseColor) {
    if (pl.isOn < 0.5) return vec3(0.0);
    vec3  toLight = pl.pos - fragPos;
    float dist    = length(toLight);
    vec3  L       = toLight / dist;
    float Kc=1.0, Kl=0.22, Kq=0.20;
    float atten = 1.0 / (Kc + Kl*dist + Kq*dist*dist);
    float diff  = max(dot(N, L), 0.0);
    vec3  H     = normalize(L + V);
    float spec  = pow(max(dot(N, H), 0.0), 48.0);
    return (diff * pl.color * baseColor + spec * pl.color * 0.25) * atten
           * (1.0 + nightBlend * 0.8);
}

void main() {
    vec3 baseColor = objectColor;
    if (bobVal > 0.003) baseColor = mix(baseColor, vec3(1.0, 0.55, 0.10), 0.4);

    vec3 N = normalize(fragNorm);
    vec3 L = normalize(-lightDir);
    vec3 V = normalize(viewPos - fragPos);
    vec3 H = normalize(L + V);

    vec3  ambient  = ambientStr * lightColor * baseColor;
    float diff     = max(dot(N, L), 0.0);
    vec3  diffuse  = diff  * lightColor * baseColor;
    float spec     = pow(max(dot(N, H), 0.0), 32.0);
    vec3  specular = spec  * lightColor * 0.10;

    float bias   = max(0.005 * (1.0 - dot(N, L)), 0.0005);
    float shadow = shadowCalc(fragPosLS, bias) * shadowStrength;

    vec3 totalPoint = vec3(0.0);
    for (int i = 0; i < MAX_POINT_LIGHTS; i++)
        totalPoint += calcPointLight(pointLights[i], N, V, baseColor);

    FragColor = vec4(ambient + (1.0 - shadow) * (diffuse + specular) + totalPoint, 1.0);
}
)glsl";

// ──────────────────────────────────────────────────────────
//  SHADER HELPERS
// ──────────────────────────────────────────────────────────
static unsigned int compileShader(GLenum type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048]; glGetShaderInfoLog(s, 2048, nullptr, buf);
        std::cerr << "SHADER ERROR:\n" << buf << "\n";
    }
    return s;
}
static unsigned int makeProgram(const char* vs, const char* fs) {
    unsigned int v = compileShader(GL_VERTEX_SHADER, vs);
    unsigned int f = compileShader(GL_FRAGMENT_SHADER, fs);
    unsigned int p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    int ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[2048]; glGetProgramInfoLog(p, 2048, nullptr, buf); std::cerr << buf; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

static inline void setMat4 (unsigned int p, const char* n, const glm::mat4& m)
    { glUniformMatrix4fv(glGetUniformLocation(p,n), 1, GL_FALSE, glm::value_ptr(m)); }
static inline void setVec3 (unsigned int p, const char* n, const glm::vec3& v)
    { glUniform3fv(glGetUniformLocation(p,n), 1, glm::value_ptr(v)); }
static inline void setFloat(unsigned int p, const char* n, float v)
    { glUniform1f(glGetUniformLocation(p,n), v); }
static inline void setInt  (unsigned int p, const char* n, int v)
    { glUniform1i(glGetUniformLocation(p,n), v); }

// ──────────────────────────────────────────────────────────
//  DRAW HELPERS
// ──────────────────────────────────────────────────────────
// draw() untuk pass normal (dengan tekstur & bayangan)
static void draw(Mesh& m, unsigned int prog,
                 glm::mat4 M, glm::vec3 col,
                 unsigned int texID = 0)
{
    setMat4(prog, "model", M);
    setVec3(prog, "objectColor", col);
    if (texID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texID);
        setInt(prog, "texDiffuse", 0);
        setInt(prog, "useTexture", 1);
    } else {
        setInt(prog, "useTexture", 0);
    }
    glBindVertexArray(m.VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)m.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// draw() untuk depth pass (shadow) — hanya butuh model matrix
static void drawDepth(Mesh& m, unsigned int prog, glm::mat4 M) {
    setMat4(prog, "model", M);
    glBindVertexArray(m.VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)m.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

static glm::mat4 TRS(glm::vec3 t, glm::vec3 s={1,1,1},
                     float rx=0, float ry=0, float rz=0)
{
    glm::mat4 M(1);
    M = glm::translate(M, t);
    if (ry) M = glm::rotate(M, glm::radians(ry), {0,1,0});
    if (rx) M = glm::rotate(M, glm::radians(rx), {1,0,0});
    if (rz) M = glm::rotate(M, glm::radians(rz), {0,0,1});
    return glm::scale(M, s);
}

// ──────────────────────────────────────────────────────────
//  POSISI AYAM
// ──────────────────────────────────────────────────────────
void initAyamPositions() {
    int idx = 0;
    for (int b = 0; b < BARIS_AYAM && idx < NUM_AYAM; b++) {
        float z = -(b * JARAK_Z + 1.0f);
        for (int k = 0; k < KOLOM_AYAM && idx < NUM_AYAM; k++) {
            ayamBasePos[idx++] = { -(OFFSET_SISI + k * JARAK_X), 0.22f, z };
            if (idx < NUM_AYAM)
                ayamBasePos[idx++] = { OFFSET_SISI + k * JARAK_X, 0.22f, z };
        }
    }
}

// ──────────────────────────────────────────────────────────
//  INSTANCE BUFFER SETUP
//
//  BUG ROOT CAUSE (diperbaiki):
//  Sebelumnya vboOff/vboBob di-setup HANYA pada mAyam.VAO (bodi ayam).
//  Tapi mAyamKep, mAyamEkor, mCyl (kaki) punya VAO sendiri yang berbeda —
//  tidak pernah dapat attrib location 3 & 4. Akibatnya glDrawElementsInstanced
//  membaca garbage → kotak putih melayang.
//
//  SOLUSI: Buat satu fungsi yang mem-bind instance attribs ke VAO APAPUN.
// ──────────────────────────────────────────────────────────
static void attachInstanceBuffers(unsigned int vao,
                                  unsigned int vboOff,
                                  unsigned int vboBob)
{
    glBindVertexArray(vao);

    // Attrib 3 → iOff (vec3, per-instance)
    glBindBuffer(GL_ARRAY_BUFFER, vboOff);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glVertexAttribDivisor(3, 1);  // ← per-instance!

    // Attrib 4 → iBob (float, per-instance)
    glBindBuffer(GL_ARRAY_BUFFER, vboBob);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(4, 1);  // ← per-instance!

    glBindVertexArray(0);
}

// ──────────────────────────────────────────────────────────
//  CALLBACKS
// ──────────────────────────────────────────────────────────
void key_callback(GLFWwindow* win, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    float now = (float)glfwGetTime();
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, true);

    if (key == GLFW_KEY_F && simState == SimState::IDLE) {
        simState      = SimState::FEEDING;
        feedStartTime = now;
        for (int i = 0; i < NUM_AYAM; i++) {
            ayamFeeding[i]  = false;
            ayamBobPhase[i] = ((float)rand() / RAND_MAX) * 6.28f;
            ayamBobs[i]     = 0.0f;
            eggs[i].visible   = false;
            eggs[i].spawnTime = now + 0.5f;
        }
        peternak.pos        = {0.0f, 0.0f, 4.0f};
        peternak.rot        = 180.0f;
        peternak.walkAnim   = 0.0f;
        peternak.bendAngle  = 0.0f;
        peternak.walking    = true;
        peternak.speed      = 4.5f;
        peternak.phase      = 100;   // mulai dari baris 0
        peternak.phaseTimer = now;
        peternak.currentChickenIdx = 0;
        peternak.totalChickenProcessed = 0;
        peternak.arrived    = false;
        peternak.scattering = false;
        peternak.feedSide   = 0;
        peternak.throwAnim  = 0.0f;
    }

    if (key == GLFW_KEY_H && simState == SimState::IDLE) {
        simState    = SimState::HARVESTING;
        harvestDone = false;
        gerobak.pos          = peternak.pos + glm::vec3{-0.85f, 0.0f, 0.6f};
        gerobak.velocity     = {0.0f, 0.0f, 0.0f};
        gerobak.heading      = 180.0f;
        gerobak.active       = true;
        gerobak.returning    = false;
        gerobak.targetZ      = -(TOTAL_D - 1.5f);
        gerobak.speed        = 4.5f;
        gerobak.wheelAngle   = 0.0f;
        gerobak.wheelVel     = 0.0f;
        gerobak.beingPushed  = false;
        gerobak.bumpPhase    = 0.0f;
        gerobak.lateralWobble= 0.0f;
        gerobak.eggsHarvested = 0;
        peternak.pos        = {0.6f, 0.0f, 4.0f};
        peternak.rot        = 180.0f;
        peternak.walkAnim   = 0.0f;
        peternak.bendAngle  = 0.0f;
        peternak.walking    = true;
        peternak.speed      = 5.0f;
        peternak.phase      = 200;
        peternak.phaseTimer = now;
        peternak.currentChickenIdx = 0;
        peternak.totalChickenProcessed = 0;
        peternak.arrived    = false;
    }
}

void scroll_callback(GLFWwindow*, double xoff, double yoff) {
    float s = 2.5f;
    yaw   += (float)xoff * s;
    pitch  = glm::clamp(pitch + (float)yoff * s, -89.0f, 89.0f);
    cameraFront = glm::normalize(glm::vec3{
        cosf(glm::radians(yaw)) * cosf(glm::radians(pitch)),
        sinf(glm::radians(pitch)),
        sinf(glm::radians(yaw)) * cosf(glm::radians(pitch))
    });
}

void processInput(GLFWwindow* w) {
    float spd = 10.0f * deltaTime;
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(w, GLFW_KEY_W)==GLFW_PRESS) cameraPos += spd * cameraFront;
    if (glfwGetKey(w, GLFW_KEY_S)==GLFW_PRESS) cameraPos -= spd * cameraFront;
    if (glfwGetKey(w, GLFW_KEY_A)==GLFW_PRESS) cameraPos -= spd * right;
    if (glfwGetKey(w, GLFW_KEY_D)==GLFW_PRESS) cameraPos += spd * right;
    if (glfwGetKey(w, GLFW_KEY_Q)==GLFW_PRESS) cameraPos.y += spd;
    if (glfwGetKey(w, GLFW_KEY_E)==GLFW_PRESS) cameraPos.y -= spd;
}

// ──────────────────────────────────────────────────────────
//  DAY/NIGHT CYCLE
// ──────────────────────────────────────────────────────────
struct DayNight {
    glm::vec3 lightDir;
    glm::vec3 lightColor;
    glm::vec3 skyColor;
    float     ambientStr;
    float     shadowStrength;

    void update(float time) {
        // Siklus 120 detik per hari
        float dayAngle = glm::radians(time / 120.0f * 360.0f);
        // Posisi matahari bergerak di bidang XY (dari timur ke barat)
        float sunX =  cosf(dayAngle);
        float sunY = -sinf(dayAngle);         // positif saat siang (Y bawah)
        lightDir   = glm::normalize(glm::vec3(sunX, sunY, -0.4f));

        // Siang (sunY < 0 → matahari di atas)
        float dayness = glm::clamp(-sunY, 0.0f, 1.0f);   // 1=siang, 0=malam
        float dusk    = 1.0f - fabsf(sunY);               // puncak saat matahari di horizon

        // Warna langit
        glm::vec3 skyDay  = {0.52f, 0.78f, 0.92f};
        glm::vec3 skyDusk = {0.85f, 0.42f, 0.22f};
        glm::vec3 skyNight= {0.04f, 0.05f, 0.12f};
        skyColor = glm::mix(skyNight,
                   glm::mix(skyDusk, skyDay, glm::smoothstep(0.2f, 0.6f, dayness)),
                   glm::clamp(dayness * 1.5f, 0.0f, 1.0f));

        // Warna cahaya matahari
        glm::vec3 lcDay  = {1.00f, 0.97f, 0.90f};
        glm::vec3 lcDusk = {1.00f, 0.55f, 0.20f};
        glm::vec3 lcNight= {0.20f, 0.22f, 0.35f};
        lightColor = glm::mix(lcNight,
                     glm::mix(lcDusk, lcDay, glm::smoothstep(0.2f, 0.5f, dayness)),
                     glm::clamp(dayness * 1.8f, 0.0f, 1.0f));

        ambientStr     = glm::mix(0.12f, 0.42f, dayness);
        shadowStrength = glm::mix(0.0f,  0.75f, dayness);
    }
} dayNight;

// ──────────────────────────────────────────────────────────
//  GAMBAR PETERNAK — Realistis dengan animasi memberi pakan
//
//  throwAnim   : 0..1  siklus animasi lempar/tabur pakan
//  feedSide    : -1 = kiri, +1 = kanan, 0 = tidak
//  kneeBend    : 0..45 lutut tekuk saat jongkok (panen)
//  liftAnim    : 0..1  kedua tangan terangkat membawa telur
//  carryingEgg : true = gambar telur di tangan
// ──────────────────────────────────────────────────────────
static void drawPeternak(unsigned int prog,
    Mesh& mBox, Mesh& mCyl, Mesh& mSphere,
    glm::vec3 pos, float rotY, float walkAnim, float bendDeg,
    glm::vec3 cBaju, glm::vec3 cKulit, glm::vec3 cCelana,
    float throwAnim = 0.0f, int feedSide = 0, bool scattering = false,
    float kneeBend = 0.0f, float liftAnim = 0.0f, bool carryingEgg = false)
{
    // ── Warna pelengkap ──
    glm::vec3 cSepatu = {0.12f, 0.08f, 0.04f};
    glm::vec3 cSol    = {0.07f, 0.05f, 0.03f};
    glm::vec3 cTopi   = {0.80f, 0.65f, 0.28f};
    glm::vec3 cTopiBrim = {0.68f, 0.52f, 0.18f};
    glm::vec3 cEmber  = {0.72f, 0.42f, 0.14f};   // warna ember/wadah pakan kayu
    glm::vec3 cEmberRim = {0.38f, 0.22f, 0.06f};
    glm::vec3 cPakan  = {0.92f, 0.80f, 0.38f};   // warna butiran pakan

    // ── Bob & sway tubuh saat berjalan ──
    float bobY     = sinf(walkAnim * 2.0f) * 0.030f;
    float sideRock = sinf(walkAnim) * 1.8f;
    
    // Badan condong ke depan sedikit saat memberi pakan
    float feedLean = scattering ? 8.0f : 0.0f;

    glm::mat4 root = glm::translate(glm::mat4(1), pos + glm::vec3(0, bobY, 0));
    root = glm::rotate(root, glm::radians(rotY), {0,1,0});
    root = glm::rotate(root, glm::radians(sideRock), {0,0,1});

    float legSw    = sinf(walkAnim) * 36.0f;
    kneeBend = fabsf(sinf(walkAnim)) * 20.0f;
    float armSwBase = sinf(walkAnim + 3.14159f) * 28.0f;
    float bendRad  = glm::radians(bendDeg + feedLean);

    // ════════════════════════════════
    //  KAKI — 2 sendi (paha + betis)
    // ════════════════════════════════
    for (int side : {-1, 1}) {
        float sw   = (float)side * (-legSw);
        float knee = kneeBend;  // lutut selalu tekuk simetris

        // Sendi panggul
        glm::mat4 hipJoint = root * glm::translate(glm::mat4(1), {side * 0.105f, 0.56f, 0.0f});
        hipJoint = glm::rotate(hipJoint, glm::radians(sw), {1,0,0});
        // Sendi bola pinggul
        draw(mSphere, prog, hipJoint * TRS({0,0,0}, {0.13f,0.13f,0.13f}), cCelana);
        // Paha atas
        draw(mCyl, prog, hipJoint * TRS({0,-0.14f,0}, {0.13f,0.28f,0.13f}), cCelana);

        // Sendi lutut
        glm::mat4 kneeJoint = hipJoint * glm::translate(glm::mat4(1), {0,-0.28f,0.0f});
        kneeJoint = glm::rotate(kneeJoint, glm::radians(-knee), {1,0,0});
        draw(mSphere, prog, kneeJoint * TRS({0,0,0}, {0.11f,0.11f,0.11f}), cCelana);
        // Betis (warna berbeda — celana berakhir di lutut, betis terlihat celana juga)
        draw(mCyl, prog, kneeJoint * TRS({0,-0.13f,0}, {0.11f,0.26f,0.11f}), cCelana);

        // Pergelangan kaki
        glm::mat4 ankle = kneeJoint * glm::translate(glm::mat4(1), {0,-0.26f,0});
        float footPitch = -sw * 0.28f + 7.0f;
        ankle = glm::rotate(ankle, glm::radians(footPitch), {1,0,0});
        // Sepatu — lebih nyata: badan sepatu + sol tebal + ujung sedikit membulat
        draw(mSphere, prog, ankle * TRS({0, 0.02f, 0.06f}, {0.11f,0.09f,0.18f}), cSepatu);
        draw(mBox,   prog, ankle * TRS({0,-0.04f, 0.06f}, {0.10f,0.04f,0.20f}), cSol);
        // Lidah sepatu (bagian depan)
        draw(mBox,   prog, ankle * TRS({0, 0.05f, 0.12f}, {0.08f,0.05f,0.06f}),
             {cSepatu.r*1.15f, cSepatu.g*1.1f, cSepatu.b});
    }

    // ════════════════════════════════
    //  TORSO — silinder + bahu lebih lebar
    // ════════════════════════════════
    glm::mat4 torso = root * glm::translate(glm::mat4(1), {0, 1.00f, 0});
    torso = glm::rotate(torso, bendRad, {1,0,0});
    float torsoTwist = -legSw * 0.09f;
    torso = glm::rotate(torso, glm::radians(torsoTwist), {0,1,0});

    // Pinggang & punggung
    draw(mCyl, prog, torso * TRS({0,-0.04f,0}, {0.30f,0.48f,0.22f}), cBaju);
    // Dada — sedikit lebih lebar dan menonjol ke depan
    draw(mSphere, prog, torso * TRS({0, 0.16f, 0.03f}, {0.30f,0.18f,0.19f}), cBaju);
    // Garis kemeja / detail baju (strip tengah sedikit lebih terang)
    draw(mBox, prog, torso * TRS({0, 0.05f, 0.12f}, {0.06f,0.36f,0.02f}),
         {cBaju.r*1.08f, cBaju.g*1.06f, cBaju.b*1.04f});
    // Sabuk
    glm::vec3 cSabuk = {0.25f, 0.15f, 0.06f};
    draw(mCyl, prog, torso * TRS({0,-0.18f,0}, {0.31f,0.055f,0.24f}), cSabuk);
    // Gesper sabuk
    draw(mBox, prog, torso * TRS({0,-0.18f,0.135f}, {0.045f,0.045f,0.02f}),
         {0.65f,0.55f,0.20f});

    // ════════════════════════════════
    //  LENGAN — animasi beda untuk tangan yang membawa ember vs yang menabur
    // ════════════════════════════════
    // Tangan yang membawa ember = sisi yang berlawanan dengan feedSide
    // (ember dibawa di satu tangan, tangan lain menabur)
    // feedSide: -1=kiri, +1=kanan — tangan yang MENABUR
    // Tangan pembawa ember = sisi yang LAIN

    // ── Tentukan sisi tangan panen (tangan kanan = side +1 selalu yang ambil telur) ──
    // liftAnim: 0=tangan di bawah (jangkau), 0.45=sudah pegang, 0.9=terangkat tinggi, 0→taruh
    // carryingEgg: true saat membawa telur ke gerobak

    // Animasi panen: sisi kanan (+1) adalah tangan yang mengambil dan membawa telur.
    // Sisi kiri (-1) membantu menyeimbangkan badan.

    for (int side : {-1, 1}) {
        bool isScatterArm = (side == feedSide) && scattering;
        bool isBucketArm  = (side == -feedSide) && scattering;

        // Apakah ini tangan panen (tangan utama ambil telur) — sisi kanan
        bool isHarvestArm  = (liftAnim > 0.0f || carryingEgg) && (side == 1);
        // Tangan kiri saat panen: menyeimbangkan
        bool isBalanceArm  = (liftAnim > 0.0f || carryingEgg) && (side == -1);

        float armSw = isHarvestArm || isBalanceArm ? 0.0f : armSwBase * (float)side;

        float scatterPitch = 0.0f;
        float scatterRoll  = 0.0f;
        float elbowExtra   = 0.0f;
        float wristRoll    = 0.0f;   // rotasi pergelangan (buka/tutup telapak)
        float wristPitch   = 0.0f;   // pergelangan menunduk ke bawah (saat jangkau)

        if (isScatterArm) {
            float t = throwAnim;
            float arc = sinf(t * 3.14159f);
            scatterPitch = -55.0f * arc - 15.0f;
            scatterRoll  = (float)side * 25.0f * arc;
            elbowExtra   = 30.0f * arc;
            armSw = 0.0f;
        } else if (isBucketArm) {
            scatterPitch = 20.0f;
            elbowExtra   = 55.0f;
            armSw = 0.0f;
        } else if (isHarvestArm) {
            // ── TANGAN KANAN: animasi panen realistis ──
            // liftAnim: 0 = tangan lurus ke bawah menjangkau telur
            //           0.45 = sudah menggenggam telur (siku tekuk naik)
            //           0.90 = terangkat penuh membawa telur ke depan dada
            //           saat taruh: turun lagi ke 0.10 lalu 0.0

            if (liftAnim < 0.45f) {
                // Fase jangkau: lengan maju ke depan-bawah
                float t = liftAnim / 0.45f;               // 0..1
                float easeT = t * t;                       // ease-in
                scatterPitch  = glm::mix(-15.0f, -65.0f, easeT); // maju ke depan & bawah
                elbowExtra    = glm::mix(10.0f, 70.0f, easeT);   // siku tekuk semakin dalam
                wristPitch    = glm::mix(0.0f, 35.0f, easeT);    // pergelangan menunduk ke bawah
                scatterRoll   = glm::mix(0.0f, 15.0f, easeT);    // sedikit ke dalam (ke arah telur)
            } else if (liftAnim < 0.90f) {
                // Fase angkat: lengan naik sambil siku tetap tekuk (membawa telur)
                float t = (liftAnim - 0.45f) / 0.45f;    // 0..1
                float easeT = 1.0f - (1.0f - t)*(1.0f - t); // ease-out
                scatterPitch  = glm::mix(-65.0f, -85.0f, easeT); // terangkat ke depan-atas
                elbowExtra    = glm::mix(70.0f, 55.0f, easeT);   // siku sedikit melonggar
                wristPitch    = glm::mix(35.0f, -10.0f, easeT);  // pergelangan naik (genggam kuat)
                scatterRoll   = glm::mix(15.0f, 5.0f, easeT);
            } else {
                // Fase bawa / taruh: lengan depan-atas stabil
                scatterPitch = -85.0f;
                elbowExtra   = 55.0f;
                wristPitch   = -10.0f;
                scatterRoll  = 5.0f;
            }
            armSw = 0.0f;
        } else if (isBalanceArm) {
            // ── TANGAN KIRI: bergerak berlawanan sedikit untuk keseimbangan ──
            float t = glm::clamp(liftAnim / 0.9f, 0.0f, 1.0f);
            scatterPitch = glm::mix(0.0f, -25.0f, t);   // sedikit ke depan
            elbowExtra   = glm::mix(0.0f, 20.0f, t);    // siku sedikit tekuk
            armSw = 0.0f;
        }

        // ── Sendi Bahu ──
        glm::mat4 shoulder = torso * glm::translate(glm::mat4(1), {(float)side * 0.21f, 0.20f, 0.0f});
        shoulder = glm::rotate(shoulder, glm::radians(armSw + scatterPitch), {1,0,0});
        shoulder = glm::rotate(shoulder, glm::radians(scatterRoll), {0,0,1});

        draw(mSphere, prog, shoulder * TRS({0,0,0}, {0.12f,0.12f,0.12f}), cBaju);
        draw(mCyl,   prog, shoulder * TRS({0,-0.13f,0}, {0.10f,0.25f,0.10f}), cBaju);

        // ── Sendi Siku ──
        glm::mat4 elbow = shoulder * glm::translate(glm::mat4(1), {0,-0.25f,0});
        float elbowBend = fabsf(armSw) * 0.20f + elbowExtra;
        elbow = glm::rotate(elbow, glm::radians(-elbowBend), {1,0,0});

        draw(mSphere, prog, elbow * TRS({0,0,0}, {0.09f,0.09f,0.09f}), cKulit);
        draw(mCyl,   prog, elbow * TRS({0,-0.11f,0}, {0.08f,0.22f,0.08f}), cKulit);

        // ── Pergelangan Tangan — dengan rotasi realistis ──
        glm::mat4 hand = elbow * glm::translate(glm::mat4(1), {0,-0.22f,0});
        // Rotasi pergelangan: menunduk ke bawah saat jangkau, netral saat bawa
        hand = glm::rotate(hand, glm::radians(wristPitch), {1,0,0});
        hand = glm::rotate(hand, glm::radians(wristRoll), {0,0,1});
        draw(mSphere, prog, hand * TRS({0,0,0}, {0.082f,0.090f,0.082f}), cKulit);

        // ── Jari tangan — 4 jari + ibu jari (sederhana) ──
        // Tampilkan jari saat panen: mengepal saat pegang telur, terbuka saat menjangkau
        if (isHarvestArm || isBalanceArm) {
            // Tingkat kepalan jari: 0=terbuka, 1=mengepal
            float gripT = 0.0f;
            if (liftAnim < 0.45f) {
                // Menjangkau: jari terbuka lebar
                gripT = glm::mix(0.0f, 0.15f, liftAnim / 0.45f);
            } else {
                // Sudah pegang / membawa: jari mengepal menggenggam telur
                gripT = glm::mix(0.7f, 1.0f, (liftAnim - 0.45f) / 0.45f);
                if (!carryingEgg) gripT = glm::mix(gripT, 0.0f, 1.0f); // lepas saat taruh
            }
            float fingerCurl = glm::mix(0.0f, 65.0f, gripT); // derajat curl jari

            // 4 jari (depan)
            for (int fi = 0; fi < 4; fi++) {
                float fx  = ((float)fi - 1.5f) * 0.022f;
                float fz  = 0.045f;
                // Ruas pertama (knuckle)
                glm::mat4 knuckle = hand * glm::translate(glm::mat4(1), {fx, -0.04f, fz});
                knuckle = glm::rotate(knuckle, glm::radians(fingerCurl * 0.5f), {1,0,0});
                draw(mBox, prog, knuckle * TRS({0,-0.018f,0}, {0.016f,0.036f,0.016f}), cKulit);
                // Ruas kedua (jari tengah — tekuk lebih dalam)
                glm::mat4 mid = knuckle * glm::translate(glm::mat4(1), {0,-0.036f,0});
                mid = glm::rotate(mid, glm::radians(fingerCurl * 0.8f), {1,0,0});
                draw(mBox, prog, mid * TRS({0,-0.014f,0}, {0.014f,0.028f,0.014f}), cKulit);
                // Ruas ujung (fingertip)
                glm::mat4 tip = mid * glm::translate(glm::mat4(1), {0,-0.028f,0});
                tip = glm::rotate(tip, glm::radians(fingerCurl * 0.6f), {1,0,0});
                draw(mBox, prog, tip * TRS({0,-0.010f,0}, {0.012f,0.020f,0.012f}), cKulit);
            }
            // Ibu jari
            glm::mat4 thumb = hand * glm::translate(glm::mat4(1), {(float)side * 0.048f, -0.02f, 0.025f});
            thumb = glm::rotate(thumb, glm::radians((float)side * -40.0f), {0,0,1});
            thumb = glm::rotate(thumb, glm::radians(fingerCurl * 0.4f), {1,0,0});
            draw(mBox, prog, thumb * TRS({0,-0.018f,0}, {0.016f,0.036f,0.016f}), cKulit);
            glm::mat4 thumbTip = thumb * glm::translate(glm::mat4(1), {0,-0.036f,0});
            thumbTip = glm::rotate(thumbTip, glm::radians(fingerCurl * 0.5f), {1,0,0});
            draw(mBox, prog, thumbTip * TRS({0,-0.014f,0}, {0.014f,0.028f,0.014f}), cKulit);

            // ── Telur di tangan (saat carryingEgg) — posisi pas di telapak ──
            if (carryingEgg && isHarvestArm && liftAnim > 0.3f) {
                // Muncul bertahap saat tangan mulai menggenggam
                float eggShowT = glm::clamp((liftAnim - 0.3f) / 0.15f, 0.0f, 1.0f);
                float eggScale = eggShowT * 0.85f;
                glm::vec3 cTelurHand = {0.99f, 0.95f, 0.85f};
                glm::mat4 eggM = hand * glm::translate(glm::mat4(1), {0, -0.06f, 0.02f});
                eggM = glm::scale(eggM, {eggScale * 0.072f, eggScale * 0.095f, eggScale * 0.072f});
                draw(mSphere, prog, eggM, cTelurHand);
            }
        }

        if (isBucketArm) {
            // ── EMBER PAKAN yang dibawa ──
            glm::mat4 bucketRoot = hand * glm::translate(glm::mat4(1), {0,-0.10f,0});
            float bucketTilt = scattering ? 20.0f * throwAnim : 5.0f;
            bucketRoot = glm::rotate(bucketRoot, glm::radians(bucketTilt * (float)(-side)), {0,0,1});
            bucketRoot = glm::rotate(bucketRoot, glm::radians(-15.0f), {1,0,0});
            draw(mCyl, prog, bucketRoot * TRS({0,0,0}, {0.09f,0.14f,0.09f}), cEmber);
            draw(mCyl, prog, bucketRoot * TRS({0, 0.075f,0}, {0.10f,0.018f,0.10f}), cEmberRim);
            draw(mCyl, prog, bucketRoot * TRS({0,-0.075f,0}, {0.085f,0.012f,0.085f}), cEmberRim);
            if (!scattering || throwAnim < 0.3f) {
                draw(mSphere, prog, bucketRoot * TRS({0, 0.06f, 0}, {0.08f,0.04f,0.08f}), cPakan);
            }
            draw(mBox, prog, bucketRoot * TRS({0, 0.10f, 0}, {0.002f, 0.04f, 0.10f}),
                 {0.40f,0.38f,0.35f});
        }

        if (isScatterArm && scattering) {
            // ── Partikel pakan yang ditabur (3–4 titik kecil) ──
            float t = throwAnim;
            if (t > 0.2f && t < 0.85f) {
                float arc = sinf((t - 0.2f) / 0.65f * 3.14159f);
                for (int p = 0; p < 3; p++) {
                    float pf = (p + 1) * 0.22f;
                    float px = (float)side * pf * 0.28f;
                    float py = arc * 0.18f - pf * 0.12f;
                    float pz = pf * 0.20f;
                    glm::vec3 pPos = pos + glm::vec3(px, 1.0f + py, pz);
                    draw(mBox, prog, TRS(pPos, {0.024f,0.024f,0.024f}), cPakan);
                }
            }
        }
    }

    // ════════════════════════════════
    //  LEHER & KEPALA
    // ════════════════════════════════
    // Leher
    draw(mCyl, prog, torso * TRS({0, 0.32f, 0.01f}, {0.09f,0.12f,0.09f}), cKulit);

    glm::mat4 head = torso * glm::translate(glm::mat4(1), {0, 0.44f, 0.01f});
    // Kepala sedikit bergoyang saat berjalan / menengok ke arah kandang saat beri pakan
    float headNod   = sinf(walkAnim * 2.0f) * 3.0f;
    float headTurnY = scattering ? (float)feedSide * -18.0f : 0.0f; // menoleh ke ayam
    head = glm::rotate(head, glm::radians(headNod), {1,0,0});
    head = glm::rotate(head, glm::radians(headTurnY), {0,1,0});

    // Wajah — bentuk oval sedikit
    draw(mSphere, prog, head * TRS({0,0,0}, {0.200f,0.220f,0.195f}), cKulit);
    // Dahi sedikit lebih lebar
    draw(mSphere, prog, head * TRS({0, 0.06f, 0.05f}, {0.175f,0.14f,0.16f}), cKulit);
    // Pipi
    draw(mSphere, prog, head * TRS({-0.09f,-0.02f,0.09f}, {0.075f,0.065f,0.07f}), 
         {cKulit.r*0.97f, cKulit.g*0.87f, cKulit.b*0.84f});
    draw(mSphere, prog, head * TRS({ 0.09f,-0.02f,0.09f}, {0.075f,0.065f,0.07f}), 
         {cKulit.r*0.97f, cKulit.g*0.87f, cKulit.b*0.84f});
    // Mata — bola mata + iris + sorot putih
    for (int s : {-1, 1}) {
        draw(mSphere, prog, head * TRS({(float)s*0.072f, 0.040f, 0.115f}, {0.028f,0.028f,0.024f}),
             {0.05f,0.03f,0.01f});
        draw(mSphere, prog, head * TRS({(float)s*0.075f, 0.042f, 0.126f}, {0.015f,0.015f,0.013f}),
             {0.30f,0.18f,0.05f});
        draw(mSphere, prog, head * TRS({(float)s*0.077f, 0.048f, 0.128f}, {0.006f,0.006f,0.006f}),
             {0.92f,0.92f,0.92f});
        // Alis tebal
        draw(mBox, prog, head * TRS({(float)s*0.068f, 0.072f, 0.112f}, {0.042f,0.012f,0.020f}),
             {0.22f,0.14f,0.06f});
    }
    // Hidung bulat
    draw(mSphere, prog, head * TRS({0,-0.005f,0.128f}, {0.030f,0.022f,0.028f}),
         {cKulit.r*0.86f, cKulit.g*0.74f, cKulit.b*0.68f});
    // Mulut (senyum kecil)
    draw(mBox, prog, head * TRS({-0.025f,-0.040f,0.120f}, {0.024f,0.010f,0.012f}),
         {cKulit.r*0.68f, cKulit.g*0.52f, cKulit.b*0.48f});
    draw(mBox, prog, head * TRS({ 0.025f,-0.040f,0.120f}, {0.024f,0.010f,0.012f}),
         {cKulit.r*0.68f, cKulit.g*0.52f, cKulit.b*0.48f});
    // Telinga
    for (int s : {-1,1}) {
        draw(mSphere, prog, head * TRS({(float)s*0.115f, 0.008f, 0.005f}, {0.040f,0.045f,0.030f}),
             {cKulit.r*0.92f, cKulit.g*0.78f, cKulit.b*0.72f});
    }

    // ── Topi petani (caping / sedge hat) — lebih proporsional ──
    // Mahkota topi
    draw(mCyl,    prog, head * TRS({0, 0.21f, 0.0f}, {0.200f,0.12f,0.200f}), cTopi);
    // Puncak topi (sedikit lebih kecil)
    draw(mSphere, prog, head * TRS({0, 0.27f, 0.0f}, {0.18f,0.10f,0.18f}), cTopi);
    // Pinggiran caping — lebar & sedikit melengkung ke bawah
    draw(mCyl,    prog, head * TRS({0, 0.18f, 0.0f}, {0.58f,0.026f,0.58f}), cTopiBrim);
    // Bayangan / lapisan bawah brim (sedikit lebih gelap)
    draw(mCyl,    prog, head * TRS({0, 0.165f,0.0f}, {0.54f,0.018f,0.54f}), 
         {cTopiBrim.r*0.80f, cTopiBrim.g*0.78f, cTopiBrim.b*0.55f});
    // Pita topi
    draw(mCyl,    prog, head * TRS({0, 0.19f, 0.0f}, {0.210f,0.030f,0.210f}),
         {0.35f,0.20f,0.08f});
}

// ──────────────────────────────────────────────────────────
//  GAMBAR GEROBAK PANEN — Keranjang terbuka, proporsional
//
//  Desain: gerobak dorong rendah dengan:
//   - Rangka kayu (chassis) ramping
//   - Bak terbuka berlapis papan (flatbed) dengan dinding rendah
//   - 2 roda kayu berspoke di kiri-kanan
//   - As roda logam
//   - Handle dorong (bukan tarik) di belakang
//   - Bantalan telur (egg rack) dari bilah kayu
// ──────────────────────────────────────────────────────────
static void drawGerobak(unsigned int prog, Mesh& mBox, Mesh& mCyl,
                        glm::vec3 gp, float wAngle,
                        float heading = 180.0f, float wobble = 0.0f)
{
    // ── Root transform: posisi + heading + wobble lateral ──
    // Semua bagian gerobak dirender relatif ke matriks root ini
    // sehingga gerobak menghadap ke arah bergeraknya (heading) dan
    // bergoyang sedikit (wobble) seperti roda tidak sempurna.
    // Helper: buat posisi dunia dari posisi lokal gerobak
    float headRad = glm::radians(heading);
    glm::mat4 rootG = glm::translate(glm::mat4(1), gp);
    rootG = glm::rotate(rootG, headRad, {0,1,0});
    rootG = glm::rotate(rootG, glm::radians(wobble * 8.0f), {0,0,1}); // lean ke samping
    // Fungsi helper draw dengan root transform
    auto drawG = [&](Mesh& m, unsigned int p, glm::mat4 localM, glm::vec3 col) {
        draw(m, p, rootG * localM, col);
    };
    // Ganti gp lokal → gunakan {0,0,0} dengan drawG/GP
    gp = {0,0,0};

    glm::vec3 cKayu    = {0.55f, 0.34f, 0.13f};
    glm::vec3 cKayuGel = {0.36f, 0.20f, 0.07f};
    glm::vec3 cKayuMud = {0.45f, 0.27f, 0.10f};
    glm::vec3 cBesi    = {0.25f, 0.24f, 0.22f};
    glm::vec3 cRoda    = {0.10f, 0.09f, 0.08f};
    glm::vec3 cRim     = {0.42f, 0.40f, 0.36f};
    glm::vec3 cSpoke   = {0.48f, 0.30f, 0.11f};

    // ── Dimensi utama ──
    float wheelR  = 0.32f;   // roda sedikit lebih kecil agar proporsional
    float wheelT  = 0.075f;
    float wheelY  = wheelR;
    float axleX   = 0.40f;   // jarak pusat roda dari tengah

    // Deck (lantai bak) sedikit di atas poros roda
    float deckY   = wheelY + 0.06f;
    float deckLen = 0.72f;   // panjang bak (Z)
    float deckWid = 0.60f;   // lebar bak (X)
    float wallH   = 0.16f;   // tinggi dinding bak
    float wallT   = 0.038f;  // tebal papan dinding

    // ── Rangka chassis bawah (2 batang memanjang + 2 palang) ──
    for (int s : {-1, 1})
        drawG(mBox, prog, TRS(gp + glm::vec3{s*0.24f, wheelY-0.02f, 0.0f},
                             {0.058f, 0.055f, deckLen + 0.10f}), cKayuGel);
    // Palang melintang depan & belakang
    drawG(mBox, prog, TRS(gp + glm::vec3{0, wheelY-0.025f,  deckLen*0.5f},
                         {deckWid+0.05f, 0.048f, 0.058f}), cKayuGel);
    drawG(mBox, prog, TRS(gp + glm::vec3{0, wheelY-0.025f, -deckLen*0.5f},
                         {deckWid+0.05f, 0.048f, 0.058f}), cKayuGel);

    // ── Lantai bak — papan memanjang (5 bilah) ──
    for (int p = -2; p <= 2; p++) {
        float px = p * (deckWid / 4.2f);
        drawG(mBox, prog, TRS(gp + glm::vec3{px, deckY, 0.0f},
                             {deckWid/5.2f - 0.01f, 0.040f, deckLen}), cKayu);
    }
    // Celah antar bilah (strip gelap tipis)
    for (int p = -1; p <= 1; p++) {
        float px = p * (deckWid / 4.2f) + (deckWid / 8.4f);
        drawG(mBox, prog, TRS(gp + glm::vec3{px, deckY + 0.001f, 0.0f},
                             {0.012f, 0.042f, deckLen}), cKayuGel);
    }

    // ── Dinding bak (4 sisi rendah, dengan papan horizontal) ──
    // Kiri & kanan: 2 papan bertingkat
    for (int s : {-1, 1}) {
        float wx = s * (deckWid*0.5f + wallT*0.5f);
        // Papan bawah
        drawG(mBox, prog, TRS(gp + glm::vec3{wx, deckY + wallH*0.30f, 0.0f},
                             {wallT, wallH*0.45f, deckLen - 0.02f}), cKayu);
        // Papan atas (lebih gelap — cuaca/usia)
        drawG(mBox, prog, TRS(gp + glm::vec3{wx, deckY + wallH*0.80f, 0.0f},
                             {wallT, wallH*0.45f, deckLen - 0.02f}), cKayuMud);
        // Cap atas dinding samping
        drawG(mBox, prog, TRS(gp + glm::vec3{wx, deckY + wallH + 0.008f, 0.0f},
                             {wallT*2.2f, 0.018f, deckLen + 0.01f}), cKayuGel);
    }
    // Depan & belakang: solid satu papan
    drawG(mBox, prog, TRS(gp + glm::vec3{0, deckY + wallH*0.50f, deckLen*0.5f + wallT*0.5f},
                         {deckWid + wallT*2.0f, wallH, wallT}), cKayu);
    drawG(mBox, prog, TRS(gp + glm::vec3{0, deckY + wallH*0.50f, -deckLen*0.5f - wallT*0.5f},
                         {deckWid + wallT*2.0f, wallH, wallT}), cKayuMud);
    // Corner posts (tiang sudut kecil)
    for (int sx : {-1,1}) for (int sz : {-1,1}) {
        float cx = sx*(deckWid*0.5f + wallT*0.4f);
        float cz = sz*(deckLen*0.5f + wallT*0.4f);
        drawG(mBox, prog, TRS(gp + glm::vec3{cx, deckY + wallH*0.5f, cz},
                             {wallT*1.4f, wallH + 0.02f, wallT*1.4f}), cKayuGel);
    }

    // ── Bantalan / egg rack di dalam bak (bilah silang) ──
    float rackY = deckY + 0.048f;
    // 4 bilah memanjang
    for (int r = -1; r <= 1; r++) {
        drawG(mBox, prog, TRS(gp + glm::vec3{r*0.165f, rackY, 0.0f},
                             {0.020f, 0.012f, deckLen - 0.12f}), cKayuGel);
    }
    // 6 bilah melintang (pembagi sel)
    for (int c = -2; c <= 2; c++) {
        drawG(mBox, prog, TRS(gp + glm::vec3{0, rackY, c*(deckLen/5.0f)},
                             {deckWid - 0.10f, 0.012f, 0.018f}), cKayuGel);
    }

    // ── As roda (axle) besi ──
    {
        glm::mat4 axleM = glm::translate(glm::mat4(1), gp + glm::vec3{0, wheelY, 0.0f});
        axleM = glm::rotate(axleM, glm::radians(90.0f), {0,0,1});
        drawG(mCyl, prog, glm::scale(axleM, {0.048f, axleX*2.4f, 0.048f}), cBesi);
    }

    // ── 2 Roda ──
    for (int side : {-1, 1}) {
        glm::vec3 wc = gp + glm::vec3{side * axleX, wheelY, 0.0f};
        glm::mat4 wb = glm::translate(glm::mat4(1), wc);
        wb = glm::rotate(wb, glm::radians(90.0f), {0,0,1});
        wb = glm::rotate(wb, glm::radians(wAngle), {0,1,0});

        // Ban luar
        drawG(mCyl, prog, glm::scale(wb, {wheelR, wheelT, wheelR}), cRoda);
        // Rim besi
        drawG(mCyl, prog, glm::scale(wb, {wheelR*0.88f, wheelT*1.06f, wheelR*0.88f}), cRim);
        // Jari-jari (8 buah)
        float wr = wheelR * 0.5f;
        for (int sp = 0; sp < 8; sp++) {
            glm::mat4 sr = glm::rotate(glm::mat4(1), glm::radians(sp*45.0f), {0,1,0});
            float sL = wr * 0.84f;
            drawG(mBox, prog, wb * sr * TRS({0,0,sL*0.5f}, {0.026f, wheelT*0.75f, sL}), cSpoke);
        }
        // Hub
        drawG(mCyl, prog, glm::scale(wb, {wheelR*0.17f, wheelT*1.6f, wheelR*0.17f}), cBesi);
        // Baut hub (6)
        float wr2 = wr * 0.30f;
        for (int b = 0; b < 6; b++) {
            glm::mat4 br = glm::rotate(glm::mat4(1), glm::radians(b*60.0f), {0,1,0});
            drawG(mCyl, prog, wb * br * TRS({0,0,wr2}, {0.015f, wheelT*1.7f, 0.015f}), cBesi);
        }
    }

    // ── Handle dorong (di belakang gerobak) — 2 batang + crossbar ──
    float hStartZ = -deckLen*0.5f - wallT;  // pangkal handle (belakang bak)
    float hLen    = 0.68f;
    float hAngle  = -18.0f;   // miring ke atas ke belakang
    float hSpan   = 0.22f;    // jarak kiri-kanan

    for (int s : {-1,1}) {
        glm::mat4 M = glm::translate(glm::mat4(1),
            gp + glm::vec3{s*hSpan, deckY + 0.05f, hStartZ - hLen*0.5f*cosf(glm::radians(-hAngle))});
        M = glm::rotate(M, glm::radians(hAngle), {1,0,0});
        M = glm::scale(M, {0.044f, 0.044f, hLen});
        drawG(mBox, prog, M, cKayuGel);
    }
    // Crossbar
    float hEndZ = hStartZ - hLen * cosf(glm::radians(-hAngle));
    float hEndY = deckY + 0.05f + hLen * sinf(glm::radians(-hAngle));
    drawG(mBox, prog, TRS(gp + glm::vec3{0, hEndY, hEndZ},
                         {hSpan*2.0f + 0.28f, 0.044f, 0.044f}), cKayu);
    // Grip silinder
    for (int s : {-1,1})
        drawG(mCyl, prog, TRS(gp + glm::vec3{s*(hSpan+0.12f), hEndY, hEndZ},
                             {0.034f, 0.13f, 0.034f}), cKayuGel);
}

// ──────────────────────────────────────────────────────────
//  SCENE DRAW  (dipanggil 2x: depth pass + color pass)
//  Fungsi helper agar kode tidak duplikat antara dua pass.
// ──────────────────────────────────────────────────────────
struct SceneObjects {
    Mesh *mFloor, *mWall, *mPillarSq, *mPillarCy, *mRoofSlb, *mRoofRdg;
    Mesh *mPakan, *mBox, *mCyl, *mSphere, *mPohon, *mDaun;
    Mesh *mAyam, *mAyamKep, *mAyamEkor, *mTelur;
    // Tekstur
    unsigned int texRumput, texTanah, texKayu, texGenteng;
    // Status malam (0=siang, 1=malam) untuk efek glow lampu
    float nightness = 0.0f;
};

// Gambar semua objek kandang + pohon + peternak + gerobak + telur
// Argumen prog: shader program yang aktif
// isDepthPass: true = depth pass (tanpa shader color/texture, gunakan progDepth)
static void drawScene(SceneObjects& S, unsigned int prog, float now,
                      bool useTextures = true)
{
    glm::vec3 cRumput  = {0.35f,0.56f,0.20f};
    glm::vec3 cTanah   = {0.50f,0.36f,0.20f};
    glm::vec3 cLorong  = {0.42f,0.32f,0.18f};
    glm::vec3 cKayu    = {0.65f,0.45f,0.25f};
    glm::vec3 cKayuTua = {0.45f,0.28f,0.12f};
    glm::vec3 cGenteng = {0.75f,0.25f,0.12f};
    glm::vec3 cKawat   = {0.75f,0.75f,0.70f};
    glm::vec3 cKrem    = {0.96f,0.93f,0.82f};
    glm::vec3 cPakanCol= {0.88f,0.78f,0.42f};

    // Tekstur hanya digunakan dalam color pass (useTextures=true)
    unsigned int tRumput  = useTextures ? S.texRumput  : 0;
    unsigned int tTanah   = useTextures ? S.texTanah   : 0;
    unsigned int tKayu    = useTextures ? S.texKayu    : 0;
    unsigned int tGenteng = useTextures ? S.texGenteng : 0;

    // ── Tanah ──
    draw(*S.mFloor, prog, TRS({0,-0.028f,-(TOTAL_D*0.5f-1)}, {TOTAL_W+28,1,TOTAL_D+38}), cRumput, tRumput);
    draw(*S.mFloor, prog, TRS({0,-0.022f,-(TOTAL_D*0.5f-1)}, {TOTAL_W,1,TOTAL_D+0.5f}),  cTanah,  tTanah);
    draw(*S.mFloor, prog, TRS({0,-0.016f,-(TOTAL_D*0.5f-1)}, {LORONG_W,1,TOTAL_D+0.5f}), cLorong, tTanah);

    // ── Kandang ──
    float seksiD = TOTAL_D / JML_SEKSI;
    for (int s = 0; s <= JML_SEKSI; s++) {
        float zt = -(s * seksiD);
        for (int sd : {-1,1})
            draw(*S.mPillarSq, prog, TRS({sd*HALF_W, TINGGI_DINDING*0.5f, zt}, {1,TINGGI_DINDING,1}), cKayu, tKayu);
    }
    for (int s = 0; s <= JML_SEKSI; s++) {
        float z = -(s * seksiD);
        draw(*S.mWall, prog, TRS({0,TINGGI_DINDING,z}, {TOTAL_W,0.10f,0.10f}), cKayu, tKayu);
    }
    for (int s = 0; s < JML_SEKSI; s++) {
        float zM = -(s*seksiD + seksiD*0.5f);
        float len = seksiD;
        draw(*S.mWall, prog, TRS({-HALF_W,0.6f,zM}, {0.06f,1.2f,len}), cKrem);
        draw(*S.mWall, prog, TRS({ HALF_W,0.6f,zM}, {0.06f,1.2f,len}), cKrem);
        float kawatH = TINGGI_DINDING - 1.2f;
        float kawatY = 1.2f + kawatH * 0.5f;
        draw(*S.mWall, prog, TRS({-HALF_W,kawatY,zM}, {0.02f,kawatH,len}), cKawat);
        draw(*S.mWall, prog, TRS({ HALF_W,kawatY,zM}, {0.02f,kawatH,len}), cKawat);
        draw(*S.mPillarSq, prog, TRS({-HALF_W,kawatY,zM}, {0.6f,kawatH,0.6f}), cKayu, tKayu);
        draw(*S.mPillarSq, prog, TRS({ HALF_W,kawatY,zM}, {0.6f,kawatH,0.6f}), cKayu, tKayu);
    }
    draw(*S.mWall, prog, TRS({0,TINGGI_DINDING*0.5f,-(TOTAL_D-0.1f)}, {TOTAL_W,TINGGI_DINDING,0.04f}), cKawat);
    {
        float panelW = HALF_W - LORONG_W*0.5f;
        draw(*S.mWall, prog, TRS({-(HALF_W - panelW*0.5f),TINGGI_DINDING*0.5f,0.1f}, {panelW,TINGGI_DINDING,0.04f}), cKawat);
        draw(*S.mWall, prog, TRS({ HALF_W - panelW*0.5f, TINGGI_DINDING*0.5f,0.1f}, {panelW,TINGGI_DINDING,0.04f}), cKawat);
    }
    draw(*S.mPillarSq, prog, TRS({-LORONG_W*0.5f,TINGGI_DINDING*0.5f,0.1f}, {1,TINGGI_DINDING,1}), cKayu, tKayu);
    draw(*S.mPillarSq, prog, TRS({ LORONG_W*0.5f,TINGGI_DINDING*0.5f,0.1f}, {1,TINGGI_DINDING,1}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({0,TINGGI_DINDING*0.80f,0.1f}, {LORONG_W,0.14f,0.14f}), cKayuTua, tKayu);
    draw(*S.mWall, prog, TRS({0,TINGGI_DINDING*0.90f,0.08f}, {LORONG_W*0.7f,0.18f,0.06f}), {0.82f,0.62f,0.22f});

    // ── Atap ──
    {
        float pitchRad = glm::radians(ROOF_PITCH_DEG);
        float rise     = HALF_W * tanf(pitchRad);
        float ridgeY   = TINGGI_DINDING + rise;
        float slope    = HALF_W / cosf(pitchRad);
        float panelCX  = -HALF_W * 0.5f;
        float panelCY  = (TINGGI_DINDING + ridgeY) * 0.5f;

        for (int s = 0; s < JML_SEKSI; s++) {
            float zM = -(s*seksiD + seksiD*0.5f);
            float sD = seksiD + 0.30f;
            { glm::mat4 M = glm::translate(glm::mat4(1), {panelCX, panelCY, zM});
              M = glm::rotate(M, pitchRad, {0,0,1}); M = glm::scale(M, {slope,0.12f,sD});
              draw(*S.mRoofSlb, prog, M, cGenteng, tGenteng); }
            { glm::mat4 M = glm::translate(glm::mat4(1), {-panelCX, panelCY, zM});
              M = glm::rotate(M, -pitchRad, {0,0,1}); M = glm::scale(M, {slope,0.12f,sD});
              draw(*S.mRoofSlb, prog, M, cGenteng, tGenteng); }
            draw(*S.mRoofRdg, prog, TRS({0,ridgeY+0.07f,zM}, {1,1,sD}), cKayuTua, tKayu);
        }
        for (float zo : {0.28f, -(TOTAL_D-0.28f)}) {
            { glm::mat4 M = glm::translate(glm::mat4(1), {panelCX, panelCY, zo});
              M = glm::rotate(M, pitchRad, {0,0,1}); M = glm::scale(M, {slope,0.12f,0.50f});
              draw(*S.mRoofSlb, prog, M, cGenteng, tGenteng); }
            { glm::mat4 M = glm::translate(glm::mat4(1), {-panelCX, panelCY, zo});
              M = glm::rotate(M, -pitchRad, {0,0,1}); M = glm::scale(M, {slope,0.12f,0.50f});
              draw(*S.mRoofSlb, prog, M, cGenteng, tGenteng); }
        }
    }

    // ── Sekat baris ──
    for (int b = 0; b <= BARIS_AYAM; b++) {
        float z = -(b*JARAK_Z + 1.0f) + JARAK_Z*0.5f;
        float sekatW = KOLOM_AYAM*JARAK_X + 0.3f;
        draw(*S.mWall, prog, TRS({-(OFFSET_SISI + sekatW*0.5f - sekatW*0.5f),0.35f,z}, {sekatW,0.70f,0.04f}), cKawat);
        draw(*S.mWall, prog, TRS({ (OFFSET_SISI + sekatW*0.5f - sekatW*0.5f),0.35f,z}, {sekatW,0.70f,0.04f}), cKawat);
    }

    // ══════════════════════════════════════════════════════════
    //  TEMPAT PAKAN AYAM — Talang V-trough realistis
    //
    //  Bentuk penampang talang:
    //   - 2 papan miring (kiri+kanan) membentuk huruf V
    //   - Alas dasar (lembah V) sebagai pelapis bawah
    //   - Dinding luar (bibir atas) sedikit lebih tinggi
    //   - Isi pakan (butiran) mengisi cekungan V
    //   - Pembatas vertikal (divider) setiap ~3 baris
    //   - Tiang kaki penyangga berbentuk T tiap ~5 baris
    //   - Tutup ujung (end cap) di kedua ujung talang
    // ══════════════════════════════════════════════════════════
    {
        // ── Dimensi talang ──
        float tStart =  0.8f;                    // ujung depan
        float tEnd   = -(TOTAL_D - 0.3f);        // ujung belakang
        float tLen   = tStart - tEnd;
        float tZc    = (tStart + tEnd) * 0.5f;

        glm::vec3 cTalang    = {0.58f, 0.38f, 0.16f};  // kayu talang (coklat medium)
        glm::vec3 cTalangGel = {0.40f, 0.24f, 0.08f};  // kayu lebih gelap (ujung/tiang)
        glm::vec3 cTalangTua = {0.32f, 0.19f, 0.06f};  // kayu tua (divider)
        glm::vec3 cBesi      = {0.30f, 0.28f, 0.26f};  // besi / paku baut
        glm::vec3 cPakanIsi  = {0.88f, 0.78f, 0.42f};  // pakan kuning
        glm::vec3 cPakanGelapIsi = {0.72f, 0.60f, 0.28f}; // pakan lebih gelap

        // X-center masing-masing talang
        float xL = -(OFFSET_SISI - 0.35f);  // kiri
        float xR =  (OFFSET_SISI - 0.35f);  // kanan

        // ── Geometri papan V-trough ──
        //   papan kiri-dalam (miring ke tengah dari luar ke dalam)
        //   papan kanan-dalam (cermin)
        //   alas bawah
        //   papan kiri-luar (dinding bibir atas)
        //   papan kanan-luar (cermin)
        //
        //   Tampak atas dari X:
        //      luar-kiri  dalam-kiri  alas  dalam-kanan  luar-kanan
        //      |-------\  /----------|------\  /---------|
        //               \/                   \/
        //
        //  Ukuran (lebar talang ~0.38, kedalaman ~0.14)

        float tw    = 0.19f;   // setengah lebar dalam talang
        float tdep  = 0.13f;   // kedalaman V (dari bibir ke alas)
        float twall = 0.025f;  // tebal papan
        float tRimH = 0.06f;   // tinggi dinding bibir atas
        float tBaseY = 0.13f;  // tinggi pusat alas dari tanah

        // Sudut papan miring V: arctan(tdep / tw) ≈ 34°
        float vAngleDeg = glm::degrees(atanf(tdep / tw));

        for (int side : {-1, 1}) {
            float xc = (side == -1) ? xL : xR;

            // ── Alas bawah talang (strip tipis di lembah V) ──
            draw(*S.mBox, prog,
                 TRS({xc, tBaseY, tZc}, {tw*0.55f, twall*0.9f, tLen}),
                 cTalangGel, tKayu);

            // ── Papan miring kiri-dalam ──
            {
                float px = xc - tw * 0.45f;
                float py = tBaseY + tdep * 0.5f;
                glm::mat4 M = glm::translate(glm::mat4(1), {px, py, tZc});
                M = glm::rotate(M, glm::radians(-vAngleDeg), {0,0,1});
                M = glm::scale(M, {sqrtf(tw*tw*0.45f*0.45f + tdep*tdep*0.25f)*2.1f,
                                   twall, tLen});
                draw(*S.mBox, prog, M, cTalang, tKayu);
            }
            // ── Papan miring kanan-dalam ──
            {
                float px = xc + tw * 0.45f;
                float py = tBaseY + tdep * 0.5f;
                glm::mat4 M = glm::translate(glm::mat4(1), {px, py, tZc});
                M = glm::rotate(M, glm::radians( vAngleDeg), {0,0,1});
                M = glm::scale(M, {sqrtf(tw*tw*0.45f*0.45f + tdep*tdep*0.25f)*2.1f,
                                   twall, tLen});
                draw(*S.mBox, prog, M, cTalang, tKayu);
            }

            // ── Dinding luar kiri (bibir) ──
            draw(*S.mBox, prog,
                 TRS({xc - tw - twall*0.5f, tBaseY + tdep * 0.5f + tRimH * 0.5f, tZc},
                     {twall, tRimH + tdep, tLen}),
                 cTalang, tKayu);
            // ── Dinding luar kanan (bibir) ──
            draw(*S.mBox, prog,
                 TRS({xc + tw + twall*0.5f, tBaseY + tdep * 0.5f + tRimH * 0.5f, tZc},
                     {twall, tRimH + tdep, tLen}),
                 cTalang, tKayu);

            // ── Strip bibir atas (penguatan horizontal) ──
            float rimY = tBaseY + tdep + tRimH - twall * 0.5f;
            draw(*S.mBox, prog,
                 TRS({xc - tw - twall*0.5f, rimY, tZc}, {twall * 3.5f, twall, tLen}),
                 cTalangGel, tKayu);
            draw(*S.mBox, prog,
                 TRS({xc + tw + twall*0.5f, rimY, tZc}, {twall * 3.5f, twall, tLen}),
                 cTalangGel, tKayu);

            // ── Isi pakan di dalam talang ──
            // Lapisan bawah: butiran lebih gelap
            draw(*S.mBox, prog,
                 TRS({xc, tBaseY + twall * 0.6f, tZc}, {tw * 1.0f, twall * 1.2f, tLen - 0.05f}),
                 cPakanGelapIsi);
            // Lapisan atas: butiran lebih terang & menggembung sedikit
            draw(*S.mBox, prog,
                 TRS({xc, tBaseY + twall * 1.4f, tZc}, {tw * 0.80f, twall * 0.9f, tLen - 0.08f}),
                 cPakanIsi);

            // ── Tutup ujung (end cap) ──
            for (float tz : {tStart - 0.02f, tEnd + 0.02f}) {
                draw(*S.mBox, prog,
                     TRS({xc, tBaseY + tdep * 0.5f + tRimH * 0.3f, tz},
                         {tw*2.0f + twall*2.5f, tdep + tRimH * 0.6f + twall, twall * 2.0f}),
                     cTalangGel, tKayu);
                // Plat besi ujung (detail)
                draw(*S.mBox, prog,
                     TRS({xc, tBaseY + tdep * 0.5f + tRimH * 0.3f, tz + (tz > 0 ? 0.014f : -0.014f)},
                         {tw*2.0f + twall*2.2f, tdep + tRimH*0.55f, twall * 0.8f}),
                     cBesi);
            }

            // ── Pembatas / divider vertikal setiap ~3 baris ──
            // Ini membantu ayam tidak berebut dan membagi zona makan
            for (int b = 0; b < BARIS_AYAM; b += 3) {
                float dz = -(b * JARAK_Z + 1.0f) + JARAK_Z * 1.5f;
                if (dz < tEnd || dz > tStart) continue;
                // Papan pembatas tegak di dalam talang
                draw(*S.mBox, prog,
                     TRS({xc, tBaseY + twall * 0.5f + tdep * 0.4f, dz},
                         {tw * 1.8f, tdep * 0.85f, twall * 1.5f}),
                     cTalangTua, tKayu);
                // Baut kecil di ujung pembatas
                for (int s2 : {-1, 1}) {
                    draw(*S.mBox, prog,
                         TRS({xc + s2 * tw * 0.75f, tBaseY + tdep * 0.55f, dz},
                             {0.018f, 0.018f, twall * 2.2f}),
                         cBesi);
                }
            }

            // ── Tiang kaki penyangga — setiap ~4 baris, berbentuk T ──
            for (int b = 0; b <= BARIS_AYAM; b += 4) {
                float tz = -(b * JARAK_Z + 1.0f);
                if (tz < tEnd - 0.05f || tz > tStart + 0.05f) continue;

                // Kaki vertikal (2 kaki per tiang, kiri & kanan talang)
                for (int ks : {-1, 1}) {
                    float kx = xc + ks * (tw + twall * 0.5f - 0.02f);
                    // Batang vertikal utama
                    draw(*S.mBox, prog,
                         TRS({kx, tBaseY * 0.5f, tz}, {0.040f, tBaseY, 0.040f}),
                         cTalangGel, tKayu);
                    // Kaki diagonal penopang (miring ke dalam)
                    glm::mat4 diagM = glm::translate(glm::mat4(1),
                        {kx - ks * 0.04f, tBaseY * 0.30f, tz});
                    diagM = glm::rotate(diagM, glm::radians((float)ks * 20.0f), {0,0,1});
                    diagM = glm::scale(diagM, {0.028f, tBaseY * 0.55f, 0.028f});
                    draw(*S.mBox, prog, diagM, cTalangGel, tKayu);
                }
                // Palang horizontal penghubung kedua kaki (crossbar bawah)
                draw(*S.mBox, prog,
                     TRS({xc, tBaseY * 0.12f, tz},
                         {(tw + twall) * 2.2f, 0.030f, 0.040f}),
                     cTalangGel, tKayu);

                // Braket pengikat ke dinding talang (2 sudut besi kecil)
                for (int ks : {-1, 1}) {
                    draw(*S.mBox, prog,
                         TRS({xc + ks * tw * 0.9f, tBaseY + twall, tz},
                             {0.032f, 0.055f, 0.032f}),
                         cBesi);
                }
            }
        }
    }  // end tempat pakan

    // ── Pohon ──
    float pxs[]={-HALF_W-3.5f,-HALF_W-7.0f,HALF_W+3.5f,HALF_W+7.0f,-HALF_W-5.5f,HALF_W+5.5f};
    float pzs[]={-2.0f,-9.0f,-3.0f,-11.0f,-(TOTAL_D-3.0f),-(TOTAL_D-4.0f)};
    for (int t = 0; t < 6; t++) {
        draw(*S.mPohon, prog, TRS({pxs[t],1.4f,pzs[t]}), {0.40f,0.24f,0.10f}, tKayu);
        draw(*S.mDaun,  prog, TRS({pxs[t],3.6f,pzs[t]}, {1,0.9f,1}), {0.22f,0.52f,0.16f});
    }

    // ── Pagar Keliling ──
    float fenceW = TOTAL_W + 12.0f;
    float fenceD = TOTAL_D + 14.0f;
    float fenceZ = -(TOTAL_D * 0.5f - 1.0f);
    // Tiang pagar keliling
    for(float x = -fenceW*0.5f; x <= fenceW*0.5f + 0.1f; x += 3.0f) {
        draw(*S.mPillarSq, prog, TRS({x, 0.6f, fenceZ - fenceD*0.5f}, {0.3f, 1.2f, 0.3f}), cKayuTua, tKayu);
        draw(*S.mPillarSq, prog, TRS({x, 0.6f, fenceZ + fenceD*0.5f}, {0.3f, 1.2f, 0.3f}), cKayuTua, tKayu);
    }
    for(float z = fenceZ - fenceD*0.5f; z <= fenceZ + fenceD*0.5f + 0.1f; z += 3.0f) {
        draw(*S.mPillarSq, prog, TRS({-fenceW*0.5f, 0.6f, z}, {0.3f, 1.2f, 0.3f}), cKayuTua, tKayu);
        draw(*S.mPillarSq, prog, TRS({ fenceW*0.5f, 0.6f, z}, {0.3f, 1.2f, 0.3f}), cKayuTua, tKayu);
    }
    // Palang pagar
    draw(*S.mWall, prog, TRS({0, 0.8f, fenceZ - fenceD*0.5f}, {fenceW, 0.15f, 0.05f}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({0, 0.4f, fenceZ - fenceD*0.5f}, {fenceW, 0.15f, 0.05f}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({0, 0.8f, fenceZ + fenceD*0.5f}, {fenceW, 0.15f, 0.05f}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({0, 0.4f, fenceZ + fenceD*0.5f}, {fenceW, 0.15f, 0.05f}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({-fenceW*0.5f, 0.8f, fenceZ}, {0.05f, 0.15f, fenceD}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({-fenceW*0.5f, 0.4f, fenceZ}, {0.05f, 0.15f, fenceD}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({ fenceW*0.5f, 0.8f, fenceZ}, {0.05f, 0.15f, fenceD}), cKayu, tKayu);
    draw(*S.mWall, prog, TRS({ fenceW*0.5f, 0.4f, fenceZ}, {0.05f, 0.15f, fenceD}), cKayu, tKayu);

    // ── Silo Pakan (2 Tabung Raksasa) ──
    glm::vec3 cSilo = {0.85f, 0.85f, 0.88f}; // Silver / Metalik
    glm::vec3 cSiloBawah = {0.3f, 0.3f, 0.3f};
    for(float sx : {-HALF_W - 2.5f, HALF_W + 2.5f}) {
        float sz = -3.0f;
        // Tabung utama
        draw(*S.mCyl, prog, TRS({sx, 2.5f, sz}, {2.0f, 4.0f, 2.0f}), cSilo);
        // Atap silo membulat
        draw(*S.mSphere, prog, TRS({sx, 4.5f, sz}, {2.0f, 0.8f, 2.0f}), cSiloBawah);
        // Kerucut bawah
        draw(*S.mSphere, prog, TRS({sx, 0.5f, sz}, {2.0f, 0.8f, 2.0f}), cSilo);
        // Kaki penyangga silo
        for(int s=0; s<4; s++) {
            float ax = (s%2==0) ? 0.8f : -0.8f;
            float az = (s<2) ? 0.8f : -0.8f;
            draw(*S.mCyl, prog, TRS({sx + ax, 1.0f, sz + az}, {0.2f, 2.0f, 0.2f}), cSiloBawah);
        }
    }

    // ── Semak-Semak (Bushes) ──
    float bxs[] = {-HALF_W-1.5f, HALF_W+1.2f, -HALF_W-4.0f, HALF_W+3.0f, 3.0f, -4.0f};
    float bzs[] = {-1.0f, -0.5f, -6.0f, -8.0f, 4.5f, 5.0f};
    for(int b=0; b<6; b++) {
        draw(*S.mDaun, prog, TRS({bxs[b], 0.2f, bzs[b]}, {0.6f, 0.4f, 0.6f}), {0.25f, 0.60f, 0.20f});
        draw(*S.mDaun, prog, TRS({bxs[b]+0.3f, 0.1f, bzs[b]+0.2f}, {0.4f, 0.3f, 0.4f}), {0.20f, 0.55f, 0.15f});
    }

    // ── Pipa Air Minum (PVC) ──
    float pipaY = 0.45f;
    float pipeLen = BARIS_AYAM*JARAK_Z + 0.5f;
    float pipeZ   = -(pipeLen*0.5f + 0.5f);
    glm::mat4 pipeML = glm::translate(glm::mat4(1), {-(OFFSET_SISI-0.10f), pipaY, pipeZ});
    pipeML = glm::rotate(pipeML, 1.5708f, {1,0,0});
    pipeML = glm::scale(pipeML, {0.04f, pipeLen, 0.04f});
    draw(*S.mCyl, prog, pipeML, {0.85f, 0.85f, 0.85f});
    
    glm::mat4 pipeMR = glm::translate(glm::mat4(1), { OFFSET_SISI-0.10f, pipaY, pipeZ});
    pipeMR = glm::rotate(pipeMR, 1.5708f, {1,0,0});
    pipeMR = glm::scale(pipeMR, {0.04f, pipeLen, 0.04f});
    draw(*S.mCyl, prog, pipeMR, {0.85f, 0.85f, 0.85f});

    // ── Kincir Angin (Windmill) Animasi ──
    glm::vec3 wmPos = {HALF_W + 5.0f, 0.0f, -TOTAL_D + 5.0f};
    draw(*S.mPillarSq, prog, TRS(wmPos + glm::vec3{0, 3.0f, 0}, {0.4f, 6.0f, 0.4f}), cKayuTua, tKayu);
    draw(*S.mBox, prog, TRS(wmPos + glm::vec3{0, 6.0f, 0.2f}, {0.5f, 0.5f, 0.6f}), cKayu, tKayu);
    glm::mat4 fanRoot = glm::translate(glm::mat4(1), wmPos + glm::vec3{0, 6.0f, 0.5f});
    fanRoot = glm::rotate(fanRoot, now * 1.5f, {0, 0, 1}); // Putar berdasarkan waktu
    glm::mat4 poros = fanRoot;
    poros = glm::rotate(poros, 1.5708f, {1,0,0});
    poros = glm::scale(poros, {0.1f, 0.4f, 0.1f});
    draw(*S.mCyl, prog, poros, {0.3f, 0.3f, 0.3f}); 
    for(int b=0; b<4; b++) {
        glm::mat4 bladeM = glm::rotate(fanRoot, b * 1.5708f, {0,0,1});
        draw(*S.mWall, prog, bladeM * TRS({0, 1.2f, 0.1f}, {0.4f, 2.0f, 0.04f}), {0.9f, 0.9f, 0.9f});
    }

    // ── Awan (Bergerak di Langit) ──
    float cloudBases[][3] = {
        {-15.0f, 16.0f, -10.0f},
        { 12.0f, 18.0f, -25.0f},
        {  5.0f, 15.0f,   5.0f},
        {-20.0f, 17.0f, -35.0f}
    };
    for(int c=0; c<4; c++) {
        float cx = cloudBases[c][0] + fmodf(now * 0.8f + c*10.0f, 80.0f) - 40.0f; 
        float cy = cloudBases[c][1];
        float cz = cloudBases[c][2];
        glm::vec3 cCol = {0.9f, 0.9f, 0.95f};
        draw(*S.mSphere, prog, TRS({cx, cy, cz}, {2.0f, 1.5f, 2.0f}), cCol);
        draw(*S.mSphere, prog, TRS({cx+1.5f, cy-0.2f, cz}, {1.5f, 1.2f, 1.5f}), cCol);
        draw(*S.mSphere, prog, TRS({cx-1.5f, cy-0.1f, cz+0.5f}, {1.6f, 1.3f, 1.6f}), cCol);
        draw(*S.mSphere, prog, TRS({cx, cy+0.5f, cz+1.0f}, {1.8f, 1.4f, 1.8f}), cCol);
    }

    // ── Lampu Kandang — menyala saat malam ──
    // nightness: 0=siang penuh, 1=malam penuh
    float lampGlow = S.nightness;       // intensitas glow berdasarkan waktu
    bool lampsLit  = lampGlow > 0.25f;
    // Warna bohlam: putih/kuning saat mati, kuning-oranye terang saat nyala
    glm::vec3 cBohlamMati  = {0.88f, 0.86f, 0.78f};
    glm::vec3 cBohlamNyala = {1.00f, 0.95f, 0.55f};
    glm::vec3 cBohlam = lampsLit
        ? glm::mix(cBohlamMati, cBohlamNyala, lampGlow)
        : cBohlamMati;

    int numLampu = 5;
    for (int i = 1; i <= numLampu; i++) {
        float lZ = -(i * (TOTAL_D / (numLampu + 1.0f)));
        // Kabel dari atap ke fitting
        draw(*S.mCyl, prog, TRS({0.0f, TINGGI_DINDING + 0.55f, lZ}, {0.015f,0.55f,0.015f}), {0.08f,0.08f,0.08f});
        // Fitting lampu (kotak kecil logam)
        draw(*S.mBox, prog, TRS({0.0f, TINGGI_DINDING + 0.18f, lZ}, {0.10f,0.06f,0.10f}), {0.22f,0.22f,0.24f});
        // Reflektor (piringan di atas bohlam)
        draw(*S.mCyl, prog, TRS({0.0f, TINGGI_DINDING + 0.16f, lZ}, {0.20f,0.04f,0.20f}), {0.50f,0.50f,0.54f});
        // Bohlam — terang saat malam
        draw(*S.mSphere, prog, TRS({0.0f, TINGGI_DINDING + 0.10f, lZ}, {0.10f,0.12f,0.10f}), cBohlam);
        // Halo/glow ekstra (sphere besar transparan via color) — hanya saat malam
        if (lampsLit) {
            float haloA = lampGlow * 0.45f;
            draw(*S.mSphere, prog, TRS({0.0f, TINGGI_DINDING + 0.10f, lZ}, {0.22f,0.24f,0.22f}),
                 {cBohlamNyala.r * haloA, cBohlamNyala.g * haloA, cBohlamNyala.b * haloA * 0.5f});
        }
    }
}

// ══════════════════════════════════════════════════════════
//  MAIN
// ══════════════════════════════════════════════════════════
int main() {
    srand(42);
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(SCR_W, SCR_H,
        "Simulasi Peternakan v4 | F=Pakan  H=Panen  WASD+QE=Gerak  Scroll=Putar",
        nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSetKeyCallback(win, key_callback);
    glfwSetScrollCallback(win, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glViewport(0, 0, SCR_W, SCR_H);

    // ── Compile shaders ──
    unsigned int progStd      = makeProgram(VS_STD, FS_STD);
    unsigned int progInst     = makeProgram(VS_INST, FS_INST);
    unsigned int progDepth    = makeProgram(VS_DEPTH, FS_DEPTH);
    unsigned int progDepthInst= makeProgram(VS_DEPTH_INST, FS_DEPTH);

    // ── Shadow map FBO ──
    shadowMap.init();

    // ── Mesh ──
    Mesh mFloor    = createBox(1, 0.05f, 1);
    Mesh mWall     = createBox(1, 1, 0.05f);
    Mesh mPillarSq = createBox(0.16f, 1, 0.16f);
    Mesh mPillarCy = createCylinder(0.08f, 1, 12);
    Mesh mRoofSlb  = createBox(1, 0.12f, 1);
    Mesh mRoofRdg  = createBox(0.18f, 0.28f, 1);
    Mesh mPakan    = createBox(0.9f, 0.12f, 0.20f);
    Mesh mAyam     = createSphere(1.0f, 16, 12);
    Mesh mAyamKep  = createSphere(0.09f, 8, 6);
    Mesh mAyamEkor = createBox(0.08f, 0.12f, 0.10f);
    Mesh mTelur    = createSphere(0.085f, 10, 8);
    Mesh mBox      = createBox(1, 1, 1);
    Mesh mCyl      = createCylinder(0.5f, 1, 18);
    Mesh mSphere   = createSphere(0.5f, 12, 10);
    Mesh mPohon    = createCylinder(0.14f, 2.8f, 8);
    Mesh mDaun     = createSphere(1.0f, 10, 8);

    // ── Generate Procedural Textures ──
    unsigned int texRumput  = makeProceduralTexture(1,  89, 143, 51);
    unsigned int texTanah   = makeProceduralTexture(1, 127,  92, 51);
    unsigned int texKayu    = makeProceduralTexture(2, 166, 115, 64);
    unsigned int texGenteng = makeProceduralTexture(3, 191,  64, 30);

    // ── Posisi Ayam ──
    initAyamPositions();

    // ── Instance Buffers ──
    // vboOff: posisi base tiap ayam (X,Y,Z) — STATIC
    // vboBob: nilai bob tiap ayam          — DYNAMIC (diupdate tiap frame)
    float iOffsets[NUM_AYAM * 3];
    for (int i = 0; i < NUM_AYAM; i++) {
        iOffsets[i*3+0] = ayamBasePos[i].x;
        iOffsets[i*3+1] = ayamBasePos[i].y;
        iOffsets[i*3+2] = ayamBasePos[i].z;
        ayamBobs[i] = 0; ayamFeeding[i] = false; ayamBobPhase[i] = 0;
    }

    unsigned int vboOff, vboBob;
    glGenBuffers(1, &vboOff);
    glBindBuffer(GL_ARRAY_BUFFER, vboOff);
    glBufferData(GL_ARRAY_BUFFER, sizeof(iOffsets), iOffsets, GL_STATIC_DRAW);

    glGenBuffers(1, &vboBob);
    glBindBuffer(GL_ARRAY_BUFFER, vboBob);
    glBufferData(GL_ARRAY_BUFFER, NUM_AYAM * sizeof(float), ayamBobs, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // ── Attach instance attribs ke SEMUA mesh yang digunakan instancing ──
    // (bodi, kepala, ekor — kaki pakai loop manual karena butuh transformasi per-part)
    attachInstanceBuffers(mAyam.VAO,    vboOff, vboBob);
    attachInstanceBuffers(mAyamKep.VAO, vboOff, vboBob);
    attachInstanceBuffers(mAyamEkor.VAO,vboOff, vboBob);
    // mCyl juga untuk badan/kepala silinder — tapi kaki ayam digambar manual
    // karena perlu rotasi per-kaki. Kita buat satu CylInst khusus.
    Mesh mAyamCyl = createCylinder(0.5f, 1, 12);
    attachInstanceBuffers(mAyamCyl.VAO, vboOff, vboBob);

    // Posisi telur
    for (int i = 0; i < NUM_AYAM; i++)
        eggs[i].pos = ayamBasePos[i] + glm::vec3{0, -0.04f, 0.20f};

    // ── Scene objects ──
    SceneObjects S;
    S.mFloor=&mFloor; S.mWall=&mWall; S.mPillarSq=&mPillarSq; S.mPillarCy=&mPillarCy;
    S.mRoofSlb=&mRoofSlb; S.mRoofRdg=&mRoofRdg; S.mPakan=&mPakan;
    S.mBox=&mBox; S.mCyl=&mCyl; S.mSphere=&mSphere;
    S.mPohon=&mPohon; S.mDaun=&mDaun;
    S.mAyam=&mAyam; S.mAyamKep=&mAyamKep; S.mAyamEkor=&mAyamEkor; S.mTelur=&mTelur;
    S.texRumput=texRumput; S.texTanah=texTanah; S.texKayu=texKayu; S.texGenteng=texGenteng;

    // Warna ayam
    glm::vec3 cAyamCol = {0.95f,0.88f,0.70f};
    glm::vec3 cJengger = {0.85f,0.15f,0.10f};
    glm::vec3 cKrem    = {0.96f,0.93f,0.82f};
    glm::vec3 cKakiAy  = {0.75f,0.52f,0.08f};
    glm::vec3 cParuhAy = {0.88f,0.65f,0.05f};
    glm::vec3 cPialAy  = {0.82f,0.08f,0.06f};
    glm::vec3 cTelurCol= {0.99f,0.95f,0.85f};

    // ──────────────────────────────────────────────────────
    //  RENDER LOOP
    // ──────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(win)) {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame; lastFrame = now;
        processInput(win);

        // ── Update Day/Night ──
        dayNight.update(now);

        // ── Update simulasi (FEEDING / HARVESTING) ──
        if (simState == SimState::FEEDING) {
            // Peternak berjalan di lorong tengah, berhenti per-baris, menabur ke kiri & kanan
            // phase 100..100+BARIS_AYAM: berjalan ke baris ayam, lalu scatter ke kiri, lalu kanan
            // feedSide: -1=kiri, +1=kanan
            // scatterState per baris: 0=jalan, 1=scatter kiri, 2=scatter kanan, lanjut baris berikut

            if (peternak.phase >= 100 && peternak.phase < 100 + BARIS_AYAM) {
                int row = peternak.phase - 100;
                // Posisi Z target = di depan baris ayam ini (di tengah lorong)
                float rowZ  = -(row * JARAK_Z + 1.0f);
                // Petani berjalan di lorong tengah (X = 0)
                glm::vec3 targetPos = {0.0f, 0.0f, rowZ};
                float dist = glm::distance(
                    glm::vec2(peternak.pos.x, peternak.pos.z),
                    glm::vec2(targetPos.x, targetPos.z));

                if (!peternak.scattering && dist > 0.18f) {
                    // ─ Berjalan ke posisi baris ─
                    glm::vec3 dir = glm::normalize(targetPos - peternak.pos);
                    peternak.pos += dir * peternak.speed * deltaTime;
                    peternak.walkAnim += deltaTime * 6.5f;
                    peternak.rot    = glm::degrees(atan2f(dir.x, -dir.z));
                    peternak.walking   = true;
                    peternak.bendAngle = 0.0f;
                    peternak.scattering = false;
                    peternak.throwAnim  = 0.0f;
                    peternak.feedSide   = 0;
                    if (peternak.arrived) { peternak.arrived = false; }
                } else if (!peternak.scattering) {
                    // ─ Tiba di baris → mulai scatter kiri ─
                    peternak.pos.x = 0.0f;
                    peternak.pos.z = rowZ;
                    peternak.walking   = false;
                    peternak.bendAngle = 0.0f;
                    peternak.scattering = true;
                    peternak.feedSide   = -1;  // mulai kiri
                    peternak.throwAnim  = 0.0f;
                    peternak.phaseTimer = now;
                    // Hadap ke kiri (ke ayam kiri)
                    peternak.rot = -90.0f;
                    // Aktifkan ayam kiri di baris ini
                    int idxL = row * 2;     // ayam kiri
                    int idxR = row * 2 + 1; // ayam kanan
                    if (idxL < NUM_AYAM) { ayamFeeding[idxL] = true; }
                    if (idxR < NUM_AYAM) { ayamFeeding[idxR] = true; }
                } else {
                    // ─ Sedang scatter ─
                    float el = now - peternak.phaseTimer;
                    float scatterDur = 1.0f;  // durasi satu sisi

                    // Animasi lempar: throwAnim siklus 0→1 berulang selama scatter
                    peternak.throwAnim = fmodf(el / 0.55f, 1.0f);

                    // Tubuh sedikit condong ke sisi yang ditabur
                    peternak.bendAngle = 6.0f * sinf(el * 5.0f);

                    if (peternak.feedSide == -1) {
                        // Hadap + condong ke kiri
                        peternak.rot = -90.0f + 15.0f * sinf(el * 2.5f);
                        if (el >= scatterDur) {
                            // Pindah ke kanan
                            peternak.feedSide  = +1;
                            peternak.phaseTimer = now;
                            peternak.rot = 90.0f;
                        }
                    } else {
                        // Hadap + condong ke kanan
                        peternak.rot = 90.0f - 15.0f * sinf(el * 2.5f);
                        if (el >= scatterDur) {
                            // Selesai baris ini, lanjut ke baris berikut
                            peternak.scattering = false;
                            peternak.feedSide   = 0;
                            peternak.throwAnim  = 0.0f;
                            peternak.bendAngle  = 0.0f;
                            // Nonaktifkan feeding ayam baris ini & spawn telur
                            int idxL = row * 2;
                            int idxR = row * 2 + 1;
                            if (idxL < NUM_AYAM) {
                                ayamFeeding[idxL] = false; ayamBobs[idxL] = 0.0f;
                                eggs[idxL].visible = true; eggs[idxL].spawnTime = now + 0.1f;
                            }
                            if (idxR < NUM_AYAM) {
                                ayamFeeding[idxR] = false; ayamBobs[idxR] = 0.0f;
                                eggs[idxR].visible = true; eggs[idxR].spawnTime = now + 0.2f;
                            }
                            peternak.phase++;
                            peternak.arrived = false;
                        }
                    }

                    // Update bob ayam saat makan
                    int idxL = row * 2, idxR = row * 2 + 1;
                    if (idxL < NUM_AYAM && ayamFeeding[idxL])
                        ayamBobs[idxL] = 0.09f * sinf(el * 9.0f);
                    if (idxR < NUM_AYAM && ayamFeeding[idxR])
                        ayamBobs[idxR] = 0.09f * sinf(el * 9.0f + 1.2f);
                }
            }
            if (peternak.phase >= 100 + BARIS_AYAM) {
                // Semua baris selesai, kembali ke pintu
                peternak.scattering = false; peternak.feedSide = 0;
                peternak.throwAnim  = 0.0f;  peternak.bendAngle = 0.0f;
                peternak.phase = 50; peternak.arrived = false;
                peternak.phaseTimer = now;
            }
            if (peternak.phase == 50) {
                glm::vec3 pintu = {0.0f, 0.0f, 4.0f};
                float dist = glm::distance(peternak.pos, pintu);
                if (dist > 0.25f) {
                    glm::vec3 dir = glm::normalize(pintu - peternak.pos);
                    peternak.pos += dir * peternak.speed * deltaTime;
                    peternak.walkAnim += deltaTime * 6.5f;
                    peternak.rot = glm::degrees(atan2f(dir.x, -dir.z));
                    peternak.walking = true;
                } else {
                    peternak.pos = pintu; peternak.rot = 180.0f;
                    peternak.walking = false; peternak.phase = 0;
                    simState = SimState::IDLE;
                }
            }
            // Upload bob data ke GPU
            glBindBuffer(GL_ARRAY_BUFFER, vboBob);
            glBufferSubData(GL_ARRAY_BUFFER, 0, NUM_AYAM * sizeof(float), ayamBobs);
        }

        if (simState == SimState::HARVESTING && gerobak.active) {
            // Gerobak mengikuti peternak dengan smooth lag — posisi gerobak lerp ke target
            // Target gerobak: 1.0m di belakang peternak (arah lawanan rotasi)
            // Gerobak selalu di sisi kiri lorong (X=-1.2), Z lerp mengikuti peternak
// ── Fisika gerobak didorong ──────────────────────────────────────────────────
// Model: gerobak punya velocity sendiri. Saat peternak berjalan,
// posisi "anchor" (samping kiri peternak) menarik gerobak via gaya pegas.
// Saat peternak diam/ambil telur, gaya tarik mengecil sehingga gerobak
// perlahan berhenti karena gesekan — efeknya seperti benar2 didorong/ditahan.

auto updateGerobakPush = [&](bool peternak_berjalan) {
    // ── Target posisi gerobak: samping kiri peternak, sedikit di belakang ──
    float pRad  = glm::radians(peternak.rot);
    glm::vec3 pFwd   = { sinf(pRad), 0,  cosf(pRad) };
    glm::vec3 pRight = { cosf(pRad), 0, -sinf(pRad) };

    // Anchor: 0.85m ke kiri, 0.5m ke belakang peternak
    glm::vec3 anchor = peternak.pos
        + pRight * (-0.85f)
        + pFwd   * (-0.50f);
    anchor.y = 0.0f;

    // ── Gaya pegas: tarik gerobak ke anchor ──
    glm::vec3 toAnchor = anchor - gerobak.pos;
    toAnchor.y = 0.0f;
    float dist = glm::length(toAnchor);

    // Kekuatan tarik lebih besar saat peternak jalan, kecil saat diam
    float springK = peternak_berjalan ? 6.0f : 2.5f;

    if (dist > 0.01f) {
        glm::vec3 springForce = glm::normalize(toAnchor) * dist * springK;
        gerobak.velocity += springForce * deltaTime;
    }

    // ── Gesekan (drag) — gerobak tidak luncur terus ──
    float drag = peternak_berjalan ? 4.5f : 7.0f; // lebih banyak drag saat diam
    gerobak.velocity -= gerobak.velocity * drag * deltaTime;

    // Clamp kecepatan maksimum
    float spd = glm::length(gerobak.velocity);
    float maxSpd = gerobak.speed * 1.1f;
    if (spd > maxSpd)
        gerobak.velocity = glm::normalize(gerobak.velocity) * maxSpd;

    // Stop total kalau sudah sangat lambat dan anchor dekat
    if (spd < 0.05f && dist < 0.15f)
        gerobak.velocity = {0,0,0};

    // ── Terapkan velocity ke posisi ──
    glm::vec3 prevPos = gerobak.pos;
    gerobak.pos += gerobak.velocity * deltaTime;
    gerobak.pos.y = 0.0f;

    float spd2 = glm::length(gerobak.velocity);
    gerobak.beingPushed = (spd2 > 0.5f);

    // ── Guncangan vertikal kecil saat menggelinding ──
    if (spd2 > 0.3f) {
        gerobak.bumpPhase += deltaTime * 10.0f;
        gerobak.pos.y = sinf(gerobak.bumpPhase) * 0.010f * glm::min(spd2 * 0.2f, 1.0f);
    }

    // ── Wobble lateral (gerobak sedikit oleng) ──
    float wobTarget = sinf(now * 2.8f) * 0.015f * glm::min(spd2 * 0.25f, 1.0f);
    gerobak.lateralWobble = glm::mix(gerobak.lateralWobble, wobTarget,
                                     glm::clamp(4.0f * deltaTime, 0.0f, 1.0f));

    // ── Heading gerobak mengikuti arah gerak secara lambat ──
    if (spd2 > 0.4f) {
        glm::vec3 moveDir = glm::normalize(gerobak.velocity);
        float wantHeading = glm::degrees(atan2f(moveDir.x, moveDir.z));
        float hDiff = wantHeading - gerobak.heading;
        while (hDiff >  180.0f) hDiff -= 360.0f;
        while (hDiff < -180.0f) hDiff += 360.0f;
        gerobak.heading += hDiff * glm::clamp(4.0f * deltaTime, 0.0f, 1.0f);
    }

    // ── Roda berputar sesuai jarak tempuh ──
    float moved = glm::length(gerobak.pos - prevPos);
    float circ  = 2.0f * 3.14159f * 0.18f;
    float targetWheelVel = (moved / (circ * deltaTime + 1e-6f)) * 360.0f;
    gerobak.wheelVel = glm::mix(gerobak.wheelVel, targetWheelVel,
                                glm::clamp(5.0f * deltaTime, 0.0f, 1.0f));
    gerobak.wheelAngle += gerobak.wheelVel * deltaTime;
};

            if (peternak.phase >= 200 && peternak.phase < 200 + NUM_AYAM) {
                int idx = peternak.phase - 200;

                // Lewati telur yang tidak visible
                if (idx < NUM_AYAM && !eggs[idx].visible) {
                    peternak.phase++; peternak.arrived = false;
                    harvestAnim.subPhase = 0;
                    goto harvest_continue;
                }

                if (idx < NUM_AYAM && eggs[idx].visible) {
                    float el = now - harvestAnim.subTimer;

                    if (harvestAnim.subPhase == 0) {
                        // ─ FASE 0: Jalan menuju posisi di depan telur (di lorong tengah) ─
                        glm::vec3 target = {0.0f, 0.0f, eggs[idx].pos.z};
                        float dist = glm::distance(
                            glm::vec2(peternak.pos.x, peternak.pos.z),
                            glm::vec2(target.x, target.z));
                        if (dist > 0.18f) {
                            glm::vec3 dir = glm::normalize(target - peternak.pos);
                            peternak.pos += dir * peternak.speed * deltaTime;
                            peternak.walkAnim += deltaTime * 6.5f;
                            // Rotasi smooth ke arah jalan
                            float wantRot = glm::degrees(atan2f(dir.x, -dir.z));
                            float rotDiff = wantRot - peternak.rot;
                            // Normalize ke -180..180
                            while (rotDiff >  180.0f) rotDiff -= 360.0f;
                            while (rotDiff < -180.0f) rotDiff += 360.0f;
                            peternak.rot += rotDiff * glm::clamp(12.0f * deltaTime, 0.0f, 1.0f);
                            peternak.walking = true;
                            peternak.bendAngle = 0.0f;
                            harvestAnim.bendAngle = 0.0f;
                            harvestAnim.kneeBend  = 0.0f;
                            harvestAnim.liftAnim  = 0.0f;
                            harvestAnim.carryingEgg = false;
                            updateGerobakPush(peternak.walking);
                        } else {
                            // Tiba di depan telur — hadap ke telur (kiri atau kanan)
                            peternak.walking = false;
                            peternak.pos.x = 0.0f;
                            peternak.pos.z = eggs[idx].pos.z;
                            float side = (eggs[idx].pos.x < 0.0f) ? -1.0f : 1.0f;
                            peternak.rot = (side < 0.0f) ? -90.0f : 90.0f;
                            harvestAnim.subPhase = 1;
                            harvestAnim.subTimer = now;
                        }

                    } else if (harvestAnim.subPhase == 1) {
                        // ─ FASE 1: Membungkuk — badan + lutut turun (0.55 detik) ─
                        float t = glm::clamp(el / 0.55f, 0.0f, 1.0f);
                        float ease = t * t;  // ease-in
                        harvestAnim.bendAngle = glm::mix(0.0f, 52.0f, ease);
                        harvestAnim.kneeBend  = glm::mix(0.0f, 40.0f, ease);
                        harvestAnim.liftAnim  = 0.0f;
                        peternak.bendAngle    = harvestAnim.bendAngle;
                        peternak.walking = false;
                        updateGerobakPush(false);
                        if (t >= 1.0f) {
                            harvestAnim.subPhase = 2;
                            harvestAnim.subTimer = now;
                        }

                    } else if (harvestAnim.subPhase == 2) {
                        // ─ FASE 2: Tangan menjangkau ke bawah & mengambil telur (0.45 detik) ─
                        float t = glm::clamp(el / 0.45f, 0.0f, 1.0f);
                        harvestAnim.bendAngle = 52.0f;
                        harvestAnim.kneeBend  = 40.0f;
                        peternak.bendAngle    = harvestAnim.bendAngle;
                        // liftAnim = 0 → tangan di bawah menjangkau, 0.45 = sudah pegang
                        harvestAnim.liftAnim  = glm::mix(0.0f, 0.45f, t);
                        updateGerobakPush(false);
                        if (t >= 1.0f) {
                            eggs[idx].visible = false;
                            harvestAnim.carryingEgg = true;
                            harvestAnim.subPhase = 3;
                            harvestAnim.subTimer = now;
                        }

                    } else if (harvestAnim.subPhase == 3) {
                        // ─ FASE 3: Berdiri sambil pegang telur (0.5 detik) ─
                        float t = glm::clamp(el / 0.50f, 0.0f, 1.0f);
                        float ease = 1.0f - (1.0f - t) * (1.0f - t);  // ease-out
                        harvestAnim.bendAngle = glm::mix(52.0f, 0.0f, ease);
                        harvestAnim.kneeBend  = glm::mix(40.0f, 0.0f, ease);
                        peternak.bendAngle    = harvestAnim.bendAngle;
                        harvestAnim.liftAnim  = glm::mix(0.45f, 0.90f, ease); // tangan naik tinggi
                        harvestAnim.carryingEgg = true;
                        updateGerobakPush(false);
                        if (t >= 1.0f) {
                            harvestAnim.subPhase = 4;
                            harvestAnim.subTimer = now;
                        }

                    } else if (harvestAnim.subPhase == 4) {
                         // FASE 4: Berjalan ke sisi kanan gerobak sambil bawa telur
                        // Gerobak ada di X=-1.2, sisi kanannya ~X=-0.55
                        glm::vec3 cartSide = {gerobak.pos.x + 0.65f, 0.0f, gerobak.pos.z};
                        float dist = glm::distance(
                            glm::vec2(peternak.pos.x, peternak.pos.z),
                            glm::vec2(cartSide.x, cartSide.z));

                        harvestAnim.bendAngle = 0.0f;
                        peternak.bendAngle    = 0.0f;
                        harvestAnim.liftAnim  = 0.90f;
                        harvestAnim.carryingEgg = true;

                        if (dist > 0.20f) {
                            glm::vec3 dir = glm::normalize(cartSide - peternak.pos);
                            peternak.pos += dir * (peternak.speed * 0.65f) * deltaTime;
                            peternak.walkAnim += deltaTime * 5.0f;
                            float wantRot = glm::degrees(atan2f(dir.x, -dir.z));
                            float rotDiff = wantRot - peternak.rot;
                            while (rotDiff >  180.0f) rotDiff -= 360.0f;
                            while (rotDiff < -180.0f) rotDiff += 360.0f;
                            peternak.rot += rotDiff * glm::clamp(10.0f * deltaTime, 0.0f, 1.0f);
                            peternak.walking = true;
                        } else {
                            peternak.walking = false;
                            harvestAnim.subPhase = 5;
                            harvestAnim.subTimer = now;
                        }

                    } else if (harvestAnim.subPhase == 5) {
                        // ─ FASE 5: Taruh telur ke gerobak — tangan turun ke bak (0.5 detik) ─
                        float t = glm::clamp(el / 0.50f, 0.0f, 1.0f);
                        // Badan sedikit membungkuk ke bak gerobak
                        harvestAnim.bendAngle = glm::mix(0.0f, 25.0f, sinf(t * 3.14159f));
                        harvestAnim.kneeBend  = glm::mix(0.0f, 12.0f, sinf(t * 3.14159f));
                        peternak.bendAngle    = harvestAnim.bendAngle;
                        // liftAnim: tangan turun ke bak (0.90 → 0.10) lalu kembali (0.10 → 0.0)
                        if (t < 0.5f) {
                            harvestAnim.liftAnim = glm::mix(0.90f, 0.10f, t * 2.0f);
                        } else {
                            harvestAnim.liftAnim = glm::mix(0.10f, 0.0f, (t - 0.5f) * 2.0f);
                            harvestAnim.carryingEgg = (t < 0.55f); // telur dilepas di tengah
                        }

                        // Spawn animasi telur terbang saat baru masuk fase 5
if (el < deltaTime * 2.0f && !gerobak.eggFlying) {
    // Posisi tangan peternak (estimasi: depan-atas sedikit)
    float rotRad = glm::radians(peternak.rot);
    glm::vec3 handOff = {sinf(rotRad)*0.3f, 0.9f, cosf(rotRad)*0.3f};
    gerobak.eggFlyFrom = peternak.pos + handOff;
    gerobak.eggFlyT    = 0.0f;
    gerobak.eggFlying  = true;
}
// Update animasi terbang
if (gerobak.eggFlying) {
    gerobak.eggFlyT += deltaTime / gerobak.eggFlyDur;
    if (gerobak.eggFlyT >= 1.0f) {
        gerobak.eggFlyT  = 1.0f;
        gerobak.eggFlying = false;
        gerobak.eggsHarvested++;
    }
}
                        peternak.walking = false;
                        updateGerobakPush(false);
                        if (t >= 1.0f) {
                            gerobak.eggsHarvested++;
                            harvestAnim.carryingEgg = false;
                            harvestAnim.liftAnim = 0.0f;
                            harvestAnim.bendAngle = 0.0f;
                            harvestAnim.kneeBend  = 0.0f;
                            peternak.bendAngle = 0.0f;
                            // Peternak kembali ke lorong tengah, lanjut ke telur berikut
                            peternak.phase++;
                            harvestAnim.subPhase = 0;
                            harvestAnim.subTimer = now;
                        }
                    }
                }
            }

            harvest_continue:
            if (peternak.phase >= 200 + NUM_AYAM && !harvestDone) {
                harvestDone = true; gerobak.returning = true;
                harvestAnim.carryingEgg = false;
                harvestAnim.liftAnim = 0.0f;
                harvestAnim.bendAngle = 0.0f;
                harvestAnim.kneeBend  = 0.0f;
                peternak.bendAngle = 0.0f;
            }
           if (gerobak.returning) {
    // ── Peternak berjalan ke pintu, gerobak ditarik pegas di sisinya ──
    peternak.walking = true;
    peternak.walkAnim += deltaTime * 6.5f;
    peternak.pos.z += gerobak.speed * deltaTime;

    // Peternak smooth hadap keluar (rot → 0)
    float rotDiffR = -peternak.rot;
    while (rotDiffR >  180.0f) rotDiffR -= 360.0f;
    while (rotDiffR < -180.0f) rotDiffR += 360.0f;
    peternak.rot += rotDiffR * glm::clamp(6.0f * deltaTime, 0.0f, 1.0f);

    // Anchor gerobak: samping kiri peternak, sedikit di belakang
    float pRR  = glm::radians(peternak.rot);
    glm::vec3 pFwdR   = { sinf(pRR), 0, cosf(pRR) };
    glm::vec3 pRightR = { cosf(pRR), 0,-sinf(pRR) };
    glm::vec3 anchorR = peternak.pos + pRightR*(-0.85f) + pFwdR*(-0.50f);
    anchorR.y = 0.0f;

    // Gaya pegas kuat saat returning (peternak dorong aktif)
    glm::vec3 toAnchorR = anchorR - gerobak.pos;
    toAnchorR.y = 0.0f;
    float distR = glm::length(toAnchorR);
    if (distR > 0.01f)
        gerobak.velocity += glm::normalize(toAnchorR) * distR * 8.0f * deltaTime;

    // Drag
    gerobak.velocity -= gerobak.velocity * 5.0f * deltaTime;
    float spdR = glm::length(gerobak.velocity);
    if (spdR > gerobak.speed * 1.1f)
        gerobak.velocity = glm::normalize(gerobak.velocity) * gerobak.speed * 1.1f;

    glm::vec3 prevPosR = gerobak.pos;
    gerobak.pos += gerobak.velocity * deltaTime;
    gerobak.pos.y = 0.0f;

    // Heading gerobak mengikuti gerak
    if (spdR > 0.4f) {
        glm::vec3 mD = glm::normalize(gerobak.velocity);
        float wH = glm::degrees(atan2f(mD.x, mD.z));
        float hd = wH - gerobak.heading;
        while (hd >  180.0f) hd -= 360.0f;
        while (hd < -180.0f) hd += 360.0f;
        gerobak.heading += hd * glm::clamp(4.0f * deltaTime, 0.0f, 1.0f);
    }

    // Guncangan dan roda
    if (spdR > 0.3f) {
        gerobak.bumpPhase += deltaTime * 10.0f;
        gerobak.pos.y = sinf(gerobak.bumpPhase) * 0.009f;
    }
    float movedR = glm::length(gerobak.pos - prevPosR);
    float circR  = 2.0f * 3.14159f * 0.18f;
    float twvR   = (movedR / (circR * deltaTime + 1e-6f)) * 360.0f;
    gerobak.wheelVel = glm::mix(gerobak.wheelVel, twvR, glm::clamp(5.0f*deltaTime,0.f,1.f));
    gerobak.wheelAngle += gerobak.wheelVel * deltaTime;

    if (peternak.pos.z >= 5.5f) {
        gerobak.active    = false;
        gerobak.velocity  = {0,0,0};
        gerobak.wheelVel  = 0.0f;
        peternak.walking  = false;
        peternak.phase    = 0;
        peternak.arrived  = false;
        simState = SimState::IDLE;
    }
}
        }

        // ──────────────────────────────────────────────────
        //  MATRIKS CAHAYA (untuk shadow map)
        // ──────────────────────────────────────────────────
        float sceneRadius  = glm::max(TOTAL_W, TOTAL_D) * 0.7f;
        float lightDist    = sceneRadius * 2.0f;
        glm::vec3 lightPos = -dayNight.lightDir * lightDist;
        glm::vec3 sceneCenter = {0.0f, 0.0f, -(TOTAL_D * 0.5f)};

        glm::mat4 lightView = glm::lookAt(lightPos + sceneCenter, sceneCenter, glm::vec3(0,1,0));
        glm::mat4 lightProj = glm::ortho(
            -sceneRadius, sceneRadius,
            -sceneRadius, sceneRadius,
            0.1f, lightDist * 2.5f);
        glm::mat4 lightSpaceMat = lightProj * lightView;

        // ──────────────────────────────────────────────────
        //  PASS 1: RENDER KE SHADOW MAP
        // ──────────────────────────────────────────────────
        glViewport(0, 0, SHADOW_W, SHADOW_H);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        // Offset untuk mengurangi shadow acne
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        // Scene statis (depth pass)
        glUseProgram(progDepth);
        setMat4(progDepth, "lightSpaceMatrix", lightSpaceMat);
        drawScene(S, progDepth, now, false);

        // Gerobak (depth pass) — hanya saat aktif
        if (gerobak.active) {
            drawGerobak(progDepth, mBox, mCyl, gerobak.pos, gerobak.wheelAngle, gerobak.heading, gerobak.lateralWobble);
        }

        // Peternak (depth pass)
        drawPeternak(progDepth, mBox, mCyl, mSphere,
            peternak.pos, peternak.rot,
            peternak.walking ? peternak.walkAnim : 0.0f, peternak.bendAngle,
            {0.22f,0.48f,0.18f}, {0.88f,0.68f,0.48f}, {0.20f,0.20f,0.35f},
            peternak.throwAnim, peternak.feedSide, peternak.scattering,
            harvestAnim.kneeBend, harvestAnim.liftAnim, harvestAnim.carryingEgg);

        // Telur (depth pass)
        for (int i = 0; i < NUM_AYAM; i++) {
            if (!eggs[i].visible) continue;
            float sc = glm::clamp((now - eggs[i].spawnTime) / 0.25f, 0.0f, 1.0f);
            glm::mat4 M = glm::translate(glm::mat4(1), eggs[i].pos);
            M = glm::scale(M, {sc*0.9f, sc*1.2f, sc*0.9f});
            drawDepth(mTelur, progDepth, M);
        }

        // Ayam instanced (depth pass)
        // Upload bob data (sudah diupdate di fase FEEDING)
        glUseProgram(progDepthInst);
        setMat4(progDepthInst, "lightSpaceMatrix", lightSpaceMat);
        glm::mat4 bodyOffset = TRS({0, 0.06f, 0}, {0.16f, 0.13f, 0.22f});
        setMat4(progDepthInst, "model", bodyOffset);
        glBindVertexArray(mAyam.VAO);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mAyam.indexCount, GL_UNSIGNED_INT, nullptr, NUM_AYAM);
        glBindVertexArray(0);

        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // ──────────────────────────────────────────────────
        //  PASS 2: COLOR PASS (scene normal dengan bayangan)
        // ──────────────────────────────────────────────────
        glViewport(0, 0, SCR_W, SCR_H);
        glClearColor(dayNight.skyColor.r, dayNight.skyColor.g, dayNight.skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 proj = glm::perspective(glm::radians(55.0f), (float)SCR_W/SCR_H, 0.1f, 500.0f);

        // Bind shadow map ke slot 1
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depthTex);

        // Helper: set uniform umum untuk progStd dan progInst
        auto setupLitShader = [&](unsigned int p) {
            glUseProgram(p);
            setMat4(p, "view", view);
            setMat4(p, "projection", proj);
            setMat4(p, "lightSpaceMatrix", lightSpaceMat);
            setVec3(p, "lightDir",   dayNight.lightDir);
            setVec3(p, "lightColor", dayNight.lightColor);
            setVec3(p, "viewPos",    cameraPos);
            setFloat(p, "ambientStr",    dayNight.ambientStr);
            setFloat(p, "shadowStrength", dayNight.shadowStrength);
            
            float nightness = 1.0f - glm::clamp(-dayNight.lightDir.y, 0.0f, 1.0f);
            setFloat(p, "nightBlend", nightness);
            
            // Lentera peternak (selalu menyala)
            glm::vec3 lanternPos = peternak.pos + glm::vec3(0.0f, 0.8f, 0.0f);
            setVec3(p, "pointLights[0].pos",   lanternPos);
            setVec3(p, "pointLights[0].color", glm::vec3(1.0f, 0.65f, 0.22f) * (1.0f + nightness * 1.2f));
            setFloat(p, "pointLights[0].isOn", 1.0f);

            // Lampu kandang (5 buah) — nyala saat malam
            int numLampu = 5;
            float lampsOn = nightness > 0.25f ? 1.0f : 0.0f;
            float lampIntensity = 1.0f + nightness * 2.5f;
            for (int i = 1; i <= numLampu && i < 8; i++) {
                char bPos[64], bCol[64], bAct[64];
                snprintf(bPos, sizeof(bPos), "pointLights[%d].pos", i);
                snprintf(bCol, sizeof(bCol), "pointLights[%d].color", i);
                snprintf(bAct, sizeof(bAct), "pointLights[%d].isOn", i);
                float lZ = -(i * (TOTAL_D / (numLampu + 1.0f)));
                setVec3(p, bPos, glm::vec3(0.0f, TINGGI_DINDING + 0.10f, lZ));
                setVec3(p, bCol, glm::vec3(1.0f, 0.82f, 0.44f) * lampIntensity);
                setFloat(p, bAct, lampsOn);
            }
            // Nonaktifkan sisa slot lampu
            for (int i = numLampu + 1; i < 8; i++) {
                char bAct[64]; snprintf(bAct, sizeof(bAct), "pointLights[%d].isOn", i);
                setFloat(p, bAct, 0.0f);
            }

            setInt(p, "shadowMap", 1);    // slot 1
            setInt(p, "texDiffuse", 0);   // slot 0 (diisi draw() per-objek)
        };

        setupLitShader(progStd);
        S.nightness = 1.0f - glm::clamp(-dayNight.lightDir.y, 0.0f, 1.0f);
        drawScene(S, progStd, now, true);

        // Gerobak — hanya digambar saat aktif
        if (gerobak.active) {
            glUseProgram(progStd);
            setupLitShader(progStd);
            drawGerobak(progStd, mBox, mCyl, gerobak.pos, gerobak.wheelAngle, gerobak.heading, gerobak.lateralWobble);
        }

        // Telur di gerobak — tersusun di dalam bak, ikut posisi gerobak
        if (gerobak.eggsHarvested > 0) {
            glm::vec3 gPos = gerobak.active ? gerobak.pos : glm::vec3{-1.2f,0.0f,3.5f};
            // Bak gerobak: panjang 0.80 (Z), lebar 0.56 (X), deck Y ≈ 0.38
            // Telur disusun dalam grid di dalam bak: 4 kolom x 6 baris per lapis
            int numCols = 4;
            int numRows = 6;
            int maxShow = std::min(gerobak.eggsHarvested, numCols * numRows * 3);
            for (int e = 0; e < maxShow; e++) {
                int lapis  = e / (numCols * numRows);
                int eLocal = e % (numCols * numRows);
                int col    = eLocal % numCols;
                int row    = eLocal / numCols;
                // Distribusi di dalam bak (sedikit offset acak deterministik)
                float jitterX = ((e * 17 + 5) % 7 - 3) * 0.005f;
                float jitterZ = ((e * 13 + 3) % 7 - 3) * 0.005f;
                float ex = (col - (numCols - 1) * 0.5f) * 0.115f + jitterX;
                float ez = (row - (numRows - 1) * 0.5f) * 0.115f + jitterZ;
                float ey = 0.395f + lapis * 0.095f;  // di atas deck gerobak
                glm::vec3 eggWorld = gPos + glm::vec3{ex, ey, ez};
                // Sedikit kemiringan bervariasi biar terlihat natural
                float tiltX = ((e * 7  + 2) % 5 - 2) * 4.0f;
                float tiltZ = ((e * 11 + 1) % 5 - 2) * 4.0f;
                glm::mat4 M = glm::translate(glm::mat4(1), eggWorld);
                M = glm::rotate(M, glm::radians(tiltX), {1,0,0});
                M = glm::rotate(M, glm::radians(tiltZ), {0,0,1});
                M = glm::scale(M, {0.072f, 0.095f, 0.072f});
                draw(mTelur, progStd, M, cTelurCol);
            }
            // ── Telur sedang terbang ke gerobak ──
if (gerobak.eggFlying && gerobak.active) {
    float t = gerobak.eggFlyT;
    // Target: di atas bak gerobak
    glm::vec3 eggTarget = gerobak.pos + glm::vec3{0, 0.5f, 0};
    // Interpolasi parabola: lerp posisi + arc naik-turun
    glm::vec3 eggPos = glm::mix(gerobak.eggFlyFrom, eggTarget, t);
    eggPos.y += sinf(t * glm::pi<float>()) * 0.7f; // arc parabola
    // Rotasi telur saat terbang
    float eggRot = t * 360.0f;
    glm::mat4 M = glm::translate(glm::mat4(1), eggPos);
    M = glm::rotate(M, glm::radians(eggRot), {1, 0.5f, 0});
    M = glm::scale(M, {0.09f, 0.12f, 0.09f});
    draw(mTelur, progStd, M, cTelurCol);
}
        }

        // Peternak
        glUseProgram(progStd);
        setupLitShader(progStd);
        drawPeternak(progStd, mBox, mCyl, mSphere,
            peternak.pos, peternak.rot,
            peternak.walking ? peternak.walkAnim : 0.0f, peternak.bendAngle,
            {0.22f,0.48f,0.18f}, {0.88f,0.68f,0.48f}, {0.20f,0.20f,0.35f},
            peternak.throwAnim, peternak.feedSide, peternak.scattering,
            harvestAnim.kneeBend, harvestAnim.liftAnim, harvestAnim.carryingEgg);

        // Telur di lantai
        glUseProgram(progStd);
        setupLitShader(progStd);
        for (int i = 0; i < NUM_AYAM; i++) {
            if (!eggs[i].visible) continue;
            float sc = glm::clamp((now - eggs[i].spawnTime) / 0.25f, 0.0f, 1.0f);
            glm::mat4 M = glm::translate(glm::mat4(1), eggs[i].pos);
            M = glm::scale(M, {sc*0.9f, sc*1.2f, sc*0.9f});
            draw(mTelur, progStd, M, cTelurCol);
        }

        // ──────────────────────────────────────────────────
        //  AYAM — HARDWARE INSTANCING (diperbaiki)
        //
        //  Strategi instancing yang benar:
        //  Bagian-bagian yang posisinya RELATIF ke base position
        //  dan hanya butuh offset (bodi, kepala, ekor) → instanced.
        //  Bagian yang butuh rotasi/transformasi berbeda per-instance
        //  (kaki dengan animasi walking, jengger, paruh) → loop manual.
        //
        //  Di sini kita gambar bodi+sayap+leher+kepala via instancing,
        //  lalu detail kecil (paruh, mata, jengger, pial, kaki) via loop.
        // ──────────────────────────────────────────────────
        setupLitShader(progInst);
        setInt(progInst, "shadowMap", 1);

        // Bodi utama (ellipsoid) — instanced
        setVec3(progInst, "objectColor", cAyamCol);
        bodyOffset = TRS({0, 0.06f, 0}, {0.16f, 0.13f, 0.22f});
        setMat4(progInst, "model", bodyOffset);
        glBindVertexArray(mAyam.VAO);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mAyam.indexCount, GL_UNSIGNED_INT, nullptr, NUM_AYAM);
        glBindVertexArray(0);

        // Kepala tidak digambar dengan instancing karena butuh animasi anggukan (nod).
        // Akan digambar di dalam loop manual.

        // Ekor — instanced (3 variasi posisi)
        for (int e = 0; e < 3; e++) {
            float exo  = (e - 1) * 0.055f;
            float eang = -38.0f - e * 9.0f;
            glm::mat4 ekorM = glm::translate(glm::mat4(1), {exo, 0.10f, -0.19f});
            ekorM = glm::rotate(ekorM, glm::radians(eang), {1,0,0});
            ekorM = glm::scale(ekorM, {0.055f, 0.14f, 0.065f});
            setMat4(progInst, "model", ekorM);
            setVec3(progInst, "objectColor",
                    {cAyamCol.r*0.72f, cAyamCol.g*0.70f, cAyamCol.b*0.58f});
            glBindVertexArray(mAyamEkor.VAO);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mAyamEkor.indexCount, GL_UNSIGNED_INT, nullptr, NUM_AYAM);
            glBindVertexArray(0);
        }

        // Detail kecil (paruh, mata, jengger, kaki) → loop manual (hemat,
        // karena detail ini kecil dan jumlah draw call-nya masih terkelola)
        glUseProgram(progStd);
        setupLitShader(progStd);
        for (int i = 0; i < NUM_AYAM; i++) {
            float bob = ayamBobs[i];
            // Animasi patuk lebih halus
            float nod = ayamFeeding[i] ? (22.0f * sinf(now * 10.0f + i * 0.7f)) : 0.0f;
            if (!ayamFeeding[i]) {
                // Patuk acak lebih natural: 2 step patuk cepat lalu jeda
                float peckCycle = fmodf(now * 0.85f + i * 1.37f, 7.0f);
                if (peckCycle < 0.35f) {
                    nod = 28.0f * sinf(peckCycle * 3.1415f / 0.35f);
                    if (nod < 0.0f) nod = 0.0f;
                } else if (peckCycle > 0.55f && peckCycle < 0.90f) {
                    nod = 20.0f * sinf((peckCycle - 0.55f) * 3.1415f / 0.35f);
                    if (nod < 0.0f) nod = 0.0f;
                }
                // Gerak kepala kecil ke kiri/kanan (look around)
                float headTilt = 8.0f * sinf(now * 0.6f + i * 2.1f);
                nod += headTilt * 0.2f;
            }
            glm::vec3 bp = ayamBasePos[i] + glm::vec3{0, bob, 0};
            float rotY = (ayamBasePos[i].x < 0.0f) ? 90.0f : -90.0f;
            // Sedikit variasi rotasi per ayam supaya tidak seragam
            rotY += 5.0f * sinf(i * 1.73f + now * 0.08f);
            glm::mat4 rootAyam = glm::translate(glm::mat4(1), bp);
            rootAyam = glm::rotate(rootAyam, glm::radians(rotY), {0,1,0});

            // ── Kaki — lebih realistis dengan animasi idle dan paha ──
            float idleStep = 3.5f * sinf(now * 1.8f + i * 2.4f); // goyang berat badan idle
            for (int side : {-1,1}) {
                float sx = side * 0.075f;
                // Ayunan kaki saat feeding
                float legSwing = ayamFeeding[i] ? (6.0f * sinf(now * 8.0f + i + side * 3.14f)) : idleStep * side;
                // Paha atas
                glm::mat4 hipM = rootAyam * glm::translate(glm::mat4(1), {sx, -0.04f, 0.04f});
                hipM = glm::rotate(hipM, glm::radians(-18.0f + legSwing), {1,0,0});
                draw(mCyl, progStd, hipM * TRS({0,-0.07f,0}, {0.032f,0.14f,0.032f}), cKakiAy);
                // Lutut
                glm::mat4 kneeM = hipM * glm::translate(glm::mat4(1), {0,-0.14f,0});
                kneeM = glm::rotate(kneeM, glm::radians(32.0f - legSwing * 0.4f), {1,0,0});
                draw(mCyl, progStd, kneeM * TRS({0,-0.07f,0}, {0.026f,0.14f,0.026f}), cKakiAy);
                // Telapak / jari kaki
                glm::mat4 footM = kneeM * glm::translate(glm::mat4(1), {0,-0.14f,0.04f});
                // Jari depan (3 jari)
                for (int toe = -1; toe <= 1; toe++) {
                    float toeAng = toe * 18.0f;
                    glm::mat4 toeM = footM;
                    toeM = glm::rotate(toeM, glm::radians(toeAng), {0,1,0});
                    draw(mBox, progStd, toeM * TRS({0,-0.008f,0.055f}, {0.012f,0.012f,0.08f}), cKakiAy);
                }
                // Jari belakang (1 jari)
                draw(mBox, progStd, footM * TRS({0,-0.008f,-0.032f}, {0.012f,0.012f,0.048f}), cKakiAy);
            }

            // ── Leher + kepala local (untuk nod animation) ──
            glm::mat4 neckM = rootAyam * TRS({0, 0.14f, 0.13f});
            neckM = glm::rotate(neckM, glm::radians(15.0f + nod * 0.25f), {1,0,0});
            draw(mAyamKep, progStd, neckM * glm::scale(glm::mat4(1), {0.09f,0.16f,0.09f}),
                 {cKrem.r*0.90f, cKrem.g*0.86f, cKrem.b*0.76f});
            glm::mat4 headM = neckM * TRS({0, 0.15f, 0.03f});
            headM = glm::rotate(headM, glm::radians(nod), {1,0,0});
            // Sedikit tilt kepala kiri-kanan (ayam sering memiringkan kepala)
            float headTiltZ = 12.0f * sinf(now * 0.5f + i * 1.9f);
            headM = glm::rotate(headM, glm::radians(headTiltZ), {0,0,1});
            
            // Kepala utama — sedikit lebih bulat & besar
            draw(mAyamKep, progStd, headM * glm::scale(glm::mat4(1), {0.17f, 0.16f, 0.155f}), cKrem);

            // Paruh — 2 bagian (atas & bawah) supaya terlihat terbuka sedikit saat makan
            float beakOpen = ayamFeeding[i] ? (4.0f * fabsf(sinf(now * 10.0f + i))) : 0.5f;
            draw(mBox, progStd, headM * TRS({0, -0.015f + beakOpen*0.003f, 0.092f}, {0.048f,0.030f,0.078f}), cParuhAy);
            draw(mBox, progStd, headM * TRS({0, -0.048f - beakOpen*0.003f, 0.082f}, {0.040f,0.024f,0.058f}), cParuhAy);

            // Mata — lebih detail (bola mata + sorot putih kecil)
            for (int s : {-1,1}) {
                // Bola mata
                draw(mAyamKep, progStd, headM * TRS({s*0.070f, 0.022f, 0.078f}, {0.032f,0.032f,0.026f}), {0.04f,0.04f,0.04f});
                // Iris (warna oranye/coklat)
                draw(mAyamKep, progStd, headM * TRS({s*0.074f, 0.023f, 0.088f}, {0.018f,0.018f,0.014f}), {0.65f,0.32f,0.06f});
                // Sorot putih kecil
                draw(mAyamKep, progStd, headM * TRS({s*0.076f, 0.030f, 0.092f}, {0.007f,0.007f,0.007f}), {0.95f,0.95f,0.95f});
                // Kelopak mata (garis tipis)
                draw(mBox, progStd, headM * TRS({s*0.070f, 0.033f, 0.079f}, {0.032f,0.005f,0.018f}), {cAyamCol.r*0.85f, cAyamCol.g*0.82f, cAyamCol.b*0.65f});
            }

            // Jengger — lebih besar dan berlekuk (3 tonjolan bervariasi)
            for (int j = 0; j < 3; j++) {
                float jx = (j-1) * 0.024f;
                float jh = (j == 1) ? 0.085f : (j == 0 ? 0.058f : 0.065f);
                float jz = (j == 1) ? -0.005f : 0.010f;
                // Tonjolan bergoyang sedikit (wattle effect)
                float wobble = 2.0f * sinf(now * 3.0f + i + j);
                draw(mAyamKep, progStd, headM * TRS({jx, 0.082f + jh*0.3f + wobble*0.001f, jz}, {0.030f, jh, 0.028f}), cJengger);
            }

            // Pial — lebih besar, menggantung di bawah paruh
            for (int s : {-1,1}) {
                float wob = 3.0f * sinf(now * 2.5f + i * 0.8f);
                draw(mAyamKep, progStd, headM * TRS({s*0.018f, -0.078f + wob*0.001f, 0.052f}, {0.026f,0.050f,0.026f}), cPialAy);
                draw(mAyamKep, progStd, headM * TRS({s*0.020f, -0.115f + wob*0.002f, 0.040f}, {0.018f,0.030f,0.018f}), cPialAy);
            }
                
            // ── Sayap Ayam — lebih panjang dan natural ──
            for (int s : {-1,1}) {
                float baseAng = ayamFeeding[i] 
                    ? (15.0f + 30.0f * sinf(now * 14.0f + i)) 
                    : (8.0f + 4.0f * sinf(now * 1.8f + i * 0.9f));
                glm::mat4 wingRoot = rootAyam * TRS({s * 0.155f, 0.065f, -0.02f});
                wingRoot = glm::rotate(wingRoot, glm::radians(s * baseAng), {0,0,1});
                wingRoot = glm::rotate(wingRoot, glm::radians(-10.0f), {1,0,0});
                // Sayap atas (scapular)
                draw(mAyamKep, progStd, wingRoot * glm::scale(glm::mat4(1), {0.055f,0.12f,0.20f}), 
                     {cAyamCol.r*0.94f, cAyamCol.g*0.92f, cAyamCol.b*0.80f});
                // Bulu sayap primer (lebih panjang, sedikit lebih gelap)
                glm::mat4 wingTip = wingRoot * glm::translate(glm::mat4(1), {0,-0.07f,0.05f});
                wingTip = glm::rotate(wingTip, glm::radians(s*8.0f), {0,0,1});
                draw(mAyamKep, progStd, wingTip * glm::scale(glm::mat4(1), {0.045f,0.10f,0.24f}), 
                     {cAyamCol.r*0.82f, cAyamCol.g*0.78f, cAyamCol.b*0.60f});
            }
        }

        // ── Title bar update ──
        std::string ttl = "Simulasi Peternakan v4 | F=Pakan  H=Panen  WASD+QE=Gerak  Scroll=Putar";
        if (simState == SimState::FEEDING)    ttl += "  [MEMBERI PAKAN...]";
        if (simState == SimState::HARVESTING) ttl += "  [PANEN TELUR...]";
        // Tunjukkan waktu hari
        {
            float dayPct = fmodf(now / 120.0f, 1.0f) * 24.0f;
            int h = (int)dayPct, m = (int)((dayPct - h) * 60);
            char timeBuf[32]; snprintf(timeBuf, 32, "  [%02d:%02d]", h, m);
            ttl += timeBuf;
        }
        glfwSetWindowTitle(win, ttl.c_str());

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}