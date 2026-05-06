#pragma once
// ============================================================
//  Shapes.h  —  Generator mesh prosedural (tanpa file .obj)
//  Semua mesh kompatibel dengan Mesh.h (VAO, indexCount)
// ============================================================

#ifndef SHAPES_H
#define SHAPES_H

#include "Mesh.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ────────────────────────────────────────────────────────────
//  BOX  (axis-aligned, centered at origin)
//  w=lebar(X)  h=tinggi(Y)  d=kedalaman(Z)
// ────────────────────────────────────────────────────────────
inline Mesh createBox(float w, float h, float d) {
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;

    // 6 sisi, tiap sisi 4 vertex, tiap vertex = posisi + normal + UV
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;

    auto addFace = [&](glm::vec3 bl, glm::vec3 br, glm::vec3 tr, glm::vec3 tl, glm::vec3 n) {
        unsigned int base = (unsigned int)verts.size();
        verts.push_back({ bl, n, {0,0} });
        verts.push_back({ br, n, {1,0} });
        verts.push_back({ tr, n, {1,1} });
        verts.push_back({ tl, n, {0,1} });
        idx.insert(idx.end(), { base, base+1, base+2, base, base+2, base+3 });
    };

    // Front (+Z)
    addFace({-hw,-hh, hd},{hw,-hh, hd},{hw, hh, hd},{-hw, hh, hd},{0,0,1});
    // Back (-Z)
    addFace({ hw,-hh,-hd},{-hw,-hh,-hd},{-hw, hh,-hd},{ hw, hh,-hd},{0,0,-1});
    // Left (-X)
    addFace({-hw,-hh,-hd},{-hw,-hh, hd},{-hw, hh, hd},{-hw, hh,-hd},{-1,0,0});
    // Right (+X)
    addFace({ hw,-hh, hd},{ hw,-hh,-hd},{ hw, hh,-hd},{ hw, hh, hd},{1,0,0});
    // Top (+Y)
    addFace({-hw, hh, hd},{ hw, hh, hd},{ hw, hh,-hd},{-hw, hh,-hd},{0,1,0});
    // Bottom (-Y)
    addFace({-hw,-hh,-hd},{ hw,-hh,-hd},{ hw,-hh, hd},{-hw,-hh, hd},{0,-1,0});

    return Mesh(verts, idx, {});
}

// ────────────────────────────────────────────────────────────
//  SPHERE  (UV sphere, centered at origin)
//  r=radius  sectors=kolom  stacks=baris
// ────────────────────────────────────────────────────────────
inline Mesh createSphere(float r, int sectors, int stacks) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;

    float sStep = 2.0f * (float)M_PI / sectors;
    float tStep = (float)M_PI / stacks;

    for (int i = 0; i <= stacks; i++) {
        float phi = (float)M_PI / 2.0f - i * tStep;
        float y   = r * sinf(phi);
        float xy  = r * cosf(phi);
        for (int j = 0; j <= sectors; j++) {
            float theta = j * sStep;
            float x = xy * cosf(theta);
            float z = xy * sinf(theta);
            glm::vec3 pos(x, y, z);
            glm::vec3 norm = glm::normalize(pos);
            glm::vec2 uv((float)j / sectors, (float)i / stacks);
            verts.push_back({ pos, norm, uv });
        }
    }

    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < sectors; j++) {
            unsigned int k1 = i * (sectors + 1) + j;
            unsigned int k2 = k1 + sectors + 1;
            idx.insert(idx.end(), { k1, k2, k1+1, k1+1, k2, k2+1 });
        }
    }

    return Mesh(verts, idx, {});
}

// ────────────────────────────────────────────────────────────
//  CYLINDER  (sumbu Y, centered, tutup atas-bawah)
//  r=radius  h=tinggi  sectors=resolusi lingkaran
// ────────────────────────────────────────────────────────────
inline Mesh createCylinder(float r, float h, int sectors) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;

    float hh    = h * 0.5f;
    float step  = 2.0f * (float)M_PI / sectors;

    // Sisi selimut
    for (int i = 0; i <= sectors; i++) {
        float angle = i * step;
        float x = r * cosf(angle), z = r * sinf(angle);
        glm::vec3 n(cosf(angle), 0.0f, sinf(angle));
        float u = (float)i / sectors;
        verts.push_back({ {x, -hh, z}, n, {u, 0} });
        verts.push_back({ {x,  hh, z}, n, {u, 1} });
    }
    for (int i = 0; i < sectors; i++) {
        unsigned int b = i * 2;
        idx.insert(idx.end(), { b, b+2, b+1, b+1, b+2, b+3 });
    }

    // Tutup bawah
    unsigned int centerBot = (unsigned int)verts.size();
    verts.push_back({ {0, -hh, 0}, {0,-1,0}, {0.5f, 0.5f} });
    unsigned int botStart = centerBot + 1;
    for (int i = 0; i <= sectors; i++) {
        float a = i * step;
        verts.push_back({ {r*cosf(a), -hh, r*sinf(a)}, {0,-1,0},
                          {0.5f + 0.5f*cosf(a), 0.5f + 0.5f*sinf(a)} });
    }
    for (int i = 0; i < sectors; i++)
        idx.insert(idx.end(), { centerBot, botStart+i+1, botStart+i });

    // Tutup atas
    unsigned int centerTop = (unsigned int)verts.size();
    verts.push_back({ {0, hh, 0}, {0,1,0}, {0.5f, 0.5f} });
    unsigned int topStart = centerTop + 1;
    for (int i = 0; i <= sectors; i++) {
        float a = i * step;
        verts.push_back({ {r*cosf(a), hh, r*sinf(a)}, {0,1,0},
                          {0.5f + 0.5f*cosf(a), 0.5f + 0.5f*sinf(a)} });
    }
    for (int i = 0; i < sectors; i++)
        idx.insert(idx.end(), { centerTop, topStart+i, topStart+i+1 });

    return Mesh(verts, idx, {});
}

#endif // SHAPES_H
