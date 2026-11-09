// Maquina de estados da mineracao automatica.
//
//   Planning -> (Navigating -> Planning)* -> Aiming -> Breaking -> Planning ... -> Verifying -> Done
//                      \-> Recovering -> Planning          (qualquer estado -> Aborted)
//
// Chamado uma vez por tick do jogo (thread do jogo). Nunca bloqueia: cada tick respeita
// Settings::tickBudgetUs e continua no tick seguinte.
#pragma once

#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

#include "Planner.hpp"
#include "Settings.hpp"

namespace am {

class Executor {
public:
    // `ordered` = todos os blocos da selecao, ja na ordem de quebra.
    void begin(std::vector<Vec3i> ordered);
    void stop(IGameBridge* bridge, const std::string& message, bool aborted);
    void tick(IGameBridge& bridge, const Settings& s);

    bool active() const;
    RunState state() const { return state_; }
    int done() const { return static_cast<int>(done_.size()); }
    int skippedCount() const { return static_cast<int>(skipped_.size()); }
    int total() const { return static_cast<int>(cells_.size()); }
    const std::string& message() const { return message_; }
    bool currentTarget(Vec3i& t) const {
        t = target_;
        return hasTarget_;
    }
    // proximos `n` blocos pendentes (para o outline) e blocos ignorados
    std::vector<Vec3i> upcoming(std::size_t n) const;
    std::vector<Vec3i> skippedCells(std::size_t n) const;
    const std::vector<Vec3i>& allCells() const { return cells_; }

private:
    using Clock = std::chrono::steady_clock;

    void planStep(IGameBridge& b, const Pose& p, const Settings& s, Clock::time_point deadline);
    void navigate(IGameBridge& b, const Pose& p, const Settings& s);
    void aim(IGameBridge& b, const Pose& p, const Settings& s);
    void breaking(IGameBridge& b, const Pose& p, const Settings& s, Clock::time_point deadline);
    void recovering(IGameBridge& b, const Pose& p);
    void verifying(IGameBridge& b, Clock::time_point deadline);

    bool nextTarget(Vec3i& t);
    bool planPathTo(const Pose& p, Vec3i target, float reach, const Settings& s, Clock::time_point deadline);
    void markDone(Vec3i t);
    void skip(Vec3i t, const char* why);
    void defer(Vec3i t);
    void recover(IGameBridge& b, const char* why);
    void finish(IGameBridge& b, const std::string& msg, bool aborted);
    void releaseInputs(IGameBridge& b);
    void setTarget(Vec3i t, RunState next);

    RunState state_{RunState::Idle};
    WorldCache world_;
    std::string message_;

    std::vector<Vec3i> cells_;  // selecao completa
    std::vector<Vec3i> order_;  // ordem da passada atual
    std::size_t cursor_{0};
    std::vector<Vec3i> priority_;  // bloqueadores que precisam sair antes
    std::vector<Vec3i> deferred_;  // inacessiveis nesta passada
    std::unordered_set<Vec3i, Vec3iHash> inSel_, done_, skipped_, deferredSet_;
    std::vector<Vec3i> skippedList_;
    bool progressSincePass_{false};
    int verifyPass_{0};
    std::size_t verifyCursor_{0};
    std::vector<Vec3i> verifyFound_;

    Vec3i target_{};
    bool hasTarget_{false};
    std::vector<Vec3i> path_;
    std::size_t pathIdx_{0};

    long long tickCount_{0};
    int navTicks_{0}, aimTicks_{0}, breakTicks_{0}, failures_{0}, replans_{0}, unknownWait_{0}, nearMiss_{0};
    int nudge_{0}, nudgeDir_{1};
    Vec3f stuckPos_{};
    long long stuckMark_{0};
};

}  // namespace am
