// ============================================================
//  Simulasi Peternakan Ayam Petelur  v3
//  OpenGL 3.3 Core Profile | C++17 | CMake
//
//  KONTROL:
//   W/A/S/D          : gerak kamera
//   Q / E            : naik / turun
//   Klik Kanan+Drag  : putar pandangan
//   Scroll           : zoom
//   F                : peternak berjalan + beri pakan, telur muncul
//   H                : peternak bawa gerobak, panen telur
//   ESC              : keluar
// ============================================================

#include "Mesh.h"
#include "Shapes.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>

// ──────────────────────────────────────────────────────────
//  KONSTANTA
// ──────────────────────────────────────────────────────────
constexpr int   SCR_W      = 1280;
constexpr int   SCR_H      = 720;
constexpr int   NUM_AYAM   = 50;

// Grid: 25 baris x 1 kolom per sisi (kiri + kanan = 50)
constexpr int   BARIS_AYAM = 25;
constexpr int   KOLOM_AYAM = 1;
constexpr float JARAK_X    = 1.0f;    
constexpr float JARAK_Z    = 0.60f;   // jarak antar baris (Z) — lebih rapat

constexpr float LORONG_W   = 2.4f;    // lebar lorong tengah
constexpr float OFFSET_SISI = LORONG_W * 0.5f + 0.3f; 
constexpr float HALF_W     = OFFSET_SISI + 1.2f; // Diperlebar agar ekor ayam tidak nembus
constexpr float TOTAL_W    = HALF_W * 2.0f;
constexpr float TOTAL_D    = BARIS_AYAM * JARAK_Z + 1.5f;

constexpr int   JML_SEKSI      = 5;
constexpr float TINGGI_DINDING = 2.5f;
constexpr float ROOF_PITCH_DEG = 20.0f;  // sudut kemiringan atap (derajat)

// ──────────────────────────────────────────────────────────
//  KAMERA
// ──────────────────────────────────────────────────────────
glm::vec3 cameraPos   = {0.0f, 8.0f, 20.0f};
glm::vec3 cameraFront = {0.0f, -0.30f, -1.0f};
glm::vec3 cameraUp    = {0.0f,  1.0f,  0.0f};

float yaw = -90.0f, pitch = -12.5f;
float deltaTime = 0.0f, lastFrame = 0.0f;
bool  mouseRight = false;
double lastMX = 0, lastMY = 0;

// ──────────────────────────────────────────────────────────
//  STATE
// ──────────────────────────────────────────────────────────
enum class SimState { IDLE, FEEDING, HARVESTING };
SimState simState = SimState::IDLE;

struct EggInfo { bool visible=false; float spawnTime=0.0f; glm::vec3 pos; };
EggInfo eggs[NUM_AYAM];

float ayamBobs[NUM_AYAM];
float ayamBobPhase[NUM_AYAM];
bool  ayamFeeding[NUM_AYAM];
float feedStartTime = 0.0f;
const float FEED_DUR = 6.0f;

// ──────────────────────────────────────────────────────────
//  PETERNAK
// ──────────────────────────────────────────────────────────
struct Peternak {
    glm::vec3 pos      = {0.6f, 0.0f, 3.8f};  // selalu tampil di dekat pintu
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
} peternak;

// ──────────────────────────────────────────────────────────
//  GEROBAK
// ──────────────────────────────────────────────────────────
struct Gerobak {
    glm::vec3 pos       = {-1.2f, 0.0f, 3.5f};  // diparkir di kiri pintu masuk
    bool      active    = false;
    bool      returning = false;
    float     targetZ   = -(TOTAL_D - 1.5f);
    float     speed     = 4.5f;
    float     wheelAngle= 0.0f;
    int       eggsHarvested = 0;
} gerobak;
bool harvestDone = false;

// ──────────────────────────────────────────────────────────
//  POSISI AYAM
// ──────────────────────────────────────────────────────────
glm::vec3 ayamBasePos[NUM_AYAM];

void initAyamPositions() {
    int idx = 0;
    for (int b = 0; b < BARIS_AYAM && idx < NUM_AYAM; b++) {
        float z = -(b * JARAK_Z + 1.0f);
        // Kiri
        ayamBasePos[idx++] = {-OFFSET_SISI, 0.22f, z};
        if(idx < NUM_AYAM){
            // Kanan
            ayamBasePos[idx++] = { OFFSET_SISI, 0.22f, z};
        }
    }
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
        // Reset semua ayam & telur
        for (int i = 0; i < NUM_AYAM; i++) {
            ayamFeeding[i]  = false;
            ayamBobPhase[i] = ((float)rand() / RAND_MAX) * 6.28f;
            ayamBobs[i]     = 0.0f;
            eggs[i].visible   = false;
            eggs[i].spawnTime = now + 0.5f;
        }
        // Init peternak eksplisit per-field (hindari bug aggregate init)
        peternak.pos        = {0.0f, 0.0f, 4.0f};
        peternak.rot        = 180.0f;
        peternak.walkAnim   = 0.0f;
        peternak.bendAngle  = 0.0f;
        peternak.walking    = true;
        peternak.targetZ    = 0.0f;
        peternak.speed      = 5.0f;
        peternak.phase      = 100;    // FSM: fase 100+i = beri pakan ke ayam ke-i
        peternak.phaseTimer = now;
        peternak.currentChickenIdx     = 0;
        peternak.totalChickenProcessed = 0;
        peternak.arrived    = false;
    }

    if (key == GLFW_KEY_H && simState == SimState::IDLE) {
        simState    = SimState::HARVESTING;
        harvestDone = false;
        // Init gerobak
        gerobak.pos          = {0.0f, 0.0f, 4.0f};
        gerobak.active       = true;
        gerobak.returning    = false;
        gerobak.targetZ      = -(TOTAL_D - 1.5f);
        gerobak.speed        = 4.5f;
        gerobak.wheelAngle   = 0.0f;
        gerobak.eggsHarvested = 0;
        // Init peternak eksplisit per-field
        peternak.pos        = {0.6f, 0.0f, 4.0f};
        peternak.rot        = 180.0f;
        peternak.walkAnim   = 0.0f;
        peternak.bendAngle  = 0.0f;
        peternak.walking    = true;
        peternak.targetZ    = 0.0f;
        peternak.speed      = 5.0f;
        peternak.phase      = 200;    // FSM: fase 200+i = ambil telur ke-i
        peternak.phaseTimer = now;
        peternak.currentChickenIdx     = 0;
        peternak.totalChickenProcessed = 0;
        peternak.arrived    = false;
    }
}

void mouse_button_callback(GLFWwindow* win, int btn, int action, int) {
    if (btn == GLFW_MOUSE_BUTTON_RIGHT) {
        mouseRight = (action == GLFW_PRESS);
        if (mouseRight) glfwGetCursorPos(win, &lastMX, &lastMY);
    }
}

void cursor_pos_callback(GLFWwindow*, double x, double y) {
    if (!mouseRight) return;
    float dx=(float)(x-lastMX)*0.15f, dy=(float)(lastMY-y)*0.15f;
    lastMX=x; lastMY=y;
    yaw+=dx; pitch=glm::clamp(pitch+dy,-89.0f,89.0f);
    glm::vec3 f={cosf(glm::radians(yaw))*cosf(glm::radians(pitch)),
                 sinf(glm::radians(pitch)),
                 sinf(glm::radians(yaw))*cosf(glm::radians(pitch))};
    cameraFront = glm::normalize(f);
}

void scroll_callback(GLFWwindow*, double, double y) {
    cameraPos += (float)y * 1.5f * cameraFront;
}

void processInput(GLFWwindow* w) {
    float spd = 10.0f*deltaTime;
    glm::vec3 right = glm::normalize(glm::cross(cameraFront,cameraUp));
    if (glfwGetKey(w,GLFW_KEY_W)==GLFW_PRESS) cameraPos+=spd*cameraFront;
    if (glfwGetKey(w,GLFW_KEY_S)==GLFW_PRESS) cameraPos-=spd*cameraFront;
    if (glfwGetKey(w,GLFW_KEY_A)==GLFW_PRESS) cameraPos-=spd*right;
    if (glfwGetKey(w,GLFW_KEY_D)==GLFW_PRESS) cameraPos+=spd*right;
    if (glfwGetKey(w,GLFW_KEY_Q)==GLFW_PRESS) cameraPos.y+=spd;
    if (glfwGetKey(w,GLFW_KEY_E)==GLFW_PRESS) cameraPos.y-=spd;
}

// ──────────────────────────────────────────────────────────
//  SHADERS
// ──────────────────────────────────────────────────────────
static unsigned int compileShader(GLenum type, const char* src) {
    unsigned int s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr); glCompileShader(s);
    int ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char b[1024];glGetShaderInfoLog(s,1024,nullptr,b);std::cerr<<"SHADER:\n"<<b;}
    return s;
}
static unsigned int makeProgram(const char* v, const char* f) {
    unsigned int vs=compileShader(GL_VERTEX_SHADER,v),
                 fs=compileShader(GL_FRAGMENT_SHADER,f),
                 p=glCreateProgram();
    glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);return p;
}

const char* VS_STD=R"glsl(
#version 330 core
layout(location=0)in vec3 aPos;
layout(location=1)in vec3 aNorm;
uniform mat4 model,view,projection;
out vec3 fragPos;out vec3 norm;
void main(){
    vec4 w=model*vec4(aPos,1.0);
    fragPos=w.xyz;
    norm=mat3(transpose(inverse(model)))*aNorm;
    gl_Position=projection*view*w;
})glsl";

const char* FS_STD=R"glsl(
#version 330 core
in vec3 fragPos;in vec3 norm;
out vec4 FragColor;
uniform vec3 objectColor,lightDir,lightColor;
uniform float ambientStr;
void main(){
    float dif=max(dot(normalize(norm),-normalize(lightDir)),0.0);
    FragColor=vec4((ambientStr+dif)*lightColor*objectColor,1.0);
})glsl";

const char* VS_INST=R"glsl(
#version 330 core
layout(location=0)in vec3 aPos;
layout(location=1)in vec3 aNorm;
layout(location=3)in vec3 iOff;
layout(location=4)in float iBob;
uniform mat4 view,projection;
out vec3 fragPos;out vec3 norm;out float bobVal;
void main(){
    vec3 w=aPos+iOff+vec3(0.0,iBob,0.0);
    fragPos=w;norm=aNorm;bobVal=iBob;
    gl_Position=projection*view*vec4(w,1.0);
})glsl";

const char* FS_INST=R"glsl(
#version 330 core
in vec3 fragPos;in vec3 norm;in float bobVal;
out vec4 FragColor;
uniform vec3 objectColor,lightDir,lightColor;
uniform float ambientStr;
void main(){
    float dif=max(dot(normalize(norm),-normalize(lightDir)),0.0);
    vec3 col=objectColor;
    if(bobVal>0.003) col=mix(col,vec3(1.0,0.55,0.10),0.5);
    FragColor=vec4((ambientStr+dif)*lightColor*col,1.0);
})glsl";

// ──────────────────────────────────────────────────────────
//  HELPERS
// ──────────────────────────────────────────────────────────
static void setMat4(unsigned int p,const char* n,const glm::mat4& m){
    glUniformMatrix4fv(glGetUniformLocation(p,n),1,GL_FALSE,glm::value_ptr(m));}
static void setVec3(unsigned int p,const char* n,const glm::vec3& v){
    glUniform3fv(glGetUniformLocation(p,n),1,glm::value_ptr(v));}
static void setFloat(unsigned int p,const char* n,float v){
    glUniform1f(glGetUniformLocation(p,n),v);}

static void draw(Mesh& m,unsigned int prog,glm::mat4 M,glm::vec3 col){
    setMat4(prog,"model",M);setVec3(prog,"objectColor",col);
    glBindVertexArray(m.VAO);
    glDrawElements(GL_TRIANGLES,(GLsizei)m.indexCount,GL_UNSIGNED_INT,nullptr);
    glBindVertexArray(0);}

static glm::mat4 TRS(glm::vec3 t,glm::vec3 s={1,1,1},float rx=0,float ry=0,float rz=0){
    glm::mat4 M(1);
    M=glm::translate(M,t);
    if(ry)M=glm::rotate(M,glm::radians(ry),{0,1,0});
    if(rx)M=glm::rotate(M,glm::radians(rx),{1,0,0});
    if(rz)M=glm::rotate(M,glm::radians(rz),{0,0,1});
    return glm::scale(M,s);}

// ──────────────────────────────────────────────────────────
//  GAMBAR PETERNAK  (proporsi manusia lebih realistis)
// ──────────────────────────────────────────────────────────
static void drawPeternak(unsigned int prog,
    Mesh& mBox, Mesh& mCyl, Mesh& mSphere,
    glm::vec3 pos, float rotY, float walkAnim, float bendDeg,
    glm::vec3 cBaju, glm::vec3 cKulit, glm::vec3 cCelana)
{
    glm::mat4 root = glm::translate(glm::mat4(1), pos);
    root = glm::rotate(root, glm::radians(rotY), {0,1,0});

    float legSw  = sinf(walkAnim) * 28.0f;
    float armSw  = sinf(walkAnim + 3.14159f) * 22.0f;
    float bendRad = glm::radians(bendDeg);
    glm::vec3 cSepatu = {0.18f, 0.12f, 0.06f};
    glm::vec3 cKemeja = cBaju;

    // ── Kaki + Sepatu ──
    for(int side : {-1, 1}){
        float sw = side == -1 ? legSw : -legSw;
        // Paha atas (Celana) - pakai Silinder
        glm::mat4 hip = root * glm::translate(glm::mat4(1), {side*0.12f, 0.60f, 0.0f});
        hip = glm::rotate(hip, glm::radians(sw), {1,0,0});
        { glm::mat4 M = hip * glm::translate(glm::mat4(1),{0,-0.16f,0});
          M = glm::scale(M, {0.16f, 0.32f, 0.16f});
          draw(mCyl, prog, M, cCelana); }
        
        // Engsel Lutut (membulat)
        { glm::mat4 M = hip * glm::translate(glm::mat4(1),{0,-0.32f,0});
          M = glm::scale(M, {0.15f, 0.15f, 0.15f});
          draw(mSphere, prog, M, cCelana); }

        // Betis - pakai Silinder
        glm::mat4 knee = hip * glm::translate(glm::mat4(1), {0,-0.32f, 0.0f});
        knee = glm::rotate(knee, glm::radians(-fabsf(sw)*0.45f), {1,0,0});
        { glm::mat4 M = knee * glm::translate(glm::mat4(1),{0,-0.16f,0});
          M = glm::scale(M, {0.13f, 0.32f, 0.13f});
          draw(mCyl, prog, M, cKulit); }
          
        // Sepatu (agak membulat)
        { glm::mat4 M = knee * glm::translate(glm::mat4(1),{0,-0.36f, 0.06f});
          M = glm::scale(M, {0.16f, 0.12f, 0.24f});
          draw(mSphere, prog, M, cSepatu); }
    }

    // ── Torso (bungkuk) ──
    glm::mat4 torso = root * glm::translate(glm::mat4(1), {0, 1.05f, 0});
    torso = glm::rotate(torso, bendRad, {1,0,0});
    { glm::mat4 M = torso * glm::scale(glm::mat4(1), {0.38f, 0.54f, 0.26f});
      // Baju lebih tebal dan membulat dari silinder
      draw(mCyl, prog, M, cKemeja); }

    // ── Lengan ──
    for(int side : {-1, 1}){
        float sw = side == -1 ? (-armSw - bendDeg*0.5f) : (armSw - bendDeg*0.5f);
        // Bahu membulat
        glm::mat4 shoulder = torso * glm::translate(glm::mat4(1), {side*0.24f, 0.18f, 0});
        shoulder = glm::rotate(shoulder, glm::radians(sw), {1,0,0});
        { glm::mat4 M = shoulder * glm::scale(glm::mat4(1), {0.14f, 0.14f, 0.14f});
          draw(mSphere, prog, M, cKemeja); }

        // Lengan atas (silinder)
        { glm::mat4 M = shoulder * glm::translate(glm::mat4(1),{0,-0.14f,0});
          M = glm::scale(M, {0.12f, 0.28f, 0.12f});
          draw(mCyl, prog, M, cKemeja); }
          
        // Siku membulat
        glm::mat4 elbow = shoulder * glm::translate(glm::mat4(1),{0,-0.28f,0});
        elbow = glm::rotate(elbow, glm::radians(sw*0.3f), {1,0,0});
        { glm::mat4 M = elbow * glm::scale(glm::mat4(1), {0.11f, 0.11f, 0.11f});
          draw(mSphere, prog, M, cKulit); }

        // Lengan bawah (silinder)
        { glm::mat4 M = elbow * glm::translate(glm::mat4(1),{0,-0.13f,0});
          M = glm::scale(M, {0.10f, 0.26f, 0.10f});
          draw(mCyl, prog, M, cKulit); }
          
        // Tangan (kepalan tangan)
        { glm::mat4 M = elbow * glm::translate(glm::mat4(1),{0,-0.28f,0});
          M = glm::scale(M, {0.11f, 0.11f, 0.11f});
          draw(mSphere, prog, M, cKulit); }
    }

    // ── Leher ──
    { glm::mat4 M = torso * TRS({0, 0.31f, 0}, {0.11f, 0.12f, 0.11f});
      draw(mBox, prog, M, cKulit); }

    // ── Kepala ──
    glm::mat4 head = torso * glm::translate(glm::mat4(1), {0, 0.48f, 0});
    { glm::mat4 M = head * glm::scale(glm::mat4(1), {0.24f, 0.26f, 0.22f});
      draw(mSphere, prog, M, cKulit); }
    // Mata kiri
    { glm::mat4 M = head * TRS({-0.07f, 0.04f, 0.12f}, {0.025f, 0.025f, 0.025f});
      draw(mSphere, prog, M, {0.1f, 0.06f, 0.02f}); }
    // Mata kanan
    { glm::mat4 M = head * TRS({ 0.07f, 0.04f, 0.12f}, {0.025f, 0.025f, 0.025f});
      draw(mSphere, prog, M, {0.1f, 0.06f, 0.02f}); }

    // ── Topi jerami (brim lebar + crown) ──
    glm::vec3 cTopi = {0.85f, 0.72f, 0.38f};
    { glm::mat4 M = head * TRS({0, 0.16f, 0}, {0.42f, 0.03f, 0.42f});
      draw(mCyl, prog, M, cTopi); }   // brim topi melingkar (silinder pipih)
    { glm::mat4 M = head * TRS({0, 0.24f, 0}, {0.24f, 0.16f, 0.24f});
      draw(mSphere, prog, M, {0.78f, 0.64f, 0.28f}); }  // crown membulat (sphere)
}

// ──────────────────────────────────────────────────────────
//  GAMBAR GEROBAK  (wheelbarrow realistis - DIPERBESAR)
// ──────────────────────────────────────────────────────────
static void drawGerobak(unsigned int prog, Mesh& mBox, Mesh& mCyl,
                        glm::vec3 gp, float wAngle)
{
    // Warna
    glm::vec3 cKayu  = {0.55f, 0.35f, 0.14f};  // bak kayu
    glm::vec3 cBesi  = {0.25f, 0.25f, 0.28f};  // rangka besi
    glm::vec3 cRoda  = {0.15f, 0.15f, 0.15f};  // roda karet
    glm::vec3 cRim   = {0.55f, 0.55f, 0.58f};  // velg

    // Scale-up multiplier
    float sM = 1.3f;

    // ── Bak (trapesium: bawah lebih kecil dari atas, sedikit miring ke depan) ──
    // Bagian bawah bak
    { glm::mat4 M = TRS(gp + glm::vec3{0, 0.30f, 0.0f}, {0.70f*sM, 0.06f, 0.65f*sM});
      draw(mBox, prog, M, cKayu); }
    // Sisi kiri bak
    { glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{-0.36f*sM, 0.45f, 0.0f});
      M = glm::rotate(M, glm::radians(12.0f), {0,0,1});
      M = glm::scale(M, {0.06f, 0.40f*sM, 0.68f*sM});
      draw(mBox, prog, M, cKayu); }
    // Sisi kanan bak
    { glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{0.36f*sM, 0.45f, 0.0f});
      M = glm::rotate(M, glm::radians(-12.0f), {0,0,1});
      M = glm::scale(M, {0.06f, 0.40f*sM, 0.68f*sM});
      draw(mBox, prog, M, cKayu); }
    // Depan bak
    { glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{0, 0.45f, 0.34f*sM});
      M = glm::rotate(M, glm::radians(-10.0f), {1,0,0});
      M = glm::scale(M, {0.80f*sM, 0.40f*sM, 0.06f});
      draw(mBox, prog, M, cKayu); }
    // Belakang bak
    { glm::mat4 M = TRS(gp + glm::vec3{0, 0.45f, -0.34f*sM}, {0.80f*sM, 0.40f*sM, 0.06f});
      draw(mBox, prog, M, cKayu); }

    // ── Roda depan (satu roda besar) ──
    { glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{0, 0.20f, 0.48f*sM});
      M = glm::rotate(M, glm::radians(wAngle), {1,0,0});
      M = glm::rotate(M, glm::radians(90.0f), {0,0,1});
      M = glm::scale(M, {0.26f, 0.08f, 0.26f});
      draw(mCyl, prog, M, cRoda); }
    // Rim roda
    { glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{0, 0.20f, 0.48f*sM});
      M = glm::rotate(M, glm::radians(wAngle), {1,0,0});
      M = glm::rotate(M, glm::radians(90.0f), {0,0,1});
      M = glm::scale(M, {0.21f, 0.11f, 0.21f});
      draw(mCyl, prog, M, cRim); }
    // Fork/garpu roda (kiri dan kanan)
    for(int s : {-1,1}){
        glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{s*0.06f, 0.30f, 0.35f*sM});
        M = glm::rotate(M, glm::radians(-20.0f), {1,0,0});
        M = glm::scale(M, {0.04f, 0.35f, 0.04f});
        draw(mBox, prog, M, cBesi); }

    // ── Rangka bawah (dua batang memanjang) ──
    for(int s : {-1,1}){
        glm::mat4 M = TRS(gp + glm::vec3{s*0.25f*sM, 0.16f, 0.0f},
                          {0.05f, 0.07f, 1.0f*sM});
        draw(mBox, prog, M, cBesi); }

    // ── Pegangan / handle (dua batang ke belakang, agak naik) ──
    for(int s : {-1,1}){
        glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{s*0.25f*sM, 0.45f, -0.32f*sM});
        M = glm::rotate(M, glm::radians(-28.0f), {1,0,0});
        M = glm::scale(M, {0.05f, 0.70f, 0.05f});
        draw(mBox, prog, M, cKayu); }
    // Pegangan palang (grip bar)
    { glm::mat4 M = TRS(gp + glm::vec3{0, 0.70f, -0.75f*sM}, {0.60f*sM, 0.05f, 0.05f});
      draw(mBox, prog, M, cKayu); }

    // ── Kaki penyangga (dua batang V ke bawah) ──
    for(int s : {-1,1}){
        glm::mat4 M = glm::translate(glm::mat4(1), gp + glm::vec3{s*0.20f*sM, 0.13f, -0.28f*sM});
        M = glm::rotate(M, glm::radians(25.0f), {1,0,0});
        M = glm::scale(M, {0.04f, 0.35f, 0.04f});
        draw(mBox, prog, M, cBesi); }
}

// ══════════════════════════════════════════════════════════
//  MAIN
// ══════════════════════════════════════════════════════════
int main(){
    srand(42);
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES,4);

    GLFWwindow* win=glfwCreateWindow(SCR_W,SCR_H,
        "Simulasi Peternakan | F=Pakan  H=Panen  WASD+QE=Gerak  RClick=Putar Pandang | 50 Ayam",
        nullptr,nullptr);
    if(!win){glfwTerminate();return -1;}
    glfwMakeContextCurrent(win);
    glfwSetKeyCallback(win,key_callback);
    glfwSetMouseButtonCallback(win,mouse_button_callback);
    glfwSetCursorPosCallback(win,cursor_pos_callback);
    glfwSetScrollCallback(win,scroll_callback);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))return -1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glViewport(0,0,SCR_W,SCR_H);

    unsigned int progStd =makeProgram(VS_STD, FS_STD);
    unsigned int progInst=makeProgram(VS_INST,FS_INST);

    // ── MESH ──
    Mesh mFloor   =createBox(1,0.05f,1);
    Mesh mWall    =createBox(1,1,0.05f);
    Mesh mPillarSq=createBox(0.16f,1,0.16f);
    Mesh mPillarCy=createCylinder(0.08f,1,12);
    Mesh mRoofSlb =createBox(1,0.12f,1);
    Mesh mRoofRdg =createBox(0.18f,0.28f,1);
    Mesh mPakan   =createBox(0.9f,0.12f,0.20f);
    Mesh mAyam    =createBox(0.26f,0.20f,0.34f);
    Mesh mAyamKep =createSphere(0.09f,8,6);
    Mesh mAyamEkor=createBox(0.08f,0.12f,0.10f);
    Mesh mTelur   =createSphere(0.085f,10,8);
    Mesh mBox     =createBox(1,1,1);
    Mesh mCyl     =createCylinder(0.5f,1,18);
    Mesh mSphere  =createSphere(0.5f,12,10);
    Mesh mPohon   =createCylinder(0.14f,2.8f,8);
    Mesh mDaun    =createSphere(1.0f,10,8);

    initAyamPositions();

    // Instance buffers
    float iOffsets[NUM_AYAM*3];
    for(int i=0;i<NUM_AYAM;i++){
        iOffsets[i*3+0]=ayamBasePos[i].x;
        iOffsets[i*3+1]=ayamBasePos[i].y;
        iOffsets[i*3+2]=ayamBasePos[i].z;
        ayamBobs[i]=0;ayamFeeding[i]=false;ayamBobPhase[i]=0;
    }
    unsigned int vboOff,vboBob;
    glGenBuffers(1,&vboOff);
    glBindBuffer(GL_ARRAY_BUFFER,vboOff);
    glBufferData(GL_ARRAY_BUFFER,sizeof(iOffsets),iOffsets,GL_STATIC_DRAW);
    glGenBuffers(1,&vboBob);
    glBindBuffer(GL_ARRAY_BUFFER,vboBob);
    glBufferData(GL_ARRAY_BUFFER,NUM_AYAM*sizeof(float),ayamBobs,GL_DYNAMIC_DRAW);

    glBindVertexArray(mAyam.VAO);
    glBindBuffer(GL_ARRAY_BUFFER,vboOff);
    glEnableVertexAttribArray(3);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,0,nullptr);
    glVertexAttribDivisor(3,1);
    glBindBuffer(GL_ARRAY_BUFFER,vboBob);
    glEnableVertexAttribArray(4);glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,0,nullptr);
    glVertexAttribDivisor(4,1);
    glBindVertexArray(0);

    for(int i=0;i<NUM_AYAM;i++)
        eggs[i].pos=ayamBasePos[i]+glm::vec3{0,-0.04f,0.20f};

    // Cahaya
    glm::vec3 lightDir  =glm::normalize(glm::vec3(-0.5f,-1.0f,-0.4f));
    glm::vec3 lightColor={1.0f,0.97f,0.90f};
    float     ambStr    =0.42f;
    auto setLight=[&](unsigned int p){
        setVec3(p,"lightDir",lightDir);
        setVec3(p,"lightColor",lightColor);
        setFloat(p,"ambientStr",ambStr);};

    // Warna
    glm::vec3 cRumput  ={0.35f,0.56f,0.20f};
    glm::vec3 cTanah   ={0.50f,0.36f,0.20f};
    glm::vec3 cLorong  ={0.42f,0.32f,0.18f};
    glm::vec3 cKayu    ={0.65f,0.45f,0.25f};
    glm::vec3 cKayuTua ={0.45f,0.28f,0.12f};
    glm::vec3 cGenteng ={0.75f,0.25f,0.12f};
    glm::vec3 cKawat   ={0.75f,0.75f,0.70f};
    glm::vec3 cKrem    ={0.96f,0.93f,0.82f};
    glm::vec3 cAyamCol ={0.95f,0.88f,0.70f};
    glm::vec3 cJengger ={0.85f,0.15f,0.10f};
    glm::vec3 cTelurCol={0.99f,0.95f,0.85f};
    glm::vec3 cPakanCol={0.88f,0.78f,0.42f};

    // ──────────────────────────────────────────────────────
    //  RENDER LOOP
    // ──────────────────────────────────────────────────────
    while(!glfwWindowShouldClose(win)){
        float now=(float)glfwGetTime();
        deltaTime=now-lastFrame; lastFrame=now;
        processInput(win);

        // ── UPDATE ────────────────────────────────────────

        // ── FEEDING: peternak memberi pakan satu-satu ke 50 ayam ──────────
        if(simState == SimState::FEEDING){
            // Fase 100+i → beri pakan ke ayam ke-i
            if(peternak.phase >= 100 && peternak.phase < 100 + NUM_AYAM){
                int idx = peternak.phase - 100;

                glm::vec3 ayamPos = ayamBasePos[idx];
                // Laning: Peternak hanya boleh berada di tengah lorong (X=0)
                glm::vec3 targetPos = glm::vec3(0.0f, peternak.pos.y, ayamPos.z);
                float dist = glm::distance(peternak.pos, targetPos);

                if(dist > 0.22f){
                    // --- BERJALAN menuju ayam ---
                    glm::vec3 dir = glm::normalize(targetPos - peternak.pos);
                    peternak.pos     += dir * peternak.speed * deltaTime;
                    peternak.walkAnim += deltaTime * 7.0f;
                    peternak.rot      = glm::degrees(atan2f(dir.x, -dir.z));
                    peternak.walking  = true;
                    peternak.bendAngle = 0.0f;
                    // Reset flag arrived setiap kali masih berjalan
                    if(peternak.arrived){
                        peternak.arrived    = false;
                        peternak.phaseTimer = now;
                    }
                } else {
                    // --- SUDAH SAMPAI → beri pakan (animasi bungkuk) ---
                    peternak.walking = false;
                    // Catat waktu tiba hanya sekali (via flag arrived)
                    if(!peternak.arrived){
                        peternak.arrived    = true;
                        peternak.phaseTimer = now;  // catat waktu tiba
                    }
                    // Animasi bungkuk ke arah ayam di samping lorong
                    float el = now - peternak.phaseTimer;
                    // Putar badan peternak menghadap ayam yang ada di kiri/kanan saat berhenti
                    glm::vec3 faceDir = glm::normalize(ayamPos - peternak.pos);
                    peternak.rot = glm::degrees(atan2f(faceDir.x, -faceDir.z));
                    peternak.bendAngle  = 28.0f * sinf(el * 3.5f);
                    ayamBobs[idx]       = 0.09f * sinf(el * 9.0f);
                    ayamFeeding[idx]    = true;

                    if(el > 1.3f){
                        // Selesai beri pakan → lanjut ke ayam berikutnya
                        ayamFeeding[idx]  = false;
                        ayamBobs[idx]     = 0.0f;
                        peternak.bendAngle = 0.0f;
                        peternak.arrived  = false;
                        peternak.phase++;
                        peternak.phaseTimer = now;
                        peternak.totalChickenProcessed++;
                        // Telur muncul sesaat setelah diberi pakan
                        eggs[idx].visible   = true;
                        eggs[idx].spawnTime = now + 0.15f;
                    }
                }
            }

            // Selesai semua ayam → peternak kembali ke pintu masuk (fase 50)
            if(peternak.phase >= 100 + NUM_AYAM){
                peternak.phase     = 50;  // fase return
                peternak.arrived   = false;
                peternak.bendAngle = 0.0f;
                peternak.phaseTimer = now;
            }

            // Fase 50: peternak kembali berjalan ke pintu masuk
            if(peternak.phase == 50){
                glm::vec3 pintu = {0.6f, 0.0f, 3.8f};
                float dist = glm::distance(peternak.pos, pintu);
                if(dist > 0.25f){
                    glm::vec3 dir = glm::normalize(pintu - peternak.pos);
                    peternak.pos      += dir * peternak.speed * deltaTime;
                    peternak.walkAnim += deltaTime * 7.0f;
                    peternak.rot       = glm::degrees(atan2f(dir.x, -dir.z));
                    peternak.walking   = true;
                } else {
                    // Sudah kembali ke pintu
                    peternak.pos     = pintu;
                    peternak.rot     = 180.0f;
                    peternak.walking = false;
                    peternak.phase   = 0;
                    simState         = SimState::IDLE;
                }
            }

            // Upload data bob ayam ke GPU setiap frame
            glBindBuffer(GL_ARRAY_BUFFER, vboBob);
            glBufferSubData(GL_ARRAY_BUFFER, 0, NUM_AYAM * sizeof(float), ayamBobs);
        }

        // ── HARVESTING: peternak mendorong gerobak + panen telur satu-satu ──
        if(simState == SimState::HARVESTING){
            if(gerobak.active){

                // Fase 200+i → ambil telur ke-i
                if(peternak.phase >= 200 && peternak.phase < 200 + NUM_AYAM){
                    int idx = peternak.phase - 200;

                    if(idx < NUM_AYAM && eggs[idx].visible){
                        glm::vec3 eggPos = eggs[idx].pos;
                        // Laning: Peternak hanya di tengah lorong (X=0)
                        glm::vec3 targetPos = glm::vec3(0.0f, peternak.pos.y, eggPos.z);
                        float dist = glm::distance(peternak.pos, targetPos);

                        if(dist > 0.15f){
                            // --- BERJALAN di lorong ---
                            glm::vec3 dir = glm::normalize(targetPos - peternak.pos);
                            peternak.pos      += dir * peternak.speed * deltaTime;
                            peternak.walkAnim  += deltaTime * 7.0f;
                            peternak.rot       = glm::degrees(atan2f(dir.x, -dir.z));
                            peternak.walking   = true;
                            peternak.bendAngle = 0.0f;
                            if(peternak.arrived){
                                peternak.arrived    = false;
                                peternak.phaseTimer = now;
                            }
                        } else {
                            // --- SUDAH SAMPAI → ambil telur (bungkuk sebentar) ---
                            peternak.walking = false;
                            if(!peternak.arrived){
                                peternak.arrived    = true;
                                peternak.phaseTimer = now;
                            }
                            
                            // Hadap telur
                            glm::vec3 faceDir = glm::normalize(eggPos - peternak.pos);
                            peternak.rot = glm::degrees(atan2f(faceDir.x, -faceDir.z));
                            
                            float el = now - peternak.phaseTimer;
                            peternak.bendAngle = 20.0f * sinf(el * 4.5f);

                            if(el > 0.9f){
                                // Telur masuk gerobak
                                eggs[idx].visible  = false;
                                gerobak.eggsHarvested++;
                                peternak.bendAngle  = 0.0f;
                                peternak.arrived    = false;
                                peternak.phase++;
                                peternak.phaseTimer = now;
                                peternak.totalChickenProcessed++;
                            }
                        }
                    } else {
                        // Telur tidak ada (belum diberi pakan) → lewati
                        peternak.phase++;
                        peternak.arrived    = false;
                        peternak.phaseTimer = now;
                    }

                    // Gerobak ATTACHED ke peternak (mengikuti di belakang kanan)
                    float pRad = glm::radians(peternak.rot);
                    gerobak.pos = peternak.pos
                        + glm::vec3(sinf(pRad) * (-0.55f), 0.0f, -cosf(pRad) * (-0.55f));
                    gerobak.pos.y = 0.0f;
                    gerobak.wheelAngle += peternak.speed * deltaTime * 150.0f;
                }

                // Selesai semua telur → gerobak kembali ke depan
                if(peternak.phase >= 200 + NUM_AYAM && !harvestDone){
                    harvestDone      = true;
                    gerobak.returning = true;
                }

                // Fase return: gerobak bergerak mundur ke posisi awal
                if(gerobak.returning){
                    peternak.walking = true;
                    peternak.walkAnim += deltaTime * 7.0f;
                    peternak.rot      = 0.0f;  // hadap ke depan (keluar kandang)
                    peternak.pos.z   += gerobak.speed * deltaTime;
                    // Gerobak masih attached saat return
                    gerobak.pos       = peternak.pos
                        + glm::vec3(sinf(glm::radians(peternak.rot)) * (-0.55f),
                                    0.0f,
                                    -cosf(glm::radians(peternak.rot)) * (-0.55f));
                    gerobak.wheelAngle += gerobak.speed * deltaTime * 150.0f;

                    if(peternak.pos.z >= 5.5f){
                        gerobak.active    = false;
                        peternak.walking  = false;
                        peternak.phase    = 0;
                        peternak.arrived  = false;
                        simState          = SimState::IDLE;
                    }
                }
            }
        }

        // ── RENDER ────────────────────────────────────────
        glClearColor(0.52f,0.78f,0.92f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        glm::mat4 view=glm::lookAt(cameraPos,cameraPos+cameraFront,cameraUp);
        glm::mat4 proj=glm::perspective(glm::radians(55.0f),(float)SCR_W/SCR_H,0.1f,500.0f);

        glUseProgram(progStd);
        setMat4(progStd,"view",view); setMat4(progStd,"projection",proj);
        setLight(progStd);

        // ── TANAH ──
        draw(mFloor,progStd,TRS({0,-0.028f,-(TOTAL_D*0.5f-1)},{TOTAL_W+28,1,TOTAL_D+38}),cRumput);
        draw(mFloor,progStd,TRS({0,-0.022f,-(TOTAL_D*0.5f-1)},{TOTAL_W,1,TOTAL_D+0.5f}),cTanah);
        draw(mFloor,progStd,TRS({0,-0.016f,-(TOTAL_D*0.5f-1)},{LORONG_W,1,TOTAL_D+0.5f}),cLorong);

        // ── KANDANG ──
        float seksiD=TOTAL_D/JML_SEKSI;

        // Tiang tiap titik sambungan (JML_SEKSI+1 baris tiang)
        for(int s=0;s<=JML_SEKSI;s++){
            float zt=-(s*seksiD);
            for(int sd:{-1,1})
                draw(mPillarSq,progStd,
                     TRS({sd*HALF_W,TINGGI_DINDING*0.5f,zt},{1,TINGGI_DINDING,1}),cKayu);
        }

        // Balok atas horizontal
        for(int s=0;s<=JML_SEKSI;s++){
            float z=-(s*seksiD);
            draw(mWall,progStd,TRS({0,TINGGI_DINDING,z},{TOTAL_W,0.10f,0.10f}),cKayu);
        }

        // Dinding samping kiri & kanan (Tembok bata/kayu di bawah, kawat di atas sebagai jendela)
        for(int s=0;s<JML_SEKSI;s++){
            float zM = -(s * seksiD + seksiD * 0.5f);
            float len = seksiD;
            
            // Tembok solid bagian bawah (tinggi 1.2m)
            draw(mWall, progStd, TRS({-HALF_W, 0.6f, zM}, {0.06f, 1.2f, len}), cKrem); // Kiri
            draw(mWall, progStd, TRS({ HALF_W, 0.6f, zM}, {0.06f, 1.2f, len}), cKrem); // Kanan

            // Kawat bagian atas (tinggi dari 1.2m ke TINGGI_DINDING)
            float kawatH = TINGGI_DINDING - 1.2f;
            float kawatY = 1.2f + kawatH * 0.5f;
            draw(mWall, progStd, TRS({-HALF_W, kawatY, zM}, {0.02f, kawatH, len}), cKawat); // Kiri
            draw(mWall, progStd, TRS({ HALF_W, kawatY, zM}, {0.02f, kawatH, len}), cKawat); // Kanan
            
            // Kusen / frame pemisah di tengah jendela (vertikal)
            draw(mPillarSq, progStd, TRS({-HALF_W, kawatY, zM}, {0.6f, kawatH, 0.6f}), cKayu); // Kiri
            draw(mPillarSq, progStd, TRS({ HALF_W, kawatY, zM}, {0.6f, kawatH, 0.6f}), cKayu); // Kanan
        }
        // ── Dinding belakang (solid kawat) ──
        draw(mWall,progStd,TRS({0,TINGGI_DINDING*0.5f,-(TOTAL_D-0.1f)},{TOTAL_W,TINGGI_DINDING,0.04f}),cKawat);

        // ── Dinding depan: frame PINTU / gerbang di tengah, kawat di sisi ──
        // Lebar bukaan pintu di lorong tengah: LORONG_W
        // Panel kawat kiri (dari tiang kiri sampai tepi lorong)
        {
            float panelW = HALF_W - LORONG_W * 0.5f;
            float panelCX = -(HALF_W - panelW * 0.5f);
            draw(mWall,progStd,TRS({panelCX, TINGGI_DINDING*0.5f, 0.1f},
                                   {panelW, TINGGI_DINDING, 0.04f}),cKawat);
        }
        // Panel kawat kanan
        {
            float panelW = HALF_W - LORONG_W * 0.5f;
            float panelCX = HALF_W - panelW * 0.5f;
            draw(mWall,progStd,TRS({panelCX, TINGGI_DINDING*0.5f, 0.1f},
                                   {panelW, TINGGI_DINDING, 0.04f}),cKawat);
        }
        // Tiang kiri pintu
        draw(mPillarSq,progStd,
             TRS({-LORONG_W*0.5f, TINGGI_DINDING*0.5f, 0.1f},{1,TINGGI_DINDING,1}),cKayu);
        // Tiang kanan pintu
        draw(mPillarSq,progStd,
             TRS({ LORONG_W*0.5f, TINGGI_DINDING*0.5f, 0.1f},{1,TINGGI_DINDING,1}),cKayu);
        // Balok atas pintu (lintel)
        draw(mWall,progStd,
             TRS({0, TINGGI_DINDING*0.80f, 0.1f},{LORONG_W, 0.14f, 0.14f}),cKayuTua);
        // Papan nama kecil di atas lintel
        draw(mWall,progStd,
             TRS({0, TINGGI_DINDING*0.90f, 0.08f},{LORONG_W*0.7f, 0.18f, 0.06f}),{0.82f,0.62f,0.22f});

        // \u2500\u2500 Atap (rumus geometri benar) \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
        // Pitch 20 derajat dari horizontal
        // - Eave  (tepi bawah) ada di x = ±HALF_W, y = TINGGI_DINDING
        // - Ridge (puncak)     ada di x = 0,       y = TINGGI_DINDING + HALF_W*tan(pitch)
        {
            float pitchRad = glm::radians(ROOF_PITCH_DEG);
            float rise     = HALF_W * tanf(pitchRad);          // tinggi puncak dari atas dinding
            float ridgeY   = TINGGI_DINDING + rise;            // y puncak atap
            float slope    = HALF_W / cosf(pitchRad);          // panjang panel sepanjang lereng
            // Posisi center panel kiri: midpoint antara eave(-HALF_W, TINGGI_DINDING)
            //   dan ridge(0, ridgeY) dalam bidang XY
            float panelCX  = -HALF_W * 0.5f;
            float panelCY  = (TINGGI_DINDING + ridgeY) * 0.5f;

            for(int s = 0; s < JML_SEKSI; s++){
                float zM = -(s*seksiD + seksiD*0.5f);
                float sD =  seksiD + 0.30f;   // sedikit overlap antar seksi

                // Panel KIRI  (+pitch CCW dari sumbu Z)
                { glm::mat4 M = glm::translate(glm::mat4(1), {panelCX, panelCY, zM});
                  M = glm::rotate(M, pitchRad, {0,0,1});
                  M = glm::scale(M, {slope, 0.12f, sD});
                  draw(mRoofSlb, progStd, M, cGenteng); }

                // Panel KANAN (mirror)
                { glm::mat4 M = glm::translate(glm::mat4(1), {-panelCX, panelCY, zM});
                  M = glm::rotate(M, -pitchRad, {0,0,1});
                  M = glm::scale(M, {slope, 0.12f, sD});
                  draw(mRoofSlb, progStd, M, cGenteng); }

                // Ridge (bubungan puncak)
                draw(mRoofRdg, progStd,
                     TRS({0, ridgeY + 0.07f, zM}, {1, 1, sD}), cKayuTua);
            }

            // Overhang depan & belakang (kanopi kecil)
            for(float zo : {0.28f, -(TOTAL_D - 0.28f)}){
                { glm::mat4 M = glm::translate(glm::mat4(1), {panelCX, panelCY, zo});
                  M = glm::rotate(M, pitchRad, {0,0,1});
                  M = glm::scale(M, {slope, 0.12f, 0.50f});
                  draw(mRoofSlb, progStd, M, cGenteng); }
                { glm::mat4 M = glm::translate(glm::mat4(1), {-panelCX, panelCY, zo});
                  M = glm::rotate(M, -pitchRad, {0,0,1});
                  M = glm::scale(M, {slope, 0.12f, 0.50f});
                  draw(mRoofSlb, progStd, M, cGenteng); }
            }
        }



        // ── SEKAT BARIS ──
        // ── SEKAT BARIS & LORONG ──
        for(int b = 0; b <= BARIS_AYAM; b++){
            float z = -(b * JARAK_Z + 1.0f) + JARAK_Z*0.5f;  
            // Papan pemisah diletakkan di antara ayam (menyekat sumbu Z)
            draw(mWall,progStd,TRS({-OFFSET_SISI, 0.35f, z},{0.8f, 0.70f, 0.04f}),cKawat);
            draw(mWall,progStd,TRS({ OFFSET_SISI, 0.35f, z},{0.8f, 0.70f, 0.04f}),cKawat);
        }

        // ── TEMPAT PAKAN (Palung memanjang di sepanjang pinggir lorong) ──
        float pakanLen = BARIS_AYAM * JARAK_Z + 0.5f;
        float pakanZ   = -(pakanLen * 0.5f + 0.5f);
        // Palung kiri
        draw(mPakan, progStd, TRS({-(OFFSET_SISI - 0.35f), 0.12f, pakanZ}, {0.3f, 1, pakanLen}), cKayu);
        // Isi pakan kiri
        draw(mPakan, progStd, TRS({-(OFFSET_SISI - 0.35f), 0.16f, pakanZ}, {0.26f, 1, pakanLen-0.05f}), cPakanCol);
        
        // Palung kanan
        draw(mPakan, progStd, TRS({ OFFSET_SISI - 0.35f, 0.12f, pakanZ}, {0.3f, 1, pakanLen}), cKayu);
        // Isi pakan kanan
        draw(mPakan, progStd, TRS({ OFFSET_SISI - 0.35f, 0.16f, pakanZ}, {0.26f, 1, pakanLen-0.05f}), cPakanCol);

        // ── POHON ──
        float pxs[]={-HALF_W-3.5f,-HALF_W-7.0f,HALF_W+3.5f,HALF_W+7.0f,-HALF_W-5.5f,HALF_W+5.5f};
        float pzs[]={-2.0f,-9.0f,-3.0f,-11.0f,-(TOTAL_D-3.0f),-(TOTAL_D-4.0f)};
        for(int t=0;t<6;t++){
            draw(mPohon, progStd,TRS({pxs[t],1.4f,pzs[t]}),{0.40f,0.24f,0.10f});
            draw(mDaun,  progStd,TRS({pxs[t],3.6f,pzs[t]},{1,0.9f,1}),{0.22f,0.52f,0.16f});}

        // ── TELUR ──
        for(int i=0;i<NUM_AYAM;i++){
            if(!eggs[i].visible)continue;
            float sc=glm::clamp((now-eggs[i].spawnTime)/0.25f,0.0f,1.0f);
            glm::mat4 M=glm::translate(glm::mat4(1),eggs[i].pos);
            M=glm::scale(M,{sc*0.9f,sc*1.2f,sc*0.9f});
            draw(mTelur,progStd,M,cTelurCol);}

        // ── GEROBAK ──
        // ── GEROBAK: selalu tampil (diparkir saat idle, aktif saat panen) ──
        {
            glm::vec3 gPos = gerobak.active ? gerobak.pos : glm::vec3{-1.2f, 0.0f, 3.5f};
            float gAngle   = gerobak.active ? gerobak.wheelAngle : 0.0f;
            drawGerobak(progStd, mBox, mCyl, gPos, gAngle);

            // Telur di dalam gerobak saat memanen
            if(gerobak.eggsHarvested > 0){
                int maxShow = glm::min(gerobak.eggsHarvested, 50); // Mampu menampung 50 telur secara visual
                for(int e = 0; e < maxShow; e++){
                    // Gerobak sudah diperbesar 1.3x.
                    // Bak bagian dalam bisa muat grid 5 x 5 atau 6 x 5 telur per lapis.
                    // Lebar dalam bak (X) ~ 0.7*1.3 = 0.9. Kedalaman (Z) ~ 0.65*1.3 = 0.8.
                    int numCols = 6;
                    int numRows = 5;
                    int lapis = e / (numCols * numRows); // setiap 30 telur numpuk ke atas
                    int eLapis = e % (numCols * numRows);
                    
                    float ex = (eLapis % numCols) * 0.12f - 0.30f;
                    float ez = (eLapis / numCols) * 0.14f - 0.28f;
                    float ey = 0.38f + lapis * 0.08f; // Dasar telur di bak y~0.38f
                    
                    glm::mat4 M = glm::translate(glm::mat4(1), gPos + glm::vec3{ex, ey, ez});
                    M = glm::scale(M, {0.7f, 0.9f, 0.7f});
                    draw(mTelur, progStd, M, cTelurCol);
                }
            }
        }

        // ── PETERNAK: selalu tampil ──
        drawPeternak(progStd, mBox, mCyl, mSphere,
            peternak.pos, peternak.rot,
            peternak.walking ? peternak.walkAnim : 0.0f,
            peternak.bendAngle,
            {0.22f,0.48f,0.18f},   // baju hijau kebun
            {0.88f,0.68f,0.48f},   // kulit
            {0.20f,0.20f,0.35f});  // celana biru-gelap

        // HAPUS INSTANCING mAyam (kotak putih melayang yang jadi bug)
        // Kita langsung gambar manual di bawah.
        // Badan + Kepala + sayap + jengger + ekor + kaki ayam (manual loop lengkap & realistis)
        glUseProgram(progStd);
        setMat4(progStd,"view",view);setMat4(progStd,"projection",proj);setLight(progStd);
        
        glm::vec3 cKakiAy = {0.75f, 0.52f, 0.08f};
        glm::vec3 cParuhAy = {0.88f, 0.65f, 0.05f};
        glm::vec3 cPialAy  = {0.82f, 0.08f, 0.06f};

        for(int i = 0; i < NUM_AYAM; i++){
            float bob = ayamBobs[i];
            // Anggukan kepala saat makan
            float nod = ayamFeeding[i] ? (18.0f * sinf((float)glfwGetTime() * 9.0f + i)) : 0.0f;
            glm::vec3 bp = ayamBasePos[i] + glm::vec3{0, bob, 0};
            
            // Rotasi ayam menghadap ke lorong (Kiri hadap Kanan/90, Kanan hadap Kiri/-90)
            float rotY = (i % 2 == 0) ? 90.0f : -90.0f;
            glm::mat4 rootAyam = glm::translate(glm::mat4(1), bp);
            rootAyam = glm::rotate(rootAyam, glm::radians(rotY), {0,1,0});

            // ─ Badan Ayam (pengganti instancing, membulat) ─
            { glm::mat4 M = rootAyam * TRS({0,0.05f,0}, {0.28f, 0.22f, 0.36f});
              draw(mSphere, progStd, M, cAyamCol); } // Bodi utama pakai sphere

            // ─ Sayap ─
            for(int s : {-1,1}){
                glm::mat4 M = rootAyam * TRS({s*0.14f, 0.06f, -0.01f});
                M = glm::rotate(M, glm::radians((float)s * 12.0f), {0,0,1});
                M = glm::scale(M, {0.05f, 0.16f, 0.32f});
                draw(mSphere, progStd, M, {cAyamCol.r*0.85f, cAyamCol.g*0.82f, cAyamCol.b*0.70f});
            }

            // ─ Kaki (2 segmen + jari) ─
            for(int side : {-1,1}){
                float sx = side * 0.07f;
                // Paha (dari bawah body ke lantai)
                { glm::mat4 M = rootAyam * TRS({sx, 0.05f, 0.06f});
                  M = glm::rotate(M, glm::radians(-20.0f), {1,0,0});
                  M = glm::scale(M, {0.03f, 0.14f, 0.03f});
                  draw(mCyl, progStd, M, cKakiAy); }
                // Betis
                { glm::mat4 M = rootAyam * TRS({sx, -0.09f, 0.13f});
                  M = glm::rotate(M, glm::radians(22.0f), {1,0,0});
                  M = glm::scale(M, {0.025f, 0.13f, 0.025f});
                  draw(mCyl, progStd, M, cKakiAy); }
                // Jari depan
                { glm::mat4 M = rootAyam * TRS({sx, -0.19f, 0.19f}, {0.018f, 0.018f, 0.10f});
                  draw(mBox, progStd, M, cKakiAy); }
            }

            // ─ Leher ─
            glm::mat4 neckM = rootAyam * TRS({0, 0.13f, 0.15f});
            neckM = glm::rotate(neckM, glm::radians(12.0f + nod*0.3f), {1,0,0});
            { glm::mat4 M = neckM * glm::scale(glm::mat4(1), {0.10f, 0.15f, 0.10f});
              draw(mAyamKep, progStd, M, {cKrem.r*0.92f, cKrem.g*0.88f, cKrem.b*0.78f}); }

            // ─ Kepala ─
            glm::mat4 headM = neckM * TRS({0, 0.14f, 0.05f});
            headM = glm::rotate(headM, glm::radians(nod), {1,0,0});
            { glm::mat4 M = headM * glm::scale(glm::mat4(1), {0.16f, 0.15f, 0.15f});
              draw(mAyamKep, progStd, M, cKrem); }

            // Paruh atas
            { glm::mat4 M = headM * TRS({0, -0.02f, 0.09f}, {0.045f, 0.032f, 0.075f});
              draw(mBox, progStd, M, cParuhAy); }
            // Paruh bawah
            { glm::mat4 M = headM * TRS({0, -0.054f, 0.082f}, {0.038f, 0.022f, 0.055f});
              draw(mBox, progStd, M, cParuhAy); }

            // Mata (2 sisi)
            for(int s : {-1,1}){
                glm::mat4 M = headM * TRS({s*0.068f, 0.022f, 0.076f}, {0.028f,0.028f,0.022f});
                draw(mAyamKep, progStd, M, {0.05f,0.05f,0.05f});
                // Sclera putih kecil
                glm::mat4 Mw = headM * TRS({s*0.075f, 0.030f, 0.090f}, {0.010f,0.010f,0.010f});
                draw(mAyamKep, progStd, Mw, {0.92f,0.92f,0.92f});
            }

            // Jengger (3 lobus)
            for(int j = 0; j < 3; j++){
                float jx = (j - 1) * 0.022f;
                float jh = (j == 1) ? 0.075f : 0.052f;
                glm::mat4 M = headM * TRS({jx, 0.078f + jh*0.3f, 0.005f}, {0.032f, jh, 0.032f});
                draw(mAyamKep, progStd, M, cJengger);
            }

            // Pial (2 bulatan di bawah paruh)
            for(int s : {-1,1}){
                glm::mat4 M = headM * TRS({s*0.022f, -0.072f, 0.055f}, {0.028f, 0.042f, 0.028f});
                draw(mAyamKep, progStd, M, cPialAy);
            }

            // ─ Ekor (3 bulu melengkung) ─
            for(int e = 0; e < 3; e++){
                float exo = (e - 1) * 0.055f;
                float eang = -38.0f - e * 9.0f;
                glm::mat4 M = rootAyam * TRS({exo, 0.10f, -0.19f});
                M = glm::rotate(M, glm::radians(eang), {1,0,0});
                M = glm::scale(M, {0.055f, 0.14f, 0.065f});
                draw(mAyamEkor, progStd, M,
                     {cAyamCol.r*0.72f, cAyamCol.g*0.70f, cAyamCol.b*0.58f});
            }
        }

        // Title bar
        std::string ttl="Simulasi Peternakan | F=Pakan  H=Panen  WASD+QE=Gerak  RClick=Putar | 50 Ayam";
        if(simState==SimState::FEEDING)    ttl+="  [MEMBERI PAKAN 1-1...]";
        if(simState==SimState::HARVESTING) ttl+="  [PANEN TELUR 1-1...]";
        glfwSetWindowTitle(win,ttl.c_str());

        glfwSwapBuffers(win);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}
