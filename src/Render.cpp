#include "Render.hpp"

#include "Templates.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

namespace am {

// ------------------------------------------------------------------ projecao
namespace {
struct Basis {
    Vec3f f, r, u;
};
Basis basisOf(const Camera& c) {
    Basis b;
    b.f = lookDir(c.yaw, c.pitch);
    b.r = cross(b.f, {0, 1, 0});
    const float l = length(b.r);
    b.r = l > 1e-6f ? b.r * (1.0f / l) : Vec3f{1, 0, 0};
    b.u = cross(b.r, b.f);
    return b;
}
constexpr float kNear = 0.05f;
}  // namespace

bool project(const Camera& cam, const Vec3f& p, float& sx, float& sy) {
    const Basis b = basisOf(cam);
    const Vec3f d = p - cam.eye;
    const float cz = dot(d, b.f);
    if (cz < kNear) return false;
    const float th = std::tan(cam.fovYDeg * kDeg2Rad * 0.5f);
    const float aspect = static_cast<float>(cam.width) / static_cast<float>(std::max(1, cam.height));
    const float nx = dot(d, b.r) / (cz * th * aspect), ny = dot(d, b.u) / (cz * th);
    sx = (nx * 0.5f + 0.5f) * static_cast<float>(cam.width);
    sy = (0.5f - ny * 0.5f) * static_cast<float>(cam.height);
    return true;
}

// ------------------------------------------------------------------ primitivas
void addTri(Batch& b, float x0, float y0, float x1, float y1, float x2, float y2, const Color& c) {
    b.v.push_back({x0, y0, c.r, c.g, c.b, c.a});
    b.v.push_back({x1, y1, c.r, c.g, c.b, c.a});
    b.v.push_back({x2, y2, c.r, c.g, c.b, c.a});
}

void addRect(Batch& b, float x, float y, float w, float h, const Color& c) {
    addTri(b, x, y, x + w, y, x + w, y + h, c);
    addTri(b, x, y, x + w, y + h, x, y + h, c);
}

void addLine3D(Batch& b, const Camera& cam, const Vec3f& a, const Vec3f& c, float thickness, const Color& col) {
    const Basis bs = basisOf(cam);
    float za = dot(a - cam.eye, bs.f), zc = dot(c - cam.eye, bs.f);
    if (za < kNear && zc < kNear) return;
    Vec3f pa = a, pc = c;
    if (za < kNear) pa = a + (c - a) * ((kNear - za) / (zc - za));
    else if (zc < kNear) pc = c + (a - c) * ((kNear - zc) / (za - zc));
    float x0, y0, x1, y1;
    if (!project(cam, pa, x0, y0) || !project(cam, pc, x1, y1)) {
        // ponto exatamente no plano near depois do recorte: empurra um pouco e tenta de novo
        pa = pa + bs.f * 0.001f;
        pc = pc + bs.f * 0.001f;
        if (!project(cam, pa, x0, y0) || !project(cam, pc, x1, y1)) return;
    }
    const float lim = 20000.0f;
    if (std::fabs(x0) > lim || std::fabs(y0) > lim || std::fabs(x1) > lim || std::fabs(y1) > lim) return;
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    addTri(b, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, col);
    addTri(b, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, col);
}

// ------------------------------------------------------------------ fonte 5x7
namespace {
struct Glyph {
    char c;
    std::uint8_t rows[7];
};
// 5 bits por linha, bit mais alto = pixel da esquerda
constexpr Glyph kFont[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {':', {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}}, {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}}, {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}}, {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'%', {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13}}, {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}}, {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}}, {',', {0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};

const std::uint8_t* glyphFor(char ch) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (const Glyph& g : kFont)
        if (g.c == u) return g.rows;
    return nullptr;
}
}  // namespace

float textWidth(std::string_view text, int scale) { return static_cast<float>(text.size() * 6 * scale); }

void addText(Batch& b, float x, float y, int scale, std::string_view text, const Color& c) {
    const float s = static_cast<float>(scale);
    float cx = x;
    for (char ch : text) {
        if (ch != ' ') {
            const std::uint8_t* g = glyphFor(ch);
            if (!g) g = glyphFor('?');
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g[row] & (0x10 >> col)) addRect(b, cx + col * s, y + row * s, s, s, c);
        }
        cx += 6.0f * s;
    }
}

// ------------------------------------------------------------------ outline
namespace {
std::uint64_t corner(int x, int y, int z) {
    constexpr int off = 1 << 20;
    return (static_cast<std::uint64_t>(x + off) << 42) | (static_cast<std::uint64_t>(y + off) << 21) |
           static_cast<std::uint64_t>(z + off);
}
Vec3f cornerPos(std::uint64_t k) {
    constexpr int off = 1 << 20;
    constexpr std::uint64_t m = (1u << 21) - 1;
    return {static_cast<float>(static_cast<int>(k >> 42) - off), static_cast<float>(static_cast<int>((k >> 21) & m) - off),
            static_cast<float>(static_cast<int>(k & m) - off)};
}
using Edge = std::pair<std::uint64_t, std::uint64_t>;

template <class F>
void forBoxEdges(Vec3i lo, Vec3i hiExclusive, F&& f) {
    const int x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hiExclusive.x, y1 = hiExclusive.y, z1 = hiExclusive.z;
    for (int y : {y0, y1})
        for (int z : {z0, z1}) f(corner(x0, y, z), corner(x1, y, z));  // arestas ao longo de X
    for (int x : {x0, x1})
        for (int z : {z0, z1}) f(corner(x, y0, z), corner(x, y1, z));  // ao longo de Y
    for (int x : {x0, x1})
        for (int y : {y0, y1}) f(corner(x, y, z0), corner(x, y, z1));  // ao longo de Z
}

void drawBox(Batch& out, const Camera& cam, Vec3i lo, Vec3i hiExclusive, float thick, const Color& c, float grow = 0.0f) {
    forBoxEdges(lo, hiExclusive, [&](std::uint64_t a, std::uint64_t b) {
        Vec3f pa = cornerPos(a), pb = cornerPos(b);
        const Vec3f mid{(static_cast<float>(lo.x) + static_cast<float>(hiExclusive.x)) * 0.5f,
                        (static_cast<float>(lo.y) + static_cast<float>(hiExclusive.y)) * 0.5f,
                        (static_cast<float>(lo.z) + static_cast<float>(hiExclusive.z)) * 0.5f};
        auto expand = [&](Vec3f p) {
            return Vec3f{p.x + (p.x > mid.x ? grow : -grow), p.y + (p.y > mid.y ? grow : -grow), p.z + (p.z > mid.z ? grow : -grow)};
        };
        if (grow != 0.0f) { pa = expand(pa); pb = expand(pb); }
        addLine3D(out, cam, pa, pb, thick, c);
    });
}
}  // namespace

void buildOutline(Batch& out, const Camera& cam, const OverlayData& d, const Settings& s) {
    if (!s.showOutline) return;
    const float px = std::max(1.0f, static_cast<float>(cam.height) / 540.0f);  // espessura escala com a tela
    const Color boundsCol = d.running ? Color{1.0f, 0.65f, 0.1f, 0.95f} : Color{0.2f, 0.9f, 1.0f, 0.95f};
    const Color cellCol = d.running ? Color{1.0f, 0.9f, 0.2f, 0.45f} : Color{0.3f, 0.8f, 1.0f, 0.40f};

    if (d.hasBounds) {
        drawBox(out, cam, d.bounds.min, d.bounds.max + Vec3i{1, 1, 1}, 3.0f * px, boundsCol);
        // grade por bloco (arestas compartilhadas desenhadas uma vez so)
        std::set<Edge> seen;
        constexpr std::size_t kMaxEdges = 6000;
        for (const Vec3i& c : d.cells) {
            if (seen.size() >= kMaxEdges) break;
            forBoxEdges(c, c + Vec3i{1, 1, 1}, [&](std::uint64_t a, std::uint64_t b) {
                if (a > b) std::swap(a, b);
                if (seen.insert({a, b}).second) addLine3D(out, cam, cornerPos(a), cornerPos(b), 1.2f * px, cellCol);
            });
        }
    }
    for (const Vec3i& c : d.skipped) drawBox(out, cam, c, c + Vec3i{1, 1, 1}, 2.5f * px, {1.0f, 0.15f, 0.15f, 0.9f}, 0.01f);
    if (d.hasTarget) drawBox(out, cam, d.target, d.target + Vec3i{1, 1, 1}, 3.5f * px, {0.2f, 1.0f, 0.3f, 1.0f}, 0.015f);
    if (d.pos1Set) drawBox(out, cam, d.pos1, d.pos1 + Vec3i{1, 1, 1}, 3.0f * px, {1.0f, 0.3f, 0.9f, 1.0f}, 0.02f);
    if (d.pos2Set) drawBox(out, cam, d.pos2, d.pos2 + Vec3i{1, 1, 1}, 3.0f * px, {0.4f, 0.5f, 1.0f, 1.0f}, 0.02f);
}

// ------------------------------------------------------------------ HUD
const char* hudTemplateLabel(int idx) {
    switch (idx) {
        case 0: return "2X2";
        case 1: return "3X3";
        case 2: return "ESCADA";
        case 3: return "TUNEL";
        case 4: return "AREA POS1-POS2";
    }
    return "?";
}

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string coord(Vec3i v) { return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z); }
const char* stateName(RunState s) {
    switch (s) {
        case RunState::Idle: return "PRONTO";
        case RunState::Planning: return "PLANEJANDO";
        case RunState::Navigating: return "ANDANDO";
        case RunState::Aiming: return "MIRANDO";
        case RunState::Breaking: return "QUEBRANDO";
        case RunState::Recovering: return "RECUPERANDO";
        case RunState::Verifying: return "VERIFICANDO";
        case RunState::Done: return "CONCLUIDO";
        case RunState::Aborted: return "PARADO";
    }
    return "?";
}
}  // namespace

void buildHud(Batch& out, int width, int height, const Status& st, const Settings& s) {
    if (!s.showHud) return;
    const bool running = st.state != RunState::Idle && st.state != RunState::Done && st.state != RunState::Aborted;
    if (!st.menuOpen && !running && !st.previewOn) return;

    const int sc = std::max(2, static_cast<int>(std::lround(static_cast<float>(height) / 360.0f)));
    const float lineH = 9.0f * static_cast<float>(sc), pad = 4.0f * static_cast<float>(sc);
    const Color white{1, 1, 1, 1}, dim{0.75f, 0.8f, 0.85f, 1}, accent{0.3f, 1.0f, 0.5f, 1}, warn{1.0f, 0.5f, 0.3f, 1};

    // linha de progresso (sempre que algo esta ativo)
    std::string prog = std::string("AUTOMINER ") + hudTemplateLabel(st.templateIdx);
    if (running) prog += " | " + std::to_string(st.done) + "/" + std::to_string(st.total) + " | " + stateName(st.state);
    else if (st.previewOn) prog += " | PREVIEW " + std::to_string(st.cells) + " BLOCOS";
    if (!st.message.empty() && !st.menuOpen) prog += " | " + st.message;

    if (!st.menuOpen) {
        const float w = textWidth(prog, sc) + 2 * pad;
        const float x = (static_cast<float>(width) - w) * 0.5f, y = pad;
        addRect(out, x, y, w, lineH + pad, {0, 0, 0, 0.55f});
        addText(out, x + pad, y + pad * 0.5f, sc, prog, running ? accent : white);
        if (running && st.total > 0) {
            const float frac = static_cast<float>(st.done + st.skipped) / static_cast<float>(st.total);
            addRect(out, x, y + lineH + pad, w, 3.0f * static_cast<float>(sc) * 0.5f, {0.2f, 0.2f, 0.2f, 0.7f});
            addRect(out, x, y + lineH + pad, w * std::clamp(frac, 0.0f, 1.0f), 3.0f * static_cast<float>(sc) * 0.5f, accent);
        }
        return;
    }

    // painel completo (menu)
    std::vector<std::pair<std::string, Color>> lines;
    lines.push_back({"AUTOMINER", accent});
    lines.push_back({"[" + upper(s.keyMenu) + "] FECHAR MENU", dim});
    for (int i = 0; i < kTemplateCount; ++i) lines.push_back({std::string(i == st.templateIdx ? "> " : "  ") + hudTemplateLabel(i), i == st.templateIdx ? white : dim});
    lines.push_back({"[" + upper(s.keyNext) + "] PROXIMO MODELO", dim});
    lines.push_back({"[" + upper(s.keyPos1) + "] POS1 " + (st.pos1Set ? coord(st.pos1) : "-"), st.pos1Set ? white : dim});
    lines.push_back({"[" + upper(s.keyPos2) + "] POS2 " + (st.pos2Set ? coord(st.pos2) : "-"), st.pos2Set ? white : dim});
    lines.push_back({"[" + upper(s.keyStart) + "] INICIAR   [" + upper(s.keyStop) + "] PARAR", dim});
    lines.push_back({std::string("ESTADO: ") + stateName(st.state) + (running ? " " + std::to_string(st.done) + "/" + std::to_string(st.total) : ""), running ? accent : white});
    if (!st.message.empty()) lines.push_back({upper(st.message), warn});
    if (!st.bridgeMissing.empty()) lines.push_back({"FALTA: " + upper(st.bridgeMissing), warn});

    float maxW = 0;
    for (const auto& l : lines) maxW = std::max(maxW, textWidth(l.first, sc));
    const float x = pad, y = pad, h = lineH * static_cast<float>(lines.size()) + pad * 2;
    addRect(out, x, y, maxW + pad * 2, h, {0, 0, 0, 0.62f});
    float ly = y + pad;
    for (std::size_t i = 0; i < lines.size(); ++i, ly += lineH) {
        if (i >= 2 && static_cast<int>(i) - 2 == st.templateIdx && st.templateIdx < kTemplateCount)
            addRect(out, x + pad * 0.5f, ly - pad * 0.25f, maxW + pad, lineH, {0.2f, 0.5f, 0.9f, 0.35f});
        addText(out, x + pad, ly, sc, lines[i].first, lines[i].second);
    }
}

}  // namespace am
