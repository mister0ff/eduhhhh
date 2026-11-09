// Menu do launcher (pl::modmenu): modelo, tamanhos, teclas e botoes. Tudo so enfileira Command
// ou altera Settings; a execucao acontece na thread do jogo.
#include "Menu.hpp"

#include <cstdlib>
#include <string>

#include <pl/ModMenu.hpp>

#include "Runtime.hpp"

namespace am {
namespace {
constexpr const char* kId = "autominer";
using pl::modmenu::ConfigEntry;
using pl::modmenu::ConfigType;

ConfigEntry entry(const char* key, const char* name, ConfigType t, std::string def, std::string mn = {}, std::string mx = {}) {
    ConfigEntry e;
    e.key = key; e.displayName = name; e.type = t; e.defaultValue = std::move(def); e.minValue = std::move(mn); e.maxValue = std::move(mx);
    return e;
}

bool asBool(std::string_view v) { return v == "1" || v == "true" || v == "on"; }

void onConfig(std::string_view, std::string_view key, std::string_view value) {
    Runtime& rt = runtime();
    const std::string k(key), v(value);
    // botoes chegam como "pressionado"
    static const struct { const char* key; Command cmd; } kButtons[] = {
        {"btn_menu", Command::ToggleMenu}, {"btn_next", Command::NextTemplate}, {"btn_prev", Command::PrevTemplate},
        {"btn_preview", Command::TogglePreview}, {"btn_pos1", Command::MarkPos1Feet}, {"btn_pos2", Command::MarkPos2Feet},
        {"btn_pos1_look", Command::MarkPos1Look}, {"btn_pos2_look", Command::MarkPos2Look},
        {"btn_start", Command::Start}, {"btn_stop", Command::Stop}};
    for (const auto& b : kButtons)
        if (k == b.key) { rt.commands.push(b.cmd); return; }

    const float f = static_cast<float>(std::atof(v.c_str()));
    const int i = std::atoi(v.c_str());
    rt.settings.update([&](Settings& s) {
        if (k == "template") s.templateIdx = i;
        else if (k == "length") s.length = i;
        else if (k == "tunnel_w") s.tunnelW = i;
        else if (k == "tunnel_h") s.tunnelH = i;
        else if (k == "stairs_down") s.stairsDown = asBool(v);
        else if (k == "show_outline") s.showOutline = asBool(v);
        else if (k == "show_hud") s.showHud = asBool(v);
        else if (k == "avoid_liquids") s.avoidLiquids = asBool(v);
        else if (k == "fov") s.fov = f;
        else if (k == "reach") s.reach = f;
        else if (k == "turn_rate") s.turnRate = f;
        else if (k == "key_menu" && !v.empty()) s.keyMenu = v;
        else if (k == "key_next" && !v.empty()) s.keyNext = v;
        else if (k == "key_pos1" && !v.empty()) s.keyPos1 = v;
        else if (k == "key_pos2" && !v.empty()) s.keyPos2 = v;
        else if (k == "key_start" && !v.empty()) s.keyStart = v;
        else if (k == "key_stop" && !v.empty()) s.keyStop = v;
    });
}

void onKeybind(std::string_view, std::string_view key, bool isDown) {
    if (!isDown) return;  // so na descida da tecla
    Runtime& rt = runtime();
    const std::string k(key);
    if (k == "key_menu") rt.commands.push(Command::ToggleMenu);
    else if (k == "key_next") rt.commands.push(Command::NextTemplate);
    else if (k == "key_pos1") rt.commands.push(Command::MarkPos1Feet);
    else if (k == "key_pos2") rt.commands.push(Command::MarkPos2Feet);
    else if (k == "key_start") rt.commands.push(Command::Start);
    else if (k == "key_stop") rt.commands.push(Command::Stop);
}
}  // namespace

bool registerMenu() {
    const Settings s = runtime().settings.get();
    pl::modmenu::ModuleInfo m;
    m.moduleId = kId;
    m.displayName = "AutoMiner";
    m.description = "Mineracao automatica: 2x2, 3x3, escada, tunel ou area Pos1-Pos2, com preview em outline.";
    m.modId = "autominer";
    m.defaultEnabled = true;
    m.hideInHudEditor = true;
    auto b = [](bool v) { return std::string(v ? "1" : "0"); };
    m.configs = {
        entry("template", "Modelo: 0=2x2 1=3x3 2=Escada 3=Tunel 4=Pos1-Pos2", ConfigType::SliderInt, std::to_string(s.templateIdx), "0", "4"),
        entry("length", "Comprimento", ConfigType::SliderInt, std::to_string(s.length), "1", "128"),
        entry("tunnel_w", "Tunel/Escada: largura", ConfigType::SliderInt, std::to_string(s.tunnelW), "1", "9"),
        entry("tunnel_h", "Tunel: altura", ConfigType::SliderInt, std::to_string(s.tunnelH), "1", "9"),
        entry("stairs_down", "Escada descendo", ConfigType::Toggle, b(s.stairsDown)),
        entry("btn_menu", "Abrir/fechar menu na tela", ConfigType::Button, ""),
        entry("btn_next", "Proximo modelo", ConfigType::Button, ""),
        entry("btn_prev", "Modelo anterior", ConfigType::Button, ""),
        entry("btn_preview", "Mostrar/ocultar preview (outline)", ConfigType::Button, ""),
        entry("btn_pos1", "Pos1 = onde estou", ConfigType::Button, ""),
        entry("btn_pos2", "Pos2 = onde estou", ConfigType::Button, ""),
        entry("btn_pos1_look", "Pos1 = bloco na mira", ConfigType::Button, ""),
        entry("btn_pos2_look", "Pos2 = bloco na mira", ConfigType::Button, ""),
        entry("btn_start", "INICIAR", ConfigType::Button, ""),
        entry("btn_stop", "PARAR", ConfigType::Button, ""),
        entry("show_outline", "Mostrar outline", ConfigType::Toggle, b(s.showOutline)),
        entry("show_hud", "Mostrar HUD/menu", ConfigType::Toggle, b(s.showHud)),
        entry("avoid_liquids", "Evitar lava/agua", ConfigType::Toggle, b(s.avoidLiquids)),
        entry("fov", "FOV do outline (igual ao do jogo)", ConfigType::SliderFloat, std::to_string(s.fov), "30", "110"),
        entry("reach", "Alcance", ConfigType::SliderFloat, std::to_string(s.reach), "2", "5"),
        entry("turn_rate", "Velocidade da camera (graus/tick)", ConfigType::SliderFloat, std::to_string(s.turnRate), "3", "60"),
        entry("key_menu", "Tecla: menu", ConfigType::Keybind, s.keyMenu),
        entry("key_next", "Tecla: proximo modelo", ConfigType::Keybind, s.keyNext),
        entry("key_pos1", "Tecla: Pos1", ConfigType::Keybind, s.keyPos1),
        entry("key_pos2", "Tecla: Pos2", ConfigType::Keybind, s.keyPos2),
        entry("key_start", "Tecla: iniciar", ConfigType::Keybind, s.keyStart),
        entry("key_stop", "Tecla: parar", ConfigType::Keybind, s.keyStop),
    };
    m.onToggle = [](std::string_view, bool enabled) {
        if (!enabled) runtime().commands.push(Command::Stop);
    };
    m.onConfigChanged = &onConfig;
    m.onKeybind = &onKeybind;
    return pl::modmenu::registerModule(m);
}

void unregisterMenu() { pl::modmenu::unregisterModule(kId); }
}  // namespace am
