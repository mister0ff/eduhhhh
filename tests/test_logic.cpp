// Testes de logica no host (sem Android, sem jogo): g++ -std=c++20 tests/test_logic.cpp src/{Templates,Planner,Executor,Settings,Render,Controller}.cpp -Iinclude
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "Controller.hpp"
#include "Executor.hpp"
#include "Planner.hpp"
#include "Render.hpp"
#include "Settings.hpp"
#include "Templates.hpp"

using namespace am;

static int gFail = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("  FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            ++gFail;                                                                   \
        }                                                                              \
    } while (0)

// ---------------------------------------------------------------- mundo simulado
struct MockBridge : IGameBridge {
    std::unordered_map<Vec3i, BlockClass, Vec3iHash> blocks;  // ausente = Air
    Vec3f pos{0.5f, 64, 0.5f};
    float yaw{270}, pitch{0}, vy{0};
    bool onGround{true};
    bool poseOk{true};
    float mvF{0}, mvS{0};
    bool mvJump{false};
    bool breaking{false};
    Vec3i brk{};
    int brkProgress{0};
    int breakTicksNeeded{6};
    int frozenTicks{0};  // simula "preso": entrada de andar e ignorada
    long long tick{0};
    int outsideBreaks{0};
    std::unordered_map<Vec3i, bool, Vec3iHash> allowed;  // selecao (para detectar quebra fora dela)

    bool ready() const override { return poseOk; }
    std::string missing() const override { return {}; }
    BlockClass blockAt(Vec3i c) override {
        auto it = blocks.find(c);
        return it == blocks.end() ? BlockClass::Air : it->second;
    }
    bool pose(Pose& o) override {
        if (!poseOk) return false;
        o.feet = pos; o.yaw = yaw; o.pitch = pitch; o.onGround = onGround; o.valid = true;
        return true;
    }
    bool lookedAtBlock(Vec3i&) override { return false; }
    void setMove(float f, float s, bool j, bool) override { mvF = f; mvS = s; mvJump = j; }
    void setLook(float y, float p) override { yaw = y; pitch = p; }
    void setBreakTarget(const Vec3i* t, int) override {
        if (!t) { breaking = false; brkProgress = 0; return; }
        if (!breaking || !(brk == *t)) brkProgress = 0;
        breaking = true;
        brk = *t;
    }

    void fillSolid(Vec3i lo, Vec3i hi) {
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) blocks[{x, y, z}] = BlockClass::Solid;
    }
    void carve(Vec3i lo, Vec3i hi) {
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) blocks.erase({x, y, z});
    }
    int countSolidIn(Vec3i lo, Vec3i hi) {
        int n = 0;
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) n += blockAt({x, y, z}) == BlockClass::Solid;
        return n;
    }

    // 1 tick de fisica (20 Hz)
    void step() {
        ++tick;
        const bool grounded = isSupport(blockAt({(int)std::floor(pos.x), (int)std::floor(pos.y - 1e-3f), (int)std::floor(pos.z)}));
        onGround = grounded && vy <= 0.0f;
        const float spd = 0.215f;
        if (frozenTicks > 0) { --frozenTicks; mvF = mvS = 0; }
        const Vec3f fwd = lookDir(yaw, 0), right = cross(fwd, {0, 1, 0});
        float nx = pos.x + (fwd.x * mvF + right.x * mvS) * spd, nz = pos.z + (fwd.z * mvF + right.z * mvS) * spd;
        auto free = [&](float x, float y, float z) {
            const int cx = (int)std::floor(x), cz = (int)std::floor(z), cy = (int)std::floor(y + 0.01f);
            return blockAt({cx, cy, cz}) == BlockClass::Air && blockAt({cx, cy + 1, cz}) == BlockClass::Air;
        };
        if (mvF != 0 || mvS != 0) {
            if (free(nx, pos.y, nz)) { pos.x = nx; pos.z = nz; }
            else if (mvJump && onGround && free(nx, pos.y + 1.0f, nz)) { pos.x = nx; pos.z = nz; pos.y += 1.0f; }
        }
        if (mvJump && onGround) vy = 0.42f;
        pos.y += vy;
        vy = (vy - 0.08f) * 0.98f;
        const int cx = (int)std::floor(pos.x), cz = (int)std::floor(pos.z);
        if (vy <= 0 && isSupport(blockAt({cx, (int)std::floor(pos.y - 1e-3f), cz}))) {
            const float floorY = std::floor(pos.y - 1e-3f) + 1.0f;
            if (pos.y < floorY) { pos.y = floorY; vy = 0; }
        }
        if (vy > 0 && blockAt({cx, (int)std::floor(pos.y + 1.8f), cz}) != BlockClass::Air) vy = 0;

        if (breaking) {
            const Vec3f eye{pos.x, pos.y + kEyeHeight, pos.z};
            const Vec3f d = centerOf(brk) - eye;
            const float dist = length(d);
            const Vec3f look = lookDir(yaw, pitch);
            const float cosA = dot(look, d * (1.0f / dist));
            if (dist <= 5.0f && cosA > std::cos(12.0f * kDeg2Rad) && blockAt(brk) == BlockClass::Solid) {
                if (++brkProgress >= breakTicksNeeded) {
                    if (!allowed.empty() && !allowed.count(brk)) ++outsideBreaks;
                    blocks.erase(brk);
                    brkProgress = 0;
                }
            }
        }
    }
};

struct Run {
    int ticks{0};
    RunState end{RunState::Idle};
};

static Run runExec(MockBridge& w, Executor& ex, const Settings& s, int maxTicks) {
    Run r;
    for (; r.ticks < maxTicks && ex.active(); ++r.ticks) {
        ex.tick(w, s);
        w.step();
    }
    r.end = ex.state();
    return r;
}

static Settings defaultSettings() {
    Settings s;
    sanitize(s);
    return s;
}

// cavidade 1x2 em (0,64..65,0); piso em y=63; massa de pedra em volta
static void stoneWorld(MockBridge& w) {
    w.fillSolid({-3, 50, -12}, {40, 80, 12});
    w.carve({0, 64, 0}, {0, 65, 0});
}

static void selectionOf(MockBridge& w, const std::vector<Vec3i>& cells) {
    for (const Vec3i& c : cells) w.allowed[c] = true;
}

// ---------------------------------------------------------------- testes
static void testMath() {
    std::puts("math/orientacao");
    CHECK(facingFromYaw(0) == Facing::South);
    CHECK(facingFromYaw(90) == Facing::West);
    CHECK(facingFromYaw(180) == Facing::North);
    CHECK(facingFromYaw(270) == Facing::East);
    CHECK(facingFromYaw(-90) == Facing::East);
    CHECK(rightOf(Facing::North) == (Vec3i{1, 0, 0}));
    CHECK(rightOf(Facing::East) == (Vec3i{0, 0, 1}));
    CHECK(std::fabs(angleDelta(350, 10) - 20) < 1e-3f);
    CHECK(std::fabs(angleDelta(10, 350) + 20) < 1e-3f);
    float y, p;
    lookAngles({0, 0, 0}, {-5, 0, 0}, y, p);  // alvo a oeste => yaw 90
    CHECK(std::fabs(y - 90) < 1e-3f);
    const Vec3f d = lookDir(y, 20);
    float y2, p2;
    lookAngles({0, 0, 0}, d, y2, p2);
    CHECK(std::fabs(angleDelta(y, y2)) < 1e-3f && std::fabs(p2 - 20) < 1e-3f);
}

static void testTemplates() {
    std::puts("templates");
    TemplateParams t;
    t.origin = {0, 64, 0};
    t.facing = Facing::East;
    t.length = 10;
    t.kind = TemplateKind::Box2x2;
    CHECK(buildCells(t).size() == 40);
    t.kind = TemplateKind::Box3x3;
    auto c33 = buildCells(t);
    CHECK(c33.size() == 90);
    for (auto& c : c33) CHECK(c.x >= 1 && c.x <= 10 && c.z >= -1 && c.z <= 1 && c.y >= 64 && c.y <= 66);
    t.kind = TemplateKind::Tunnel;
    t.width = 1; t.height = 2;
    CHECK(buildCells(t).size() == 20);
    t.width = 3; t.height = 3;
    CHECK(buildCells(t).size() == 90);
    t.kind = TemplateKind::Stairs;
    t.width = 1;
    auto st = buildCells(t);
    CHECK(st.size() == 30);
    bool has = false;
    for (auto& c : st) has |= (c == Vec3i{10, 54, 0});  // degrau 10 desce 10
    CHECK(has);
    t.stairsDown = false;
    st = buildCells(t);
    CHECK(st.size() == 31);  // 30 + 1 de folga acima da cabeca no ponto de partida
    has = false;
    for (auto& c : st) has |= (c == Vec3i{10, 74, 0});
    CHECK(has);
    t.kind = TemplateKind::Custom;
    t.pos1 = {0, 0, 0}; t.pos2 = {3, 3, 3};
    CHECK(buildCells(t).size() == 64);
    t.pos2 = {200, 200, 200};
    std::string err;
    CHECK(buildCells(t, &err).empty() && !err.empty());

    // ordem: frente -> tras, topo -> base dentro da fatia
    t.kind = TemplateKind::Box3x3; t.length = 3;
    auto ord = orderCells(buildCells(t), t, t.origin);
    bool mono = true;
    for (size_t i = 1; i < ord.size(); ++i) mono &= ord[i].x >= ord[i - 1].x;
    CHECK(mono);
    CHECK(ord.front().y == 66);
}

static void testProjection() {
    std::puts("projecao/outline/hud");
    Camera cam;
    cam.eye = {0, 0, 0}; cam.yaw = 0; cam.pitch = 0; cam.width = 1920; cam.height = 1080;
    float x, y;
    CHECK(project(cam, {0, 0, 10}, x, y));  // reto a frente => centro
    CHECK(std::fabs(x - 960) < 0.5f && std::fabs(y - 540) < 0.5f);
    CHECK(!project(cam, {0, 0, -5}, x, y));  // atras
    CHECK(project(cam, {-1, 0, 5}, x, y) && x > 960);  // oeste esta a DIREITA de quem olha +Z
    CHECK(project(cam, {0, 1, 5}, x, y) && y < 540);   // acima => y menor
    // olhando para baixo, ponto no chao a frente fica abaixo do centro
    cam.pitch = 45;
    CHECK(project(cam, {0, -3, 3}, x, y) && std::fabs(y - 540) < 1.0f);

    Settings s = defaultSettings();
    OverlayData d;
    d.preview = true; d.hasBounds = true; d.bounds = {{1, 64, -1}, {5, 66, 1}};
    Controller* none = nullptr; (void)none;
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 5; t.kind = TemplateKind::Box3x3;
    d.cells = buildCells(t);
    cam.pitch = 0; cam.yaw = 270; cam.eye = {0.5f, 65.62f, 0.5f};
    Batch b;
    buildOutline(b, cam, d, s);
    CHECK(!b.v.empty() && b.v.size() % 3 == 0);
    s.showOutline = false;
    Batch b2;
    buildOutline(b2, cam, d, s);
    CHECK(b2.v.empty());
    Status st; st.menuOpen = true; st.templateIdx = 2; st.message = "TESTE";
    Batch h;
    buildHud(h, 1920, 1080, st, defaultSettings());
    CHECK(!h.v.empty());
}

static void testSettings() {
    std::puts("settings");
    Settings s;
    s.length = 33; s.keyStart = "G"; s.reach = 4.0f; s.templateIdx = 3;
    Settings r;
    parseInto(r, serialize(s));
    CHECK(r.length == 33 && r.keyStart == "G" && std::fabs(r.reach - 4.0f) < 1e-4f && r.templateIdx == 3);
    Settings bad;
    parseInto(bad, "length=999999\nreach=abc\ntemplate=-4\n#x\nlixo");
    CHECK(bad.length == kMaxLength && bad.templateIdx == 0 && std::fabs(bad.reach - 4.5f) < 1e-4f);
}

static void testBox3x3() {
    std::puts("exec: 3x3 reto, 12 blocos");
    MockBridge w; stoneWorld(w);
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 12; t.kind = TemplateKind::Box3x3;
    auto cells = orderCells(buildCells(t), t, t.origin);
    selectionOf(w, cells);
    const int before = w.countSolidIn({-3, 50, -12}, {40, 80, 12});
    Executor ex; ex.begin(cells);
    Run r = runExec(w, ex, defaultSettings(), 6000);
    std::printf("  %d ticks, estado=%d, done=%d skipped=%d, pos=(%.1f,%.1f,%.1f)\n", r.ticks, (int)r.end, ex.done(), ex.skippedCount(), w.pos.x, w.pos.y, w.pos.z);
    CHECK(r.end == RunState::Done);
    CHECK(ex.done() == (int)cells.size() && ex.skippedCount() == 0);
    CHECK(w.countSolidIn({1, 64, -1}, {12, 66, 1}) == 0);
    CHECK(before - w.countSolidIn({-3, 50, -12}, {40, 80, 12}) == (int)cells.size());  // nada fora da selecao
    CHECK(w.outsideBreaks == 0);
    CHECK(w.pos.x > 8.0f);  // andou pelo tunel
}

static void testObstacles() {
    std::puts("exec: bedrock, lava e vizinho de lava sao ignorados (sem travar)");
    MockBridge w; stoneWorld(w);
    w.blocks[{4, 65, 0}] = BlockClass::Unbreakable;   // dentro da selecao
    w.blocks[{6, 64, 1}] = BlockClass::Hazard;        // lava dentro da selecao
    w.blocks[{9, 67, 0}] = BlockClass::Hazard;        // lava fora, em cima de (9,66,0)
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 12; t.kind = TemplateKind::Box3x3;
    auto cells = orderCells(buildCells(t), t, t.origin);
    selectionOf(w, cells);
    Executor ex; ex.begin(cells);
    Run r = runExec(w, ex, defaultSettings(), 8000);
    std::printf("  %d ticks, estado=%d, done=%d skipped=%d | %s\n", r.ticks, (int)r.end, ex.done(), ex.skippedCount(), ex.message().c_str());
    CHECK(r.end == RunState::Done);
    CHECK(w.blockAt({4, 65, 0}) == BlockClass::Unbreakable);
    CHECK(w.blockAt({6, 64, 1}) == BlockClass::Hazard);
    CHECK(w.blockAt({9, 66, 0}) == BlockClass::Solid);  // protegido por estar colado na lava
    CHECK(ex.skippedCount() >= 3);
    CHECK(w.outsideBreaks == 0);
    CHECK(ex.done() + ex.skippedCount() == (int)cells.size());
}

static void testStairs() {
    for (bool down : {true, false}) {
        std::printf("exec: escada %s\n", down ? "descendo" : "subindo");
        MockBridge w; stoneWorld(w);
        TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 10; t.kind = TemplateKind::Stairs;
        t.stairsDown = down;
        auto cells = orderCells(buildCells(t), t, t.origin);
        selectionOf(w, cells);
        Executor ex; ex.begin(cells);
        Run r = runExec(w, ex, defaultSettings(), 10000);
        std::printf("  %d ticks, estado=%d, done=%d skipped=%d, pos=(%.1f,%.1f,%.1f) | %s\n", r.ticks, (int)r.end, ex.done(), ex.skippedCount(), w.pos.x, w.pos.y, w.pos.z, ex.message().c_str());
        CHECK(r.end == RunState::Done);
        CHECK(ex.skippedCount() == 0);
        CHECK(w.outsideBreaks == 0);
        // chegou ao ultimo degrau (x ~ 9..10) e na altura certa
        CHECK(w.pos.x > 8.0f);
        CHECK(down ? w.pos.y < 56.5f : w.pos.y > 71.5f);
    }
}

static void testCustom() {
    std::puts("exec: area Pos1-Pos2 (camada por camada)");
    MockBridge w; stoneWorld(w);
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.kind = TemplateKind::Custom;
    t.pos1 = {1, 64, -1}; t.pos2 = {5, 66, 1};
    auto cells = orderCells(buildCells(t), t, t.origin);
    CHECK(cells.size() == 5 * 3 * 3);
    CHECK(cells.front().y == 66);
    selectionOf(w, cells);
    Executor ex; ex.begin(cells);
    Run r = runExec(w, ex, defaultSettings(), 6000);
    std::printf("  %d ticks, estado=%d, done=%d skipped=%d\n", r.ticks, (int)r.end, ex.done(), ex.skippedCount());
    CHECK(r.end == RunState::Done);
    CHECK(w.countSolidIn({1, 64, -1}, {5, 66, 1}) == 0);
    CHECK(w.outsideBreaks == 0);
}

static void testUnreachable() {
    std::puts("exec: area longe e fechada (so pode sair da selecao) => ignora e termina");
    MockBridge w; stoneWorld(w);
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.kind = TemplateKind::Custom;
    t.pos1 = {20, 64, 0}; t.pos2 = {21, 65, 0};  // pedra no meio, fora da selecao
    auto cells = orderCells(buildCells(t), t, t.origin);
    selectionOf(w, cells);
    Executor ex; ex.begin(cells);
    Run r = runExec(w, ex, defaultSettings(), 4000);
    std::printf("  %d ticks, estado=%d, skipped=%d | %s\n", r.ticks, (int)r.end, ex.skippedCount(), ex.message().c_str());
    CHECK(r.end == RunState::Done);
    CHECK(ex.skippedCount() == (int)cells.size());
    CHECK(w.outsideBreaks == 0);
    CHECK(w.countSolidIn({20, 64, 0}, {21, 65, 0}) == 4);
}

static void testStuckRecovery() {
    std::puts("exec: jogador preso por 3 s => recupera e conclui");
    MockBridge w; stoneWorld(w);
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 6; t.kind = TemplateKind::Box3x3;
    auto cells = orderCells(buildCells(t), t, t.origin);
    selectionOf(w, cells);
    Executor ex; ex.begin(cells);
    Settings s = defaultSettings();
    int ticks = 0;
    for (; ticks < 8000 && ex.active(); ++ticks) {
        if (ticks == 120) w.frozenTicks = 60;  // trava no meio do trabalho
        ex.tick(w, s);
        w.step();
    }
    std::printf("  %d ticks, estado=%d, done=%d skipped=%d\n", ticks, (int)ex.state(), ex.done(), ex.skippedCount());
    CHECK(ex.state() == RunState::Done);
    CHECK(ex.done() + ex.skippedCount() == (int)cells.size());
    CHECK(w.outsideBreaks == 0);
}

static void testAbortAndStop() {
    std::puts("exec: perda de dados do jogador aborta e solta entradas");
    MockBridge w; stoneWorld(w);
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 6; t.kind = TemplateKind::Box3x3;
    auto cells = orderCells(buildCells(t), t, t.origin);
    Executor ex; ex.begin(cells);
    Settings s = defaultSettings();
    for (int i = 0; i < 30; ++i) { ex.tick(w, s); w.step(); }
    w.poseOk = false;
    ex.tick(w, s);
    CHECK(ex.state() == RunState::Aborted);
    CHECK(!w.breaking && w.mvF == 0);

    MockBridge w2; stoneWorld(w2);
    Executor e2; e2.begin(cells);
    for (int i = 0; i < 30; ++i) { e2.tick(w2, s); w2.step(); }
    e2.stop(&w2, "PARADO PELO USUARIO", true);
    CHECK(e2.state() == RunState::Aborted && !w2.breaking && w2.mvF == 0);
}

static void testPerf() {
    std::puts("perf: custo por tick (3x3 x 64) no mock");
    MockBridge w; w.fillSolid({-3, 50, -12}, {80, 80, 12}); w.carve({0, 64, 0}, {0, 65, 0});
    TemplateParams t; t.origin = {0, 64, 0}; t.facing = Facing::East; t.length = 64; t.kind = TemplateKind::Box3x3;
    auto cells = orderCells(buildCells(t), t, t.origin);
    Executor ex; ex.begin(cells);
    Settings s = defaultSettings();
    double worst = 0, total = 0; int n = 0;
    for (int i = 0; i < 40000 && ex.active(); ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        ex.tick(w, s);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        worst = std::max(worst, us); total += us; ++n;
        w.step();
    }
    std::printf("  %d ticks, media %.1f us, pior %.1f us, estado=%d, done=%d/%zu\n", n, total / n, worst, (int)ex.state(), ex.done(), cells.size());
    CHECK(ex.state() == RunState::Done);
    CHECK(worst < 20000.0);  // < 20 ms (host; no celular pode ser ~3-5x)
}

static void testController() {
    std::puts("controller: comandos -> preview -> start -> conclui");
    MockBridge w; stoneWorld(w);
    SettingsStore store; StatusBoard status; CommandQueue q;
    store.update([](Settings& s) { s.templateIdx = 1; s.length = 6; });
    Controller c(store, status, q);
    q.push(Command::ToggleMenu);
    c.onTick(w);
    auto ov = c.overlay();
    CHECK(ov && ov->preview && ov->hasBounds);
    CHECK(status.get().menuOpen && status.get().cells == 6 * 9);
    q.push(Command::NextTemplate);  // 3x3 -> Stairs
    c.onTick(w);
    CHECK(status.get().templateIdx == 2);
    q.push(Command::NextTemplate); q.push(Command::NextTemplate);  // -> Custom
    c.onTick(w);
    CHECK(status.get().templateIdx == 4 && status.get().message.find("POS1") != std::string::npos);
    q.push(Command::Start);  // sem pos => erro, nao inicia
    c.onTick(w);
    CHECK(!c.executor().active());
    q.push(Command::MarkPos1Feet);
    c.onTick(w);
    w.pos.x = 0.5f;
    CHECK(status.get().pos1Set);
    // volta para 3x3 e roda
    q.push(Command::NextTemplate);  // Custom -> 2x2
    q.push(Command::NextTemplate);  // -> 3x3
    c.onTick(w);
    CHECK(status.get().templateIdx == 1);
    q.push(Command::Start);
    c.onTick(w);
    CHECK(c.executor().active());
    for (int i = 0; i < 6000 && c.executor().active(); ++i) { c.onTick(w); w.step(); }
    CHECK(c.executor().state() == RunState::Done);
    CHECK(w.countSolidIn({1, 64, -1}, {6, 66, 1}) == 0);
    q.push(Command::Stop);
    c.onTick(w);
}

int main() {
    testMath();
    testTemplates();
    testProjection();
    testSettings();
    testBox3x3();
    testObstacles();
    testStairs();
    testCustom();
    testUnreachable();
    testStuckRecovery();
    testAbortAndStop();
    testPerf();
    testController();
    std::printf("\n%s (%d falhas)\n", gFail ? "FALHOU" : "OK", gFail);
    return gFail ? 1 : 0;
}
