// Implementacao real de IGameBridge para libminecraftpe.so (Android arm64).
//
// IMPORTANTE: o Minecraft Bedrock nao exporta simbolos. Cada funcao/offset abaixo precisa vir de um
// arquivo de bindings `autominer_bindings.cfg` (ao lado do config), feito para a SUA versao do jogo
// (engenharia reversa do libminecraftpe.so com Ghidra/IDA). Sem o arquivo o mod carrega, o menu e
// o preview funcionam, mas a automacao informa "FALTA: <binding>" e nao executa. Ver README.md.
#pragma once

#include <atomic>
#include <string>

#include "Bridge.hpp"

namespace am {

class NativeBridge final : public IGameBridge {
public:
    // Le o arquivo de bindings, resolve assinaturas e instala hooks. Retorna true se os hooks
    // obrigatorios (tick) foram instalados. Nunca lanca.
    bool install(const std::string& bindingsPath);
    void uninstall();

    bool ready() const override;
    std::string missing() const override;
    bool pose(Pose& out) override;
    bool lookedAtBlock(Vec3i& out) override;
    BlockClass blockAt(Vec3i cell) override;
    void setMove(float forward, float strafe, bool jump, bool sneak) override;
    void setLook(float yaw, float pitch) override;
    void setBreakTarget(const Vec3i* target, int face) override;

    // chamado pelos detours (thread do jogo)
    void onPlayerTick(void* player);
    void onMoveInput(void* inputState);

private:
    std::atomic<bool> installed_{false};
};

NativeBridge& nativeBridge();

}  // namespace am
