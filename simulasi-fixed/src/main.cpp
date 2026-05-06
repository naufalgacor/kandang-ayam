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

// Grid: 5 baris x 5 kolom per sisi (kiri + kanan = 50)
constexpr int   BARIS_AYAM = 5;
constexpr int   KOLOM_AYAM = 5;
constexpr float JARAK_X    = 1.3f;
constexpr float JARAK_Z    = 1.6f;

constexpr float LORONG_W   = 2.8f;
constexpr float OFFSET_SISI = LORONG_W * 0.5f + 0.5f;
constexpr float HALF_W     = OFFSET_SISI + KOLOM_AYAM * JARAK_X + 1.0f;
constexpr float TOTAL_W    = HALF_W * 2.0f;
constexpr float TOTAL_D    = BARIS_AYAM * JARAK_Z + 2.0f;

constexpr int   JML_SEKSI  = 5;
constexpr float TINGGI_DINDING = 2.6f;

// ──────────────────────────────────────────────────────────
//  KAMERA
// ──────────────────────────────────────────────────────────
glm::vec3 cameraPos   = {0.0f, 7.0f, 28.0f};
glm::vec3 cameraFront = {0.0f, -0.22f, -1.0f};
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
    glm::vec3 pos      = {0,0,4};
    float     rot      = 180.0f;
    float     walkAnim = 0.0f;
    float     bendAngle= 0.0f;
    bool      walking  = false;
    float     targetZ  = 0;
    float     speed    = 3.5f;
    int       phase    = 0;
    float     phaseTimer = 0.0f;
    int       currentChickenIdx = 0;  // tracking ayam yg sedang diproses
    int       totalChickenProcessed = 0;
} peternak;

// ──────────────────────────────────────────────────────────
//  GEROBAK
// ──────────────────────────────────────────────────────────
struct Gerobak {
    glm::vec3 pos       = {0,0,5};
    bool      active    = false;
    bool      returning = false;
    float     targetZ   = -(TOTAL_D - 1.5f);
    float     speed     = 4.5f;
    float     wheelAngle= 0.0f;
    int       eggsHarvested = 0;  // jumlah telur yg sudah dipanen
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
        for (int k = 0; k < KOLOM_AYAM && idx < NUM_AYAM; k++)
            ayamBasePos[idx++] = {-(OFFSET_SISI + k*JARAK_X), 0.22f, z};
        for (int k = 0; k < KOLOM_AYAM && idx < NUM_AYAM; k++)
            ayamBasePos[idx++] = { OFFSET_SISI + k*JARAK_X,  0.22f, z};
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
        for (int i=0; i<NUM_AYAM; i++) {
            ayamFeeding[i]  = false;
            ayamBobPhase[i] = ((float)rand()/RAND_MAX)*6.28f;
            eggs[i].visible   = false;
            eggs[i].spawnTime = now + 0.5f;
        }
        peternak.currentChickenIdx = 0;
        peternak.totalChickenProcessed = 0;
        peternak = {glm::vec3{0,0,4}, 180.0f, 0, 0, true,
                    ayamBasePos[0].z, 5.0f, 100, now, 0, 0};
    }

    if (key == GLFW_KEY_H && simState == SimState::IDLE) {
        simState = SimState::HARVESTING;
        gerobak  = {glm::vec3{0,0,4}, true, false, -(TOTAL_D-1.5f), 4.5f, 0, 0};
        harvestDone = false;
        peternak.currentChickenIdx = 0;
        peternak.totalChickenProcessed = 0;
        peternak = {glm::vec3{0.6f,0,4}, 180.0f, 0, 0, true, 0, 5.0f, 200, now, 0, 0};
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
//  GAMBAR PETERNAK
// ──────────────────────────────────────────────────────────
static void drawPeternak(unsigned int prog,
    Mesh& mBox,Mesh& mCyl,Mesh& mSphere,
    glm::vec3 pos,float rotY,float walkAnim,float bendDeg,
    glm::vec3 cBaju,glm::vec3 cKulit,glm::vec3 cCelana)
{
    glm::mat4 root=glm::translate(glm::mat4(1),pos);
    root=glm::rotate(root,glm::radians(rotY),{0,1,0});

    float legSw=sinf(walkAnim)*22.0f;
    float armSw=sinf(walkAnim+3.14159f)*18.0f;
    float bendRad=glm::radians(bendDeg);

    // Kaki kiri
    {glm::mat4 M=root*glm::translate(glm::mat4(1),{-0.11f,0.45f,0});
     M=glm::rotate(M,glm::radians(legSw),{1,0,0});
     M=glm::translate(M,{0,-0.225f,0});M=glm::scale(M,{0.17f,0.45f,0.17f});
     draw(mBox,prog,M,cCelana);}
    // Kaki kanan
    {glm::mat4 M=root*glm::translate(glm::mat4(1),{0.11f,0.45f,0});
     M=glm::rotate(M,glm::radians(-legSw),{1,0,0});
     M=glm::translate(M,{0,-0.225f,0});M=glm::scale(M,{0.17f,0.45f,0.17f});
     draw(mBox,prog,M,cCelana);}

    // Torso (dengan bungkuk)
    glm::mat4 torso=root*glm::translate(glm::mat4(1),{0,0.90f,0});
    torso=glm::rotate(torso,bendRad,{1,0,0});
    {glm::mat4 M=torso*glm::scale(glm::mat4(1),{0.34f,0.48f,0.20f});
     draw(mBox,prog,M,cBaju);}

    // Lengan kiri
    {glm::mat4 M=torso*glm::translate(glm::mat4(1),{-0.22f,0.08f,0});
     M=glm::rotate(M,glm::radians(-armSw-bendDeg*0.5f),{1,0,0});
     M=glm::translate(M,{0,-0.17f,0});M=glm::scale(M,{0.12f,0.34f,0.12f});
     draw(mBox,prog,M,cBaju);}
    // Lengan kanan
    {glm::mat4 M=torso*glm::translate(glm::mat4(1),{0.22f,0.08f,0});
     M=glm::rotate(M,glm::radians(armSw-bendDeg*0.5f),{1,0,0});
     M=glm::translate(M,{0,-0.17f,0});M=glm::scale(M,{0.12f,0.34f,0.12f});
     draw(mBox,prog,M,cBaju);}

    // Kepala
    {glm::mat4 M=torso*glm::translate(glm::mat4(1),{0,0.47f,0});
     M=glm::scale(M,{0.21f,0.23f,0.21f});draw(mSphere,prog,M,cKulit);}

    // Topi brim
    {glm::mat4 M=torso*TRS({0,0.64f,0},{0.26f,0.04f,0.26f});
     draw(mBox,prog,M,{0.28f,0.16f,0.06f});}
    // Topi tinggi
    {glm::mat4 M=torso*TRS({0,0.70f,0},{0.17f,0.10f,0.17f});
     draw(mBox,prog,M,{0.28f,0.16f,0.06f});}
}

// ──────────────────────────────────────────────────────────
//  GAMBAR GEROBAK
// ──────────────────────────────────────────────────────────
static void drawGerobak(unsigned int prog,Mesh& mBox,Mesh& mCyl,
                        glm::vec3 gp,float wAngle)
{
    glm::vec3 cW={0.52f,0.33f,0.14f};
    glm::vec3 cB={0.18f,0.18f,0.18f};
    // Bak
    {glm::mat4 M=TRS(gp+glm::vec3{0,0.50f,0},{0.80f,0.38f,0.55f});
     draw(mBox,prog,M,cW);}
    // Roda
    {glm::mat4 M=glm::translate(glm::mat4(1),gp+glm::vec3{0,0.27f,0.44f});
     M=glm::rotate(M,glm::radians(wAngle),{1,0,0});
     M=glm::rotate(M,glm::radians(90.0f),{0,0,1});
     M=glm::scale(M,{0.27f,0.07f,0.27f});
     draw(mCyl,prog,M,cB);}
    // Tongkat kiri & kanan
    for(int s:{-1,1}){
        glm::mat4 M=glm::translate(glm::mat4(1),gp+glm::vec3{s*0.30f,0.55f,-0.30f});
        M=glm::rotate(M,glm::radians(-30.0f),{1,0,0});
        M=glm::scale(M,{0.04f,0.95f,0.04f});
        draw(mBox,prog,M,cW);}
    // Kaki bawah
    for(int s:{-1,1}){
        glm::mat4 M=glm::translate(glm::mat4(1),gp+glm::vec3{s*0.27f,0.14f,-0.16f});
        M=glm::rotate(M,glm::radians(18.0f),{1,0,0});
        M=glm::scale(M,{0.05f,0.38f,0.05f});
        draw(mBox,prog,M,cB);}
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

        // Feeding - peternak memberi pakan satu-satu
        if(simState==SimState::FEEDING){
            // Peternak fase 100+i: berjalan ke ayam ke-i
            if(peternak.phase >= 100 && peternak.phase < 100+NUM_AYAM){
                int idx = peternak.phase - 100;
                if(idx < NUM_AYAM){
                    glm::vec3 targetPos = ayamBasePos[idx];
                    float dist = glm::distance(peternak.pos, targetPos);
                    
                    // Jalan ke ayam
                    if(dist > 0.25f){
                        glm::vec3 dir = glm::normalize(targetPos - peternak.pos);
                        peternak.pos += dir * peternak.speed * deltaTime;
                        peternak.walkAnim += deltaTime * 6.0f;
                        peternak.rot = glm::degrees(atan2f(dir.x, -dir.z));
                        peternak.walking = true;
                    } else {
                        // Sudah sampai, beri pakan (bungkuk)
                        float el = now - peternak.phaseTimer;
                        if(el < 0.1f) peternak.phaseTimer = now;  // Set timer saat pertama sampai
                        
                        el = now - peternak.phaseTimer;
                        peternak.bendAngle = 25.0f * sinf(el * 3.5f);
                        ayamBobs[idx] = 0.08f * sinf(el * 8.0f);
                        ayamFeeding[idx] = true;
                        
                        if(el > 1.2f){
                            // Selesai memberi pakan ke ayam ini, lanjut ke ayam berikutnya
                            ayamFeeding[idx] = false;
                            ayamBobs[idx] = 0;
                            peternak.bendAngle = 0;
                            peternak.phase++;
                            peternak.phaseTimer = now;
                            peternak.totalChickenProcessed++;
                            
                            // Telur muncul setelah diberi pakan
                            eggs[idx].visible = true;
                            eggs[idx].spawnTime = now + 0.2f;
                        }
                    }
                }
            }
            
            // Setelah selesai memberi pakan semua ayam
            if(peternak.phase >= 100 + NUM_AYAM){
                peternak.phase = 0;
                simState = SimState::IDLE;
                peternak.walking = false;
                peternak.bendAngle = 0;
            }
            
            glBindBuffer(GL_ARRAY_BUFFER, vboBob);
            glBufferSubData(GL_ARRAY_BUFFER, 0, NUM_AYAM*sizeof(float), ayamBobs);
        }

        // Harvesting - peternak mengambil telur satu-satu
        if(simState==SimState::HARVESTING){
            // Gerobak bergerak
            if(gerobak.active){
                float dir = gerobak.returning ? 1.0f : -1.0f;
                gerobak.pos.z += dir * gerobak.speed * deltaTime;
                gerobak.wheelAngle += gerobak.speed * deltaTime * 180.0f;
                
                // Peternak mengambil telur satu-satu
                if(peternak.phase >= 200 && peternak.phase < 200+NUM_AYAM){
                    int idx = peternak.phase - 200;
                    if(idx < NUM_AYAM && eggs[idx].visible){
                        // Posisi telur
                        glm::vec3 eggPos = eggs[idx].pos;
                        float dist = glm::distance(peternak.pos, eggPos);
                        
                        // Jalan ke telur
                        if(dist > 0.2f){
                            glm::vec3 dir = glm::normalize(eggPos - peternak.pos);
                            peternak.pos += dir * peternak.speed * deltaTime;
                            peternak.walkAnim += deltaTime * 6.0f;
                            peternak.rot = glm::degrees(atan2f(dir.x, -dir.z));
                            peternak.walking = true;
                        } else {
                            // Ambil telur
                            float el = now - peternak.phaseTimer;
                            if(el < 0.1f) peternak.phaseTimer = now;
                            
                            el = now - peternak.phaseTimer;
                            peternak.bendAngle = 15.0f * sinf(el * 4.0f);
                            
                            if(el > 0.8f){
                                // Telur sudah diambil
                                eggs[idx].visible = false;
                                gerobak.eggsHarvested++;
                                peternak.bendAngle = 0;
                                peternak.phase++;
                                peternak.phaseTimer = now;
                                peternak.totalChickenProcessed++;
                            }
                        }
                    } else {
                        peternak.phase++;
                        peternak.phaseTimer = now;
                    }
                }
                
                // Setelah selesai mengambil semua telur
                if(peternak.phase >= 200 + NUM_AYAM){
                    gerobak.returning = true;
                    harvestDone = true;
                }
                
                // Gerobak kembali
                if(gerobak.returning && gerobak.pos.z >= 6.5f){
                    gerobak.active = false;
                    simState = SimState::IDLE;
                    peternak.phase = 0;
                    peternak.walking = false;
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

        // Dinding kawat tipis kiri & kanan
        for(int s=0;s<JML_SEKSI;s++){
            float zM=-(s*seksiD+seksiD*0.5f);
            float len=seksiD;
            draw(mWall,progStd,TRS({-HALF_W,TINGGI_DINDING*0.5f,zM},{0.04f,TINGGI_DINDING,len},0,90),cKawat);
            draw(mWall,progStd,TRS({ HALF_W,TINGGI_DINDING*0.5f,zM},{0.04f,TINGGI_DINDING,len},0,90),cKawat);
        }
        // Dinding depan & belakang
        draw(mWall,progStd,TRS({0,TINGGI_DINDING*0.5f,0.1f},{TOTAL_W,TINGGI_DINDING,0.04f}),cKawat);
        draw(mWall,progStd,TRS({0,TINGGI_DINDING*0.5f,-(TOTAL_D-0.1f)},{TOTAL_W,TINGGI_DINDING,0.04f}),cKawat);

        // Atap per seksi (miring + ridge)
        for(int s=0;s<JML_SEKSI;s++){
            float zM=-(s*seksiD+seksiD*0.5f);
            float sD=seksiD+0.28f;
            // Kiri
            {glm::mat4 M=glm::translate(glm::mat4(1),{-HALF_W*0.5f,TINGGI_DINDING+0.58f,zM});
             M=glm::rotate(M,glm::radians(22.0f),{0,0,1});
             M=glm::scale(M,{HALF_W+0.65f,0.13f,sD});draw(mRoofSlb,progStd,M,cGenteng);}
            // Kanan
            {glm::mat4 M=glm::translate(glm::mat4(1),{HALF_W*0.5f,TINGGI_DINDING+0.58f,zM});
             M=glm::rotate(M,glm::radians(-22.0f),{0,0,1});
             M=glm::scale(M,{HALF_W+0.65f,0.13f,sD});draw(mRoofSlb,progStd,M,cGenteng);}
            // Ridge
            draw(mRoofRdg,progStd,TRS({0,TINGGI_DINDING+1.02f,zM},{1,1,sD}),cKayuTua);
        }
        // Overhang depan & belakang
        for(float zo:{0.32f,-(TOTAL_D+0.02f)}){
            for(int sd:{-1,1}){
                glm::mat4 M=glm::translate(glm::mat4(1),{sd*HALF_W*0.5f,TINGGI_DINDING+0.55f,zo});
                M=glm::rotate(M,glm::radians((float)sd*22.0f),{0,0,1});
                M=glm::scale(M,{HALF_W+0.65f,0.13f,0.52f});draw(mRoofSlb,progStd,M,cGenteng);}}

        // ── SEKAT BARIS ──
        for(int b=0;b<=BARIS_AYAM;b++){
            float z=-(b*JARAK_Z+0.8f);
            for(int sd:{-1,1}){
                float cx=sd*(OFFSET_SISI+(KOLOM_AYAM*JARAK_X)*0.5f-0.2f);
                draw(mWall,progStd,TRS({cx,0.28f,z},{KOLOM_AYAM*JARAK_X,0.5f,0.06f}),cKayu);}}

        // ── TEMPAT PAKAN ──
        float pakanW=KOLOM_AYAM*JARAK_X*0.95f;
        float pakanX=OFFSET_SISI+(KOLOM_AYAM*JARAK_X)*0.5f-0.3f;
        for(int b=0;b<BARIS_AYAM;b++){
            float z=-(b*JARAK_Z+1.0f);
            draw(mPakan,progStd,TRS({-pakanX,0.10f,z},{pakanW,1,1}),cPakanCol);
            draw(mPakan,progStd,TRS({ pakanX,0.10f,z},{pakanW,1,1}),cPakanCol);}

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
        if(gerobak.active)
            drawGerobak(progStd,mBox,mCyl,gerobak.pos,gerobak.wheelAngle);

        // ── PETERNAK ──
        if(simState!=SimState::IDLE||peternak.phase!=0){
            drawPeternak(progStd,mBox,mCyl,mSphere,
                peternak.pos,peternak.rot,
                peternak.walking?peternak.walkAnim:0.0f,
                peternak.bendAngle,
                {0.22f,0.42f,0.18f},{0.88f,0.68f,0.48f},{0.25f,0.25f,0.38f});}

        // ── AYAM instancing ──
        glUseProgram(progInst);
        setMat4(progInst,"view",view);setMat4(progInst,"projection",proj);
        setVec3(progInst,"objectColor",cAyamCol);setLight(progInst);
        glBindVertexArray(mAyam.VAO);
        glDrawElementsInstanced(GL_TRIANGLES,(GLsizei)mAyam.indexCount,GL_UNSIGNED_INT,nullptr,NUM_AYAM);
        glBindVertexArray(0);

        // Kepala + jengger + ekor ayam (per-instance manual — cukup cepat)
        glUseProgram(progStd);
        setMat4(progStd,"view",view);setMat4(progStd,"projection",proj);setLight(progStd);
        for(int i=0;i<NUM_AYAM;i++){
            float bob=ayamBobs[i];
            glm::vec3 bp=ayamBasePos[i]+glm::vec3{0,bob,0};
            
            // Kaki kiri
            {glm::mat4 M=glm::translate(glm::mat4(1),bp+glm::vec3{-0.08f,-0.05f,0.05f});
             M=glm::scale(M,{0.03f,0.12f,0.03f});
             draw(mBox,progStd,M,{0.55f,0.35f,0.15f});}
            // Kaki kanan
            {glm::mat4 M=glm::translate(glm::mat4(1),bp+glm::vec3{0.08f,-0.05f,0.05f});
             M=glm::scale(M,{0.03f,0.12f,0.03f});
             draw(mBox,progStd,M,{0.55f,0.35f,0.15f});}
            
            // Kepala
            {glm::mat4 M=glm::translate(glm::mat4(1),bp+glm::vec3{0,0.19f,0.17f});
             M=glm::scale(M,{0.13f,0.13f,0.13f});draw(mAyamKep,progStd,M,cKrem);}
            // Jengger
            {glm::mat4 M=glm::translate(glm::mat4(1),bp+glm::vec3{0,0.29f,0.19f});
             M=glm::scale(M,{0.045f,0.08f,0.045f});draw(mAyamKep,progStd,M,cJengger);}
            // Mata
            {glm::mat4 M=glm::translate(glm::mat4(1),bp+glm::vec3{0.04f,0.22f,0.26f});
             M=glm::scale(M,{0.02f,0.02f,0.02f});draw(mAyamKep,progStd,M,{0.0f,0.0f,0.0f});}
            // Ekor
            {glm::mat4 M=glm::translate(glm::mat4(1),bp+glm::vec3{0,0.10f,-0.19f});
             M=glm::rotate(M,glm::radians(-30.0f),{1,0,0});
             M=glm::scale(M,{0.09f,0.14f,0.11f});
             draw(mAyamEkor,progStd,M,cAyamCol);}
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
