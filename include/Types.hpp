// Tipos basicos e matematica compartilhada (sem dependencia do jogo).
//
// Convencoes (Bedrock):
//   * yaw 0 = olhando para +Z (Sul), yaw 90 = -X (Oeste), 180 = -Z (Norte), 270 = +X (Leste)
//   * pitch positivo = olhando para BAIXO
//   * eixo Y para cima; posicao do jogador = pes
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace am {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
constexpr float kEyeHeight = 1.62f;

struct Vec3i {
    int x{}, y{}, z{};
    constexpr bool operator==(const Vec3i& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3i& o) const { return !(*this == o); }
    constexpr bool operator<(const Vec3i& o) const { return x != o.x ? x < o.x : y != o.y ? y < o.y : z < o.z; }
    constexpr Vec3i operator+(const Vec3i& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3i operator-(const Vec3i& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3i operator*(int k) const { return {x * k, y * k, z * k}; }
};

struct Vec3iHash {
    std::size_t operator()(const Vec3i& v) const noexcept {
        return (static_cast<std::size_t>(static_cast<std::uint32_t>(v.x)) * 73856093u) ^
               (static_cast<std::size_t>(static_cast<std::uint32_t>(v.y)) * 19349663u) ^
               (static_cast<std::size_t>(static_cast<std::uint32_t>(v.z)) * 83492791u);
    }
};

struct Vec3f {
    float x{}, y{}, z{};
    constexpr Vec3f operator+(const Vec3f& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3f operator-(const Vec3f& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3f operator*(float k) const { return {x * k, y * k, z * k}; }
};
inline float dot(const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(const Vec3f& a) { return std::sqrt(dot(a, a)); }
inline Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3f centerOf(const Vec3i& c) { return {c.x + 0.5f, c.y + 0.5f, c.z + 0.5f}; }
inline Vec3i cellOf(const Vec3f& p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}

struct Box {
    Vec3i min{}, max{};
    long long volume() const {
        return static_cast<long long>(max.x - min.x + 1) * (max.y - min.y + 1) * (max.z - min.z + 1);
    }
};

// ---- angulos ---------------------------------------------------------------
inline float wrapDeg(float a) {
    a = std::fmod(a, 360.0f);
    return a < 0 ? a + 360.0f : a;
}
// menor diferenca assinada (to - from) em (-180, 180]
inline float angleDelta(float from, float to) {
    float d = std::fmod(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}
inline float approachAngle(float from, float to, float maxStep) {
    return from + std::clamp(angleDelta(from, to), -maxStep, maxStep);
}
inline float yawForDirection(float dx, float dz) { return std::atan2(-dx, dz) * kRad2Deg; }
inline Vec3f lookDir(float yawDeg, float pitchDeg) {
    const float y = yawDeg * kDeg2Rad, p = pitchDeg * kDeg2Rad;
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}
inline void lookAngles(const Vec3f& from, const Vec3f& to, float& yaw, float& pitch) {
    const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    yaw = yawForDirection(dx, dz);
    pitch = -std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * kRad2Deg;
}

// ---- orientacao horizontal --------------------------------------------------
enum class Facing : std::uint8_t { North, South, West, East };  // N=-Z S=+Z W=-X E=+X

constexpr Vec3i forwardOf(Facing f) {
    switch (f) {
        case Facing::North: return {0, 0, -1};
        case Facing::South: return {0, 0, 1};
        case Facing::West: return {-1, 0, 0};
        case Facing::East: return {1, 0, 0};
    }
    return {0, 0, 1};
}
// direita de quem olha para `f` (visto de cima, sentido horario)
constexpr Vec3i rightOf(Facing f) {
    const Vec3i fw = forwardOf(f);
    return {-fw.z, 0, fw.x};
}
inline Facing facingFromYaw(float yawDeg) {
    const float y = wrapDeg(yawDeg);
    if (y >= 315.0f || y < 45.0f) return Facing::South;
    if (y < 135.0f) return Facing::West;
    if (y < 225.0f) return Facing::North;
    return Facing::East;
}

// ---- mundo / jogador --------------------------------------------------------
enum class BlockClass : std::uint8_t {
    Unknown,      // chunk nao carregado / leitura falhou
    Air,          // ar (ou passavel)
    Solid,        // quebravel e solido
    Liquid,       // agua (nao quebra, nao anda)
    Unbreakable,  // bedrock, barreira... (serve de piso)
    Hazard        // lava, fogo, cacto, magma...
};

struct Pose {
    Vec3f feet{};
    float yaw{};
    float pitch{};
    bool onGround{true};
    bool valid{false};
    Vec3f eye() const { return {feet.x, feet.y + kEyeHeight, feet.z}; }
};

enum class RunState : std::uint8_t {
    Idle, Planning, Navigating, Aiming, Breaking, Recovering, Verifying, Done, Aborted
};

}  // namespace am
