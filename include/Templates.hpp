// Templates de escavacao: gera o conjunto de blocos (celulas) de cada formato.
#pragma once

#include <string>
#include <vector>

#include "Types.hpp"

namespace am {

enum class TemplateKind : int { Box2x2 = 0, Box3x3 = 1, Stairs = 2, Tunnel = 3, Custom = 4 };
constexpr int kTemplateCount = 5;

constexpr int kMaxLength = 256;
constexpr long long kMaxCells = 20000;

const char* templateName(TemplateKind k);        // "2x2", "3x3", ...
const char* templateLabelPt(TemplateKind k);     // rotulo p/ menu (PT-BR)

struct TemplateParams {
    TemplateKind kind{TemplateKind::Box3x3};
    Facing facing{Facing::North};
    Vec3i origin{};          // celula dos pes do jogador (nao e escavada)
    int length{16};          // blocos para frente
    int width{1};            // largura (Tunnel / Stairs)
    int height{2};           // altura (Tunnel)
    bool stairsDown{true};   // Stairs: descer (true) ou subir (false)
    Vec3i pos1{}, pos2{};    // Custom: cantos da caixa
};

// Retorna celulas unicas. Vazio + `error` preenchido se invalido / grande demais.
std::vector<Vec3i> buildCells(const TemplateParams& p, std::string* error = nullptr);

Box boundsOf(const std::vector<Vec3i>& cells);

}  // namespace am
