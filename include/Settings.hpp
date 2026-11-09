// Configuracao, fila de comandos (UI -> thread do jogo) e quadro de status (jogo -> UI).
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Types.hpp"

namespace am {

struct Settings {
    // template
    int templateIdx{1};  // TemplateKind
    int length{16};
    int tunnelW{1};
    int tunnelH{2};
    bool stairsDown{true};
    // visual
    bool showOutline{true};
    bool showHud{true};
    float fov{70.0f};
    // automacao
    bool avoidLiquids{true};
    float reach{4.5f};
    float turnRate{20.0f};      // graus por tick
    int planNodeCap{1500};
    int tickBudgetUs{2500};     // orcamento de CPU por tick
    int breakTimeoutTicks{240};
    int stuckTicks{20};
    int navTimeoutTicks{400};
    // teclas (nome de 1 tecla; o launcher entrega o evento por onKeybind)
    std::string keyMenu{"M"}, keyNext{"N"}, keyPos1{"Z"}, keyPos2{"X"}, keyStart{"V"}, keyStop{"C"};
};

void sanitize(Settings& s);
std::string serialize(const Settings& s);
void parseInto(Settings& s, std::string_view text);  // linhas chave=valor; ignora o desconhecido

class SettingsStore {
public:
    Settings get() const;
    void update(const std::function<void(Settings&)>& fn);
    void setPath(std::string path);
    bool load();
    bool save() const;

private:
    mutable std::mutex m_;
    Settings s_;
    std::string path_;
};

enum class Command {
    MarkPos1Feet, MarkPos2Feet, MarkPos1Look, MarkPos2Look,
    TogglePreview, ToggleMenu, NextTemplate, PrevTemplate, Start, Stop
};

class CommandQueue {
public:
    void push(Command c);
    std::vector<Command> drain();

private:
    std::mutex m_;
    std::vector<Command> q_;
};

struct Status {
    RunState state{RunState::Idle};
    int done{}, total{}, skipped{};
    int cells{};  // celulas do preview atual
    int templateIdx{1};
    bool previewOn{false};
    bool menuOpen{false};
    bool pos1Set{false}, pos2Set{false};
    Vec3i pos1{}, pos2{};
    std::string message;  // PT-BR, ASCII maiusculo (a fonte do HUD so tem isso)
    std::string bridgeMissing;
};

class StatusBoard {
public:
    Status get() const;
    void set(Status s);
    void setBridgeMissing(std::string m);

private:
    mutable std::mutex m_;
    Status s_;
};

}  // namespace am
