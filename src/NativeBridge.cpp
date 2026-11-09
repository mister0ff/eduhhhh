#include "NativeBridge.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include "Runtime.hpp"

namespace am {
namespace {

using Clock = std::chrono::steady_clock;

// ----------------------------------------------------------------------------------------------
// Prototipos esperados para cada binding. Se a funcao da SUA versao tiver outra assinatura, ajuste
// aqui (e so aqui). Referencias C++ (T const&) sao ponteiros no ABI AArch64.
//
//   player_tick   : void  fn(void* localPlayer)                      -- hook; 1x por tick do jogo
//   move_input    : R     fn(void* inputState, a1..a5)               -- hook; depois do original
//                                                                       escrevemos forward/strafe/jump
//   get_region    : void* fn(void* actor)                            -- Actor::getDimensionBlockSource
//   get_block     : const void* fn(void* blockSource, const int pos[3]) -- BlockSource::getBlock(BlockPos const&)
//   block_is_air  : bool  fn(const void* block)                      -- Block::isAir()
//   block_is_liquid / block_is_unbreakable / block_is_hazard (opcionais)  : bool fn(const void* block)
//   start_destroy : bool  fn(void* gameMode, const int pos[3], uint8_t face, bool* out)
//   continue_destroy: bool fn(void* gameMode, const int pos[3], uint8_t face, bool* out)
//                     (se a sua versao tem um Vec3 de hit extra, adapte ContinueDestroyFn)
//   crosshair_block (opcional): bool fn(void* player, int outPos[3])
// ----------------------------------------------------------------------------------------------
using PlayerTickFn = void (*)(void*);
using MoveInputFn = std::uintptr_t (*)(void*, void*, void*, void*, void*, void*);
using GetRegionFn = void* (*)(void*);
using GetBlockFn = const void* (*)(void*, const int*);
using BlockPredFn = bool (*)(const void*);
using StartDestroyFn = bool (*)(void*, const int*, std::uint8_t, bool*);
using ContinueDestroyFn = bool (*)(void*, const int*, std::uint8_t, bool*);
using CrosshairFn = bool (*)(void*, int*);

struct Bindings {
    std::map<std::string, std::string> sig;     // sig.<nome> = padrao de bytes
    std::map<std::string, std::uintptr_t> off;  // off.<nome> = deslocamento (hex/dec)
    std::map<std::string, std::string> opt;     // opt.<nome> = valor
};

struct State {
    Bindings cfg;
    PlayerTickFn playerTickOrig{};
    MoveInputFn moveInputOrig{};
    void* playerTickTarget{};
    void* moveInputTarget{};
    GetRegionFn getRegion{};
    GetBlockFn getBlock{};
    BlockPredFn isAir{}, isLiquid{}, isUnbreakable{}, isHazard{};
    StartDestroyFn startDestroy{};
    ContinueDestroyFn continueDestroy{};
    CrosshairFn crosshair{};
    std::string missingText;

    std::atomic<void*> player{nullptr};
    std::atomic<long long> lastTickMs{0};
    void* region{};  // so thread do jogo
    // entrada virtual
    std::atomic<float> moveF{0}, moveS{0};
    std::atomic<bool> moveJump{false}, moveSneak{false};
    std::atomic<bool> moveActive{false};
    // quebra
    bool breaking{false};
    Vec3i breakCell{};
    int breakFace{0};
    bool breakStarted{false};
    bool breakDestroyedFlag{false};
};
State g;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

std::string trim(const std::string& v) {
    const auto b = v.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    return v.substr(b, v.find_last_not_of(" \t\r") - b + 1);
}

bool loadBindings(const std::string& path, Bindings& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k.empty() || v.empty()) continue;
        if (k.rfind("sig.", 0) == 0) out.sig[k.substr(4)] = v;
        else if (k.rfind("off.", 0) == 0) out.off[k.substr(4)] = static_cast<std::uintptr_t>(std::strtoull(v.c_str(), nullptr, 0));
        else if (k.rfind("opt.", 0) == 0) out.opt[k.substr(4)] = v;
    }
    return true;
}

void* resolve(const std::string& name) {
    const auto it = g.cfg.sig.find(name);
    if (it == g.cfg.sig.end()) return nullptr;
    const std::uintptr_t a = pl::memory::resolveSignature(it->second, "libminecraftpe.so");
    return reinterpret_cast<void*>(a);  // 0 = nao achou / ambiguo
}

bool hasOff(const char* n) { return g.cfg.off.count(n) != 0; }
std::uintptr_t off(const char* n) {
    const auto it = g.cfg.off.find(n);
    return it == g.cfg.off.end() ? 0 : it->second;
}

template <class T>
T rd(void* base, std::uintptr_t o) {
    T v;
    std::memcpy(&v, static_cast<char*>(base) + o, sizeof(T));
    return v;
}
template <class T>
void wr(void* base, std::uintptr_t o, T v) {
    std::memcpy(static_cast<char*>(base) + o, &v, sizeof(T));
}

// ---- detours -------------------------------------------------------------------------------
void playerTickDetour(void* self) {
    if (g.playerTickOrig) g.playerTickOrig(self);
    try {
        nativeBridge().onPlayerTick(self);
    } catch (...) {
    }
}

std::uintptr_t moveInputDetour(void* self, void* a1, void* a2, void* a3, void* a4, void* a5) {
    const std::uintptr_t r = g.moveInputOrig ? g.moveInputOrig(self, a1, a2, a3, a4, a5) : 0;
    try {
        nativeBridge().onMoveInput(self);
    } catch (...) {
    }
    return r;
}

}  // namespace

NativeBridge& nativeBridge() {
    static NativeBridge b;
    return b;
}

// ---------------------------------------------------------------------------------------------
bool NativeBridge::install(const std::string& bindingsPath) {
    if (installed_.load()) return true;
    g.cfg = {};
    g.missingText.clear();
    std::vector<std::string> missing;

    if (!loadBindings(bindingsPath, g.cfg)) {
        g.missingText = "arquivo autominer_bindings.cfg";
        return false;
    }

    // obrigatorios --------------------------------------------------------------------------
    void* tick = resolve("player_tick");
    if (!tick) missing.push_back("sig.player_tick");
    for (const char* o : {"pos_x", "yaw", "pitch"})
        if (!hasOff(o)) missing.push_back(std::string("off.") + o);
    g.getRegion = reinterpret_cast<GetRegionFn>(resolve("get_region"));
    if (!g.getRegion) missing.push_back("sig.get_region");
    g.getBlock = reinterpret_cast<GetBlockFn>(resolve("get_block"));
    if (!g.getBlock) missing.push_back("sig.get_block");
    g.isAir = reinterpret_cast<BlockPredFn>(resolve("block_is_air"));
    if (!g.isAir) missing.push_back("sig.block_is_air");
    // entrada / quebra: sem eles so da para fazer preview
    void* moveHook = resolve("move_input");
    if (!moveHook) missing.push_back("sig.move_input");
    for (const char* o : {"move_forward", "move_strafe", "move_jump"})
        if (!hasOff(o)) missing.push_back(std::string("off.") + o);
    g.startDestroy = reinterpret_cast<StartDestroyFn>(resolve("start_destroy"));
    if (!g.startDestroy) missing.push_back("sig.start_destroy");
    g.continueDestroy = reinterpret_cast<ContinueDestroyFn>(resolve("continue_destroy"));
    if (!g.continueDestroy) missing.push_back("sig.continue_destroy");
    if (!hasOff("gamemode_ptr")) missing.push_back("off.gamemode_ptr");

    // opcionais ------------------------------------------------------------------------------
    g.isLiquid = reinterpret_cast<BlockPredFn>(resolve("block_is_liquid"));
    g.isUnbreakable = reinterpret_cast<BlockPredFn>(resolve("block_is_unbreakable"));
    g.isHazard = reinterpret_cast<BlockPredFn>(resolve("block_is_hazard"));
    g.crosshair = reinterpret_cast<CrosshairFn>(resolve("crosshair_block"));

    {
        std::ostringstream o;
        for (std::size_t i = 0; i < missing.size(); ++i) o << (i ? ", " : "") << missing[i];
        g.missingText = o.str();
    }

    if (!tick) return false;  // sem o tick nada roda

    g.playerTickTarget = tick;
    if (pl::memory::hook(tick, reinterpret_cast<void*>(&playerTickDetour), reinterpret_cast<void**>(&g.playerTickOrig),
                         pl::memory::HookPriority::Normal) != 0 || !g.playerTickOrig) {
        g.missingText = "falha ao instalar hook player_tick";
        g.playerTickTarget = nullptr;
        return false;
    }
    if (moveHook) {
        g.moveInputTarget = moveHook;
        if (pl::memory::hook(moveHook, reinterpret_cast<void*>(&moveInputDetour), reinterpret_cast<void**>(&g.moveInputOrig),
                             pl::memory::HookPriority::Normal) != 0 || !g.moveInputOrig) {
            g.moveInputTarget = nullptr;
            missing.push_back("hook move_input");
            g.missingText += (g.missingText.empty() ? "" : ", ") + std::string("hook move_input");
        }
    }
    installed_.store(true);
    return true;
}

void NativeBridge::uninstall() {
    if (!installed_.exchange(false)) return;
    // solta tudo antes de remover
    g.moveActive.store(false);
    g.breaking = false;
    if (g.moveInputTarget) (void)pl::memory::unhook(g.moveInputTarget, reinterpret_cast<void*>(&moveInputDetour));
    if (g.playerTickTarget) (void)pl::memory::unhook(g.playerTickTarget, reinterpret_cast<void*>(&playerTickDetour));
    g.moveInputTarget = g.playerTickTarget = nullptr;
    g.player.store(nullptr);
}

std::string NativeBridge::missing() const { return g.missingText; }

bool NativeBridge::ready() const {
    if (!installed_.load() || !g.missingText.empty()) return false;
    // ponteiro do jogador so e confiavel se o tick chegou ha pouco (mundo fechado => para de chegar)
    return g.player.load() != nullptr && (nowMs() - g.lastTickMs.load()) < 400;
}

// ---- thread do jogo ---------------------------------------------------------------------------
void NativeBridge::onPlayerTick(void* player) {
    g.player.store(player);
    g.lastTickMs.store(nowMs());
    g.region = g.getRegion ? g.getRegion(player) : nullptr;
    runtime().controller.onTick(*this);
}

void NativeBridge::onMoveInput(void* inputState) {
    if (!g.moveActive.load() || !inputState) return;
    wr<float>(inputState, off("move_forward"), g.moveF.load());
    wr<float>(inputState, off("move_strafe"), g.moveS.load());
    wr<std::uint8_t>(inputState, off("move_jump"), g.moveJump.load() ? 1 : 0);
    if (hasOff("move_sneak")) wr<std::uint8_t>(inputState, off("move_sneak"), g.moveSneak.load() ? 1 : 0);
}

// ---- IGameBridge ------------------------------------------------------------------------------
bool NativeBridge::pose(Pose& out) {
    void* p = g.player.load();
    if (!p || !installed_.load() || !hasOff("pos_x")) return false;
    out.feet = {rd<float>(p, off("pos_x")), rd<float>(p, off("pos_x") + 4), rd<float>(p, off("pos_x") + 8)};
    if (g.cfg.opt.count("pos_is_eye") && g.cfg.opt["pos_is_eye"] == "1") out.feet.y -= kEyeHeight;
    out.yaw = rd<float>(p, off("yaw"));
    out.pitch = rd<float>(p, off("pitch"));
    out.onGround = hasOff("on_ground") ? rd<std::uint8_t>(p, off("on_ground")) != 0 : true;
    out.valid = std::isfinite(out.feet.x) && std::isfinite(out.feet.y) && std::isfinite(out.feet.z) &&
                std::fabs(out.feet.x) < 3.0e7f && std::fabs(out.feet.z) < 3.0e7f;
    return out.valid;
}

BlockClass NativeBridge::blockAt(Vec3i c) {
    if (!g.region || !g.getBlock || !g.isAir) return BlockClass::Unknown;
    if (c.y < -64 || c.y > 320) return BlockClass::Unknown;
    const int pos[3] = {c.x, c.y, c.z};
    const void* blk = g.getBlock(g.region, pos);
    if (!blk) return BlockClass::Unknown;
    if (g.isHazard && g.isHazard(blk)) return BlockClass::Hazard;
    if (g.isLiquid && g.isLiquid(blk)) return BlockClass::Liquid;
    if (g.isAir(blk)) return BlockClass::Air;
    if (g.isUnbreakable && g.isUnbreakable(blk)) return BlockClass::Unbreakable;
    return BlockClass::Solid;
}

bool NativeBridge::lookedAtBlock(Vec3i& out) {
    void* p = g.player.load();
    if (!p || !g.crosshair) return false;
    int pos[3]{};
    if (!g.crosshair(p, pos)) return false;
    out = {pos[0], pos[1], pos[2]};
    return true;
}

void NativeBridge::setMove(float forward, float strafe, bool jump, bool sneak) {
    g.moveF.store(std::clamp(forward, -1.0f, 1.0f));
    g.moveS.store(std::clamp(strafe, -1.0f, 1.0f));
    g.moveJump.store(jump);
    g.moveSneak.store(sneak);
    g.moveActive.store(forward != 0 || strafe != 0 || jump || sneak);
}

void NativeBridge::setLook(float yaw, float pitch) {
    void* p = g.player.load();
    if (!p || !hasOff("yaw")) return;
    wr<float>(p, off("yaw"), wrapDeg(yaw));
    wr<float>(p, off("pitch"), std::clamp(pitch, -89.9f, 89.9f));
    // rotacao "anterior" (interpolacao do render) para nao dar tranco visual
    if (hasOff("yaw_prev")) wr<float>(p, off("yaw_prev"), wrapDeg(yaw));
    if (hasOff("pitch_prev")) wr<float>(p, off("pitch_prev"), std::clamp(pitch, -89.9f, 89.9f));
    if (hasOff("yaw_head")) wr<float>(p, off("yaw_head"), wrapDeg(yaw));
}

void NativeBridge::setBreakTarget(const Vec3i* target, int face) {
    void* p = g.player.load();
    if (!target || !p) {
        g.breaking = false;
        g.breakStarted = false;
        return;
    }
    if (!g.startDestroy || !g.continueDestroy || !hasOff("gamemode_ptr")) return;
    void* gm = rd<void*>(p, off("gamemode_ptr"));
    if (!gm) return;
    const int pos[3] = {target->x, target->y, target->z};
    bool flag = false;
    if (!g.breaking || !(g.breakCell == *target)) {  // alvo novo: comeca a quebrar
        g.breaking = true;
        g.breakCell = *target;
        g.breakStarted = g.startDestroy(gm, pos, static_cast<std::uint8_t>(face), &flag);
        return;
    }
    (void)g.continueDestroy(gm, pos, static_cast<std::uint8_t>(face), &flag);  // 1x por tick enquanto segura
}

}  // namespace am
