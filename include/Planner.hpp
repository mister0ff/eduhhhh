// Ordenacao dos blocos, cache de mundo, pathfinding em grade e linha de visada.
#pragma once

#include <chrono>
#include <functional>
#include <unordered_map>
#include <vector>

#include "Bridge.hpp"
#include "Templates.hpp"

namespace am {

// ---- ordem de quebra ----------------------------------------------------------
// Templates: fatia por fatia para frente; dentro da fatia, de cima para baixo (evita cascalho/areia
// caindo no buraco ja aberto) e depois lateral.
// Custom: camada por camada de cima para baixo, em serpentina a partir do canto mais proximo de `start`.
std::vector<Vec3i> orderCells(std::vector<Vec3i> cells, const TemplateParams& p, Vec3i start);

// ---- mundo com cache (1 consulta ao jogo por celula por tick) ----------------------
class WorldCache : public IWorld {
public:
    void attach(IWorld* w) { src_ = w; }
    void clear() { cache_.clear(); }
    BlockClass blockAt(Vec3i c) override;
    std::size_t queries() const { return cache_.size(); }

private:
    IWorld* src_{};
    std::unordered_map<Vec3i, BlockClass, Vec3iHash> cache_;
};

// ---- regras de andar / alcance ------------------------------------------------------
inline bool isSupport(BlockClass c) { return c == BlockClass::Solid || c == BlockClass::Unbreakable; }

// pes em `c`: ar em c e c+1, piso solido em c-1, sem lava colada.
bool standable(IWorld& w, Vec3i c);
// algum vizinho (6 faces) e lava/fogo? (`orLiquid`: tambem agua) -> quebrar pode inundar/queimar.
bool touchesHazard(IWorld& w, Vec3i c, bool orLiquid);

bool inReach(const Vec3f& eye, Vec3i target, float reach);
// Raio olho -> centro do bloco. Ar/liquido sao transparentes. `blocker` recebe o 1o bloco que atrapalha.
bool lineOfSight(IWorld& w, const Vec3f& eye, Vec3i target, Vec3i* blocker);

struct PathResult {
    bool found{false};
    bool timedOut{false};
    int expanded{0};
    std::vector<Vec3i> cells;  // celulas dos pes, comecando em `start` (inclui-o)
};
using GoalFn = std::function<bool(Vec3i feetCell)>;

// Busca de custo uniforme limitada (nodeCap expansoes OU prazo). Passos: andar, subir 1, descer 1 (ou 2).
PathResult findPath(IWorld& w, Vec3i start, const GoalFn& goal, int nodeCap, int maxFall,
                    std::chrono::steady_clock::time_point deadline);

}  // namespace am
