#include "Planner.hpp"

#include <algorithm>
#include <queue>

namespace am {

// ------------------------------------------------------------------ ordenacao
std::vector<Vec3i> orderCells(std::vector<Vec3i> cells, const TemplateParams& p, Vec3i start) {
    if (cells.empty()) return cells;

    if (p.kind != TemplateKind::Custom) {
        const Vec3i fw = forwardOf(p.facing), rt = rightOf(p.facing);
        auto fwdOf = [&](const Vec3i& c) {
            const Vec3i d = c - p.origin;
            return d.x * fw.x + d.z * fw.z;
        };
        auto latOf = [&](const Vec3i& c) {
            const Vec3i d = c - p.origin;
            return d.x * rt.x + d.z * rt.z;
        };
        std::sort(cells.begin(), cells.end(), [&](const Vec3i& a, const Vec3i& b) {
            const int fa = fwdOf(a), fb = fwdOf(b);
            if (fa != fb) return fa < fb;
            if (a.y != b.y) return a.y > b.y;  // de cima para baixo
            return latOf(a) < latOf(b);
        });
        return cells;
    }

    const Box b = boundsOf(cells);
    const bool zAsc = std::abs(start.z - b.min.z) <= std::abs(start.z - b.max.z);
    const bool xAsc = std::abs(start.x - b.min.x) <= std::abs(start.x - b.max.x);
    auto zKey = [&](const Vec3i& c) { return zAsc ? c.z - b.min.z : b.max.z - c.z; };
    auto xKey = [&](const Vec3i& c) { return xAsc ? c.x - b.min.x : b.max.x - c.x; };
    auto flip = [&](const Vec3i& c) { return ((zKey(c) + (b.max.y - c.y)) & 1) != 0; };  // serpentina
    std::sort(cells.begin(), cells.end(), [&](const Vec3i& a, const Vec3i& c) {
        if (a.y != c.y) return a.y > c.y;
        const int za = zKey(a), zc = zKey(c);
        if (za != zc) return za < zc;
        const int xa = flip(a) ? -xKey(a) : xKey(a), xc = flip(c) ? -xKey(c) : xKey(c);
        return xa < xc;
    });
    return cells;
}

// ------------------------------------------------------------------ cache
BlockClass WorldCache::blockAt(Vec3i c) {
    const auto it = cache_.find(c);
    if (it != cache_.end()) return it->second;
    const BlockClass v = src_ ? src_->blockAt(c) : BlockClass::Unknown;
    cache_.emplace(c, v);
    return v;
}

// ------------------------------------------------------------------ regras
bool standable(IWorld& w, Vec3i c) {
    if (w.blockAt(c) != BlockClass::Air) return false;
    if (w.blockAt({c.x, c.y + 1, c.z}) != BlockClass::Air) return false;
    if (!isSupport(w.blockAt({c.x, c.y - 1, c.z}))) return false;
    static constexpr Vec3i kSide[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vec3i& s : kSide)
        for (int dy = 0; dy <= 1; ++dy)
            if (w.blockAt({c.x + s.x, c.y + dy, c.z + s.z}) == BlockClass::Hazard) return false;
    return true;
}

bool touchesHazard(IWorld& w, Vec3i c, bool orLiquid) {
    static constexpr Vec3i kN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vec3i& n : kN) {
        const BlockClass k = w.blockAt(c + n);
        if (k == BlockClass::Hazard || (orLiquid && k == BlockClass::Liquid)) return true;
    }
    return false;
}

bool inReach(const Vec3f& eye, Vec3i target, float reach) {
    const Vec3f d = centerOf(target) - eye;
    return dot(d, d) <= reach * reach;
}

bool lineOfSight(IWorld& w, const Vec3f& eye, Vec3i target, Vec3i* blocker) {
    const Vec3f to = centerOf(target);
    const Vec3f d = to - eye;
    const float dist = length(d);
    if (dist < 1e-4f) return true;
    const Vec3f step = d * (0.05f / dist);
    const int n = static_cast<int>(dist / 0.05f) + 1;
    Vec3f p = eye;
    Vec3i last = cellOf(eye);
    for (int i = 0; i <= n; ++i, p = p + step) {
        const Vec3i cell = cellOf(p);
        if (cell == last && i != 0) continue;
        last = cell;
        if (cell == target) return true;
        const BlockClass k = w.blockAt(cell);
        if (k == BlockClass::Air || k == BlockClass::Liquid) continue;
        if (blocker) *blocker = cell;
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ caminho
PathResult findPath(IWorld& w, Vec3i start, const GoalFn& goal, int nodeCap, int maxFall,
                    std::chrono::steady_clock::time_point deadline) {
    struct Node {
        float cost;
        Vec3i parent;
        bool hasParent;
        bool closed;
    };
    struct QItem {
        float cost;
        unsigned seq;
        Vec3i cell;
        bool operator>(const QItem& o) const { return cost != o.cost ? cost > o.cost : seq > o.seq; }
    };
    PathResult res;
    std::unordered_map<Vec3i, Node, Vec3iHash> nodes;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<>> open;
    unsigned seq = 0;
    nodes[start] = {0.0f, {}, false, false};
    open.push({0.0f, seq++, start});

    static constexpr Vec3i kDir[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};

    while (!open.empty()) {
        const QItem cur = open.top();
        open.pop();
        Node& cn = nodes[cur.cell];
        if (cn.closed) continue;
        cn.closed = true;

        if (goal(cur.cell)) {
            res.found = true;
            for (Vec3i c = cur.cell;;) {
                res.cells.push_back(c);
                const Node& n = nodes[c];
                if (!n.hasParent) break;
                c = n.parent;
            }
            std::reverse(res.cells.begin(), res.cells.end());
            return res;
        }
        if (++res.expanded >= nodeCap) break;
        if ((res.expanded & 31) == 0 && std::chrono::steady_clock::now() > deadline) {
            res.timedOut = true;
            break;
        }

        const float baseCost = cn.cost;
        for (const Vec3i& d : kDir) {
            const Vec3i flat = cur.cell + d;
            // 1) andar no mesmo nivel
            // 2) subir 1: precisa de folga acima da cabeca atual
            // 3) descer ate maxFall: cabeca passa pelo vizinho
            for (int dy = 1; dy >= -maxFall; --dy) {
                const Vec3i next{flat.x, cur.cell.y + dy, flat.z};
                if (dy == 1 && w.blockAt({cur.cell.x, cur.cell.y + 2, cur.cell.z}) != BlockClass::Air) continue;
                if (dy < 0) {
                    bool clear = true;
                    for (int y = next.y; y <= cur.cell.y + 1 && clear; ++y)
                        clear = w.blockAt({flat.x, y, flat.z}) == BlockClass::Air;
                    if (!clear) continue;
                }
                if (!standable(w, next)) continue;
                const float nc = baseCost + 1.0f + (dy != 0 ? 1.0f : 0.0f);
                auto it = nodes.find(next);
                if (it != nodes.end() && it->second.cost <= nc) continue;
                nodes[next] = {nc, cur.cell, true, false};
                open.push({nc, seq++, next});
                break;  // so o primeiro passo valido por direcao (evita degraus duplicados)
            }
        }
    }
    return res;
}

}  // namespace am
