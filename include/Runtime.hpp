// Estado global do mod (um unico conjunto de instancias).
#pragma once

#include <string>

#include "Controller.hpp"
#include "Settings.hpp"

namespace am {

struct Runtime {
    SettingsStore settings;
    StatusBoard status;
    CommandQueue commands;
    Controller controller{settings, status, commands};
    std::string configDir;  // pasta de config/bindings
};

Runtime& runtime();

// Descobre/cria a pasta de config do mod (override: variavel de ambiente AUTOMINER_DIR).
std::string discoverConfigDir();

}  // namespace am
