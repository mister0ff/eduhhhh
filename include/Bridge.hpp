// Interface entre a logica do mod e o jogo. A logica (Executor/Planner) so conhece isto;
// a implementacao real (NativeBridge) fala com libminecraftpe.so, os testes usam um mock.
#pragma once

#include <string>

#include "Types.hpp"

namespace am {

struct IWorld {
    virtual ~IWorld() = default;
    virtual BlockClass blockAt(Vec3i cell) = 0;
};

struct IGameBridge : IWorld {
    // true quando todos os bindings obrigatorios resolveram E o jogador local esta vivo/atualizado.
    virtual bool ready() const = 0;
    // texto com o que falta (vazio = nada). Mostrado no menu.
    virtual std::string missing() const = 0;

    virtual bool pose(Pose& out) = 0;
    // bloco sob a mira (crosshair)
    virtual bool lookedAtBlock(Vec3i& out) = 0;

    // Entrada "virtual" aplicada no proximo tick de input do jogo (como se fosse o jogador).
    //   forward/strafe em [-1,1]
    virtual void setMove(float forward, float strafe, bool jump, bool sneak) = 0;
    // Define a rotacao da camera (graus, convencao do Types.hpp).
    virtual void setLook(float yaw, float pitch) = 0;
    // Segura o botao de quebrar em `target` (face 0..5 = D,U,N,S,W,E). nullptr solta.
    virtual void setBreakTarget(const Vec3i* target, int face) = 0;
};

}  // namespace am
