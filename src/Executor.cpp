#include "Executor.hpp"

#include <algorithm>
#include <cmath>

namespace am {
namespace {
// face de bater: a mais proxima do olho (0..5 = baixo, cima, norte, sul, oeste, leste)
int faceToward(const Vec3f& eye, Vec3i t) {
    const Vec3f d = eye - centerOf(t);
    const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    if (ay >= ax && ay >= az) return d.y > 0 ? 1 : 0;
    if (ax >= az) return d.x > 0 ? 5 : 4;
    return d.z > 0 ? 3 : 2;
}

float horizDist(const Vec3f& a, const Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }

Vec3i feetCell(const Pose& p) { return {static_cast<int>(std::floor(p.feet.x)), static_cast<int>(std::floor(p.feet.y + 0.05f)),
                                        static_cast<int>(std::floor(p.feet.z))}; }
}  // namespace

// ---------------------------------------------------------------- controle
void Executor::begin(std::vector<Vec3i> ordered) {
    cells_ = ordered;
    order_ = std::move(ordered);
    cursor_ = 0;
    priority_.clear();
    deferred_.clear();
    inSel_.clear();
    done_.clear();
    skipped_.clear();
    deferredSet_.clear();
    skippedList_.clear();
    for (const Vec3i& c : cells_) inSel_.insert(c);
    progressSincePass_ = false;
    verifyPass_ = 0;
    verifyCursor_ = 0;
    verifyFound_.clear();
    hasTarget_ = false;
    path_.clear();
    pathIdx_ = 0;
    tickCount_ = 0;
    failures_ = replans_ = unknownWait_ = nudge_ = nearMiss_ = 0;
    stuckMark_ = 0;
    message_ = "INICIANDO";
    state_ = cells_.empty() ? RunState::Done : RunState::Planning;
}

bool Executor::active() const {
    return state_ != RunState::Idle && state_ != RunState::Done && state_ != RunState::Aborted;
}

void Executor::releaseInputs(IGameBridge& b) {
    b.setMove(0, 0, false, false);
    b.setBreakTarget(nullptr, 0);
}

void Executor::finish(IGameBridge& b, const std::string& msg, bool aborted) {
    releaseInputs(b);
    hasTarget_ = false;
    path_.clear();
    message_ = msg;
    state_ = aborted ? RunState::Aborted : RunState::Done;
}

void Executor::stop(IGameBridge* bridge, const std::string& message, bool aborted) {
    if (bridge) releaseInputs(*bridge);
    hasTarget_ = false;
    path_.clear();
    message_ = message;
    state_ = aborted ? RunState::Aborted : RunState::Idle;
}

std::vector<Vec3i> Executor::upcoming(std::size_t n) const {
    std::vector<Vec3i> out;
    auto pending = [&](const Vec3i& c) { return !done_.count(c) && !skipped_.count(c); };
    for (std::size_t i = cursor_; i < order_.size() && out.size() < n; ++i)
        if (pending(order_[i])) out.push_back(order_[i]);
    for (const Vec3i& c : deferred_)
        if (out.size() < n && pending(c)) out.push_back(c);
    return out;
}

std::vector<Vec3i> Executor::skippedCells(std::size_t n) const {
    return std::vector<Vec3i>(skippedList_.begin(), skippedList_.begin() + std::min(n, skippedList_.size()));
}

// ---------------------------------------------------------------- bookkeeping
void Executor::markDone(Vec3i t) {
    done_.insert(t);
    priority_.erase(std::remove(priority_.begin(), priority_.end(), t), priority_.end());
    progressSincePass_ = true;
}

void Executor::skip(Vec3i t, const char* why) {
    if (skipped_.insert(t).second) skippedList_.push_back(t);
    priority_.erase(std::remove(priority_.begin(), priority_.end(), t), priority_.end());
    message_ = std::string("IGNORADO: ") + why;
}

void Executor::defer(Vec3i t) {
    if (deferredSet_.insert(t).second) deferred_.push_back(t);
    priority_.erase(std::remove(priority_.begin(), priority_.end(), t), priority_.end());
}

bool Executor::nextTarget(Vec3i& t) {
    auto settled = [&](const Vec3i& c) { return done_.count(c) || skipped_.count(c) || deferredSet_.count(c); };
    while (!priority_.empty() && settled(priority_.back())) priority_.pop_back();
    if (!priority_.empty()) {
        t = priority_.back();
        return true;
    }
    while (cursor_ < order_.size() && settled(order_[cursor_])) ++cursor_;
    if (cursor_ >= order_.size()) return false;
    t = order_[cursor_];
    return true;
}

void Executor::setTarget(Vec3i t, RunState next) {
    target_ = t;
    hasTarget_ = true;
    aimTicks_ = breakTicks_ = navTicks_ = 0;
    state_ = next;
}

void Executor::recover(IGameBridge& b, const char* why) {
    b.setMove(0, 0, false, false);
    b.setBreakTarget(nullptr, 0);
    path_.clear();
    message_ = std::string("RECUPERANDO: ") + why;
    if (++failures_ >= 3) {
        if (hasTarget_) defer(target_);
        failures_ = 0;
        hasTarget_ = false;
        state_ = RunState::Planning;
        return;
    }
    nudge_ = 10;
    nudgeDir_ = (failures_ % 2) ? 1 : -1;
    state_ = RunState::Recovering;
}

// ---------------------------------------------------------------- tick
void Executor::tick(IGameBridge& b, const Settings& s) {
    if (!active()) return;
    Pose p;
    if (!b.ready() || !b.pose(p) || !p.valid) {
        finish(b, "ABORTADO: SEM DADOS DO JOGADOR", true);
        return;
    }
    ++tickCount_;
    world_.attach(&b);
    world_.clear();
    const auto deadline = Clock::now() + std::chrono::microseconds(s.tickBudgetUs);

    switch (state_) {
        case RunState::Planning: planStep(b, p, s, deadline); break;
        case RunState::Navigating: navigate(b, p, s); break;
        case RunState::Aiming: aim(b, p, s); break;
        case RunState::Breaking: breaking(b, p, s, deadline); break;
        case RunState::Recovering: recovering(b, p); break;
        case RunState::Verifying: verifying(b, deadline); break;
        default: break;
    }
}

// ---------------------------------------------------------------- Planning
bool Executor::planPathTo(const Pose& p, Vec3i target, float reach, const Settings& s, Clock::time_point deadline) {
    const Vec3i start = feetCell(p);
    const GoalFn goal = [&](Vec3i c) {
        const Vec3f eye{c.x + 0.5f, c.y + kEyeHeight, c.z + 0.5f};
        // margem: o jogador nunca para exatamente no centro da celula
        return inReach(eye, target, reach - 0.35f) && lineOfSight(world_, eye, target, nullptr);
    };
    PathResult r = findPath(world_, start, goal, s.planNodeCap, 2, deadline + std::chrono::microseconds(s.tickBudgetUs));
    if (!r.found) return false;
    path_ = std::move(r.cells);
    pathIdx_ = 0;
    stuckPos_ = p.feet;
    stuckMark_ = tickCount_;
    return true;
}

void Executor::planStep(IGameBridge& b, const Pose& p, const Settings& s, Clock::time_point deadline) {
    const Vec3f eye = p.eye();
    const float reach = std::min(s.reach, 5.0f) - 0.25f;

    for (int guard = 0; guard < 1024; ++guard) {
        if (Clock::now() > deadline) return;  // continua no proximo tick

        Vec3i t{};
        if (!nextTarget(t)) {
            // fim da passada
            if (!deferred_.empty()) {
                if (progressSincePass_) {  // houve progresso: tenta de novo os adiados
                    order_ = deferred_;
                    deferred_.clear();
                    deferredSet_.clear();
                    cursor_ = 0;
                    progressSincePass_ = false;
                    continue;
                }
                for (const Vec3i& c : deferred_) skip(c, "INACESSIVEL");
                deferred_.clear();
                deferredSet_.clear();
            }
            if (verifyPass_ < 2) {  // confere se algo caiu/voltou (areia, cascalho)
                ++verifyPass_;
                verifyCursor_ = 0;
                verifyFound_.clear();
                state_ = RunState::Verifying;
                return;
            }
            finish(b, "CONCLUIDO: " + std::to_string(done()) + " OK, " + std::to_string(skippedCount()) + " IGNORADOS", false);
            return;
        }

        const BlockClass cls = world_.blockAt(t);
        if (cls == BlockClass::Air) { markDone(t); continue; }
        if (cls == BlockClass::Unknown) {
            if (++unknownWait_ > 60) { skip(t, "CHUNK NAO CARREGADO"); unknownWait_ = 0; continue; }
            message_ = "AGUARDANDO CHUNK";
            return;
        }
        unknownWait_ = 0;
        if (cls == BlockClass::Liquid) { skip(t, "LIQUIDO"); continue; }
        if (cls == BlockClass::Unbreakable) { skip(t, "INQUEBRAVEL"); continue; }
        if (cls == BlockClass::Hazard) { skip(t, "PERIGOSO"); continue; }
        if (touchesHazard(world_, t, s.avoidLiquids)) { skip(t, "LAVA/AGUA ADJACENTE"); continue; }

        Vec3i blocker{};
        if (inReach(eye, t, reach)) {
            if (lineOfSight(world_, eye, t, &blocker)) {
                nearMiss_ = 0;
                setTarget(t, RunState::Aiming);
                message_ = "MIRANDANDO";
                return;
            }
            // um bloco da propria selecao esta na frente: quebra ele primeiro (nunca fora da selecao)
            // (se o bloqueador ja foi adiado/ignorado ele nao e alcancavel agora: nao entra em ciclo)
            if (blocker != t && inSel_.count(blocker) && !done_.count(blocker) && !skipped_.count(blocker) &&
                !deferredSet_.count(blocker) && world_.blockAt(blocker) == BlockClass::Solid &&
                std::find(priority_.begin(), priority_.end(), blocker) == priority_.end()) {
                priority_.push_back(blocker);
                continue;
            }
        }
        if (planPathTo(p, t, reach, s, deadline)) {
            // caminho de 1 celula = ja estamos na celula certa mas a mira/alcance real falhou por
            // causa do deslocamento dentro dela; tenta poucas vezes e adia para nao ficar em ciclo
            if (path_.size() <= 1 && ++nearMiss_ > 2) {
                nearMiss_ = 0;
                path_.clear();
                defer(t);
                continue;
            }
            if (path_.size() > 1) nearMiss_ = 0;
            replans_ = 0;
            setTarget(t, RunState::Navigating);
            message_ = "ANDANDO";
            return;
        }
        defer(t);
    }
}

// ---------------------------------------------------------------- Navigating
void Executor::navigate(IGameBridge& b, const Pose& p, const Settings& s) {
    ++navTicks_;
    auto reached = [&](Vec3i wp) {
        return horizDist(p.feet, {wp.x + 0.5f, 0, wp.z + 0.5f}) < 0.25f && std::fabs(p.feet.y - static_cast<float>(wp.y)) < 0.8f;
    };
    while (pathIdx_ < path_.size() && reached(path_[pathIdx_])) ++pathIdx_;
    if (pathIdx_ >= path_.size()) {
        b.setMove(0, 0, false, false);
        path_.clear();
        state_ = RunState::Planning;  // reavalia alcance / visada na proxima rodada
        return;
    }
    if (navTicks_ > s.navTimeoutTicks) { recover(b, "TIMEOUT"); return; }

    // caminho continua valido? (algo pode ter mudado: areia caiu, bloco colocado...)
    if (navTicks_ % 4 == 0) {
        for (std::size_t k = 0; k < 2 && pathIdx_ + k < path_.size(); ++k) {
            if (!standable(world_, path_[pathIdx_ + k])) {
                path_.clear();
                b.setMove(0, 0, false, false);
                if (++replans_ > 6) recover(b, "CAMINHO BLOQUEADO");
                else state_ = RunState::Planning;
                return;
            }
        }
    }

    const Vec3i wp = path_[pathIdx_];
    const float dx = wp.x + 0.5f - p.feet.x, dz = wp.z + 0.5f - p.feet.z;
    const float wantYaw = yawForDirection(dx, dz);
    const float yaw = approachAngle(p.yaw, wantYaw, s.turnRate);
    b.setLook(yaw, approachAngle(p.pitch, 10.0f, s.turnRate));
    const float err = std::fabs(angleDelta(yaw, wantYaw));
    const float forward = err < 40.0f ? 1.0f : 0.0f;
    const bool needJump = p.onGround && static_cast<float>(wp.y) > p.feet.y + 0.3f;
    b.setMove(forward, 0, needJump, false);

    // travado? (nao saiu do lugar apesar de andar)
    if (tickCount_ - stuckMark_ >= s.stuckTicks) {
        const float moved = horizDist(p.feet, stuckPos_) + std::fabs(p.feet.y - stuckPos_.y);
        stuckPos_ = p.feet;
        stuckMark_ = tickCount_;
        if (moved < 0.12f && forward > 0.0f) recover(b, "TRAVADO");
    }
}

// ---------------------------------------------------------------- Aiming
void Executor::aim(IGameBridge& b, const Pose& p, const Settings& s) {
    b.setMove(0, 0, false, false);
    const float reach = std::min(s.reach, 5.0f) - 0.25f;
    const BlockClass cls = world_.blockAt(target_);
    if (cls == BlockClass::Air) { markDone(target_); hasTarget_ = false; state_ = RunState::Planning; return; }
    if (!inReach(p.eye(), target_, reach)) { state_ = RunState::Planning; return; }
    if (++aimTicks_ > 80) { recover(b, "MIRA"); return; }

    float wantYaw, wantPitch;
    lookAngles(p.eye(), centerOf(target_), wantYaw, wantPitch);
    wantPitch = std::clamp(wantPitch, -89.0f, 89.0f);
    const float yaw = approachAngle(p.yaw, wantYaw, s.turnRate);
    const float pitch = approachAngle(p.pitch, wantPitch, s.turnRate);
    b.setLook(yaw, pitch);
    if (std::fabs(angleDelta(yaw, wantYaw)) < 1.5f && std::fabs(pitch - wantPitch) < 1.5f) {
        state_ = RunState::Breaking;
        breakTicks_ = 0;
        message_ = "QUEBRANDO";
    }
}

// ---------------------------------------------------------------- Breaking
void Executor::breaking(IGameBridge& b, const Pose& p, const Settings& s, Clock::time_point deadline) {
    const float reach = std::min(s.reach, 5.0f) - 0.1f;
    const BlockClass cls = world_.blockAt(target_);
    if (cls != BlockClass::Solid) {
        b.setBreakTarget(nullptr, 0);
        if (cls == BlockClass::Air) {
            markDone(target_);
            failures_ = 0;
        } else {
            skip(target_, "MUDOU DURANTE A QUEBRA");
        }
        hasTarget_ = false;
        state_ = RunState::Planning;
        planStep(b, p, s, deadline);  // sem perder um tick entre blocos
        return;
    }
    if (!inReach(p.eye(), target_, reach)) {  // empurrado / lag
        b.setBreakTarget(nullptr, 0);
        state_ = RunState::Planning;
        return;
    }
    // mantem a mira (pequenas correcoes; se desviou muito, reaponta)
    float wantYaw, wantPitch;
    lookAngles(p.eye(), centerOf(target_), wantYaw, wantPitch);
    wantPitch = std::clamp(wantPitch, -89.0f, 89.0f);
    if (std::fabs(angleDelta(p.yaw, wantYaw)) > 8.0f || std::fabs(p.pitch - wantPitch) > 8.0f) {
        b.setBreakTarget(nullptr, 0);
        state_ = RunState::Aiming;
        return;
    }
    b.setLook(wantYaw, wantPitch);
    b.setMove(0, 0, false, false);
    b.setBreakTarget(&target_, faceToward(p.eye(), target_));
    if (++breakTicks_ > s.breakTimeoutTicks) recover(b, "DEMOROU DEMAIS");
}

// ---------------------------------------------------------------- Recovering
void Executor::recovering(IGameBridge& b, const Pose& p) {
    if (nudge_-- > 0) {
        b.setMove(0, static_cast<float>(nudgeDir_), p.onGround, false);  // pulinho lateral para soltar
        return;
    }
    b.setMove(0, 0, false, false);
    state_ = RunState::Planning;
}

// ---------------------------------------------------------------- Verifying
void Executor::verifying(IGameBridge& b, Clock::time_point deadline) {
    // reescaneia toda a selecao (em fatias de tempo) atras de blocos que reapareceram
    while (verifyCursor_ < cells_.size()) {
        if (Clock::now() > deadline) return;
        const Vec3i c = cells_[verifyCursor_++];
        if (skipped_.count(c)) continue;
        if (world_.blockAt(c) == BlockClass::Solid) {
            done_.erase(c);
            verifyFound_.push_back(c);
        }
    }
    if (verifyFound_.empty()) {
        finish(b, "CONCLUIDO: " + std::to_string(done()) + " OK, " + std::to_string(skippedCount()) + " IGNORADOS", false);
        return;
    }
    order_ = verifyFound_;
    cursor_ = 0;
    deferred_.clear();
    deferredSet_.clear();
    progressSincePass_ = false;
    state_ = RunState::Planning;
}

}  // namespace am
