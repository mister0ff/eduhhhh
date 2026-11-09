// Geometria 2D/3D do overlay (outline do perimetro + HUD/menu), 100% em CPU e sem OpenGL.
// Tudo vira triangulos em coordenadas de pixel; GlRenderer so envia isso para a GPU.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Settings.hpp"
#include "Types.hpp"

namespace am {

struct Color {
    float r{}, g{}, b{}, a{1};
};
struct Vertex {
    float x, y;  // pixels (origem no canto superior esquerdo)
    float r, g, b, a;
};
struct Batch {
    std::vector<Vertex> v;
    void clear() { v.clear(); }
};

struct Camera {
    Vec3f eye{};
    float yaw{}, pitch{};
    float fovYDeg{70.0f};
    int width{1}, height{1};
};

// Projeta ponto do mundo para a tela. false se estiver atras da camera.
bool project(const Camera& cam, const Vec3f& p, float& sx, float& sy);

void addTri(Batch& b, float x0, float y0, float x1, float y1, float x2, float y2, const Color& c);
void addRect(Batch& b, float x, float y, float w, float h, const Color& c);
// segmento 3D com espessura em pixels (recorta no plano near)
void addLine3D(Batch& b, const Camera& cam, const Vec3f& a, const Vec3f& c, float thickness, const Color& col);
void addText(Batch& b, float x, float y, int scale, std::string_view text, const Color& c);
float textWidth(std::string_view text, int scale);

// O que o outline precisa saber (publicado pelo Controller, lido pelo thread de render)
struct OverlayData {
    bool preview{false};
    bool running{false};
    bool hasBounds{false};
    Box bounds{};
    std::vector<Vec3i> cells;    // ate N proximos blocos
    bool hasTarget{false};
    Vec3i target{};
    std::vector<Vec3i> skipped;  // blocos ignorados (vermelho)
    bool pos1Set{false}, pos2Set{false};
    Vec3i pos1{}, pos2{};
};

void buildOutline(Batch& out, const Camera& cam, const OverlayData& d, const Settings& s);
void buildHud(Batch& out, int width, int height, const Status& st, const Settings& s);

const char* hudTemplateLabel(int idx);

}  // namespace am
