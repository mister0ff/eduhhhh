#include "Templates.hpp"

#include <algorithm>

namespace am {

const char* templateName(TemplateKind k) {
    switch (k) {
        case TemplateKind::Box2x2: return "2x2";
        case TemplateKind::Box3x3: return "3x3";
        case TemplateKind::Stairs: return "Stairs";
        case TemplateKind::Tunnel: return "Tunnel";
        case TemplateKind::Custom: return "Pos1-Pos2";
    }
    return "?";
}

const char* templateLabelPt(TemplateKind k) {
    switch (k) {
        case TemplateKind::Box2x2: return "2x2";
        case TemplateKind::Box3x3: return "3x3";
        case TemplateKind::Stairs: return "Escada (Stairs)";
        case TemplateKind::Tunnel: return "Tunel (Tunnel)";
        case TemplateKind::Custom: return "Area Pos1 - Pos2";
    }
    return "?";
}

namespace {
// faixa lateral centrada: w=1 -> [0,0]; w=2 -> [0,1]; w=3 -> [-1,1]; w=4 -> [-1,2]
void lateralRange(int w, int& lo, int& hi) {
    lo = -((w - 1) / 2);
    hi = w / 2;
}

void addSlice(std::vector<Vec3i>& out, const TemplateParams& p, int fwdIndex, int baseY, int lo, int hi, int h) {
    const Vec3i fw = forwardOf(p.facing), rt = rightOf(p.facing);
    for (int w = lo; w <= hi; ++w)
        for (int y = 0; y < h; ++y) out.push_back(p.origin + fw * fwdIndex + rt * w + Vec3i{0, baseY + y, 0});
}

void fail(std::string* e, const char* msg) {
    if (e) *e = msg;
}
}  // namespace

std::vector<Vec3i> buildCells(const TemplateParams& p, std::string* error) {
    std::vector<Vec3i> out;
    if (error) error->clear();
    const int len = std::clamp(p.length, 1, kMaxLength);
    int lo = 0, hi = 0;

    switch (p.kind) {
        case TemplateKind::Box2x2:
            // 2 de largura (coluna do jogador + a da direita) x 2 de altura (pes e cabeca)
            for (int f = 1; f <= len; ++f) addSlice(out, p, f, 0, 0, 1, 2);
            break;
        case TemplateKind::Box3x3:
            for (int f = 1; f <= len; ++f) addSlice(out, p, f, 0, -1, 1, 3);
            break;
        case TemplateKind::Tunnel: {
            const int w = std::clamp(p.width, 1, 9), h = std::clamp(p.height, 1, 9);
            lateralRange(w, lo, hi);
            for (int f = 1; f <= len; ++f) addSlice(out, p, f, 0, lo, hi, h);
            break;
        }
        case TemplateKind::Stairs: {
            // Cada degrau avanca 1 e desce/sobe 1. Libera 3 de altura a partir do piso do degrau
            // (pes + cabeca + 1 de folga para o pulo / queda).
            const int w = std::clamp(p.width, 1, 9);
            lateralRange(w, lo, hi);
            // subindo, o 1o pulo precisa de folga acima da cabeca na coluna do proprio jogador
            if (!p.stairsDown) addSlice(out, p, 0, 2, lo, hi, 1);
            for (int f = 1; f <= len; ++f) addSlice(out, p, f, p.stairsDown ? -f : f, lo, hi, 3);
            break;
        }
        case TemplateKind::Custom: {
            const Box b{{std::min(p.pos1.x, p.pos2.x), std::min(p.pos1.y, p.pos2.y), std::min(p.pos1.z, p.pos2.z)},
                        {std::max(p.pos1.x, p.pos2.x), std::max(p.pos1.y, p.pos2.y), std::max(p.pos1.z, p.pos2.z)}};
            if (b.volume() > kMaxCells) {
                fail(error, "AREA GRANDE DEMAIS");
                return {};
            }
            for (int y = b.min.y; y <= b.max.y; ++y)
                for (int z = b.min.z; z <= b.max.z; ++z)
                    for (int x = b.min.x; x <= b.max.x; ++x) out.push_back({x, y, z});
            break;
        }
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (static_cast<long long>(out.size()) > kMaxCells) {
        fail(error, "AREA GRANDE DEMAIS");
        return {};
    }
    if (out.empty()) fail(error, "NADA PARA ESCAVAR");
    return out;
}

Box boundsOf(const std::vector<Vec3i>& cells) {
    if (cells.empty()) return {};
    Box b{cells.front(), cells.front()};
    for (const Vec3i& c : cells) {
        b.min = {std::min(b.min.x, c.x), std::min(b.min.y, c.y), std::min(b.min.z, c.z)};
        b.max = {std::max(b.max.x, c.x), std::max(b.max.y, c.y), std::max(b.max.z, c.z)};
    }
    return b;
}

}  // namespace am
