#include "Mod.hpp"

#include "GlRenderer.hpp"
#include "Menu.hpp"
#include "NativeBridge.hpp"
#include "Runtime.hpp"

namespace am {
bool AutoMinerMod::load() {
    Runtime& rt = runtime();
    rt.configDir = discoverConfigDir();
    if (!rt.configDir.empty()) {
        rt.settings.setPath(rt.configDir + "/autominer.cfg");
        if (!rt.settings.load()) (void)rt.settings.save();  // cria com os padroes
    }
    return true;
}

bool AutoMinerMod::enable() {
    Runtime& rt = runtime();
    menu_ = registerMenu();
    bridge_ = nativeBridge().install(rt.configDir + "/autominer_bindings.cfg");
    rt.status.setBridgeMissing(nativeBridge().missing());
    gl_ = installGlOverlay();
    return menu_ || gl_ || bridge_;
}

bool AutoMinerMod::disable() {
    Runtime& rt = runtime();
    rt.controller.shutdown(&nativeBridge());
    if (menu_) { unregisterMenu(); menu_ = false; }
    if (gl_) { removeGlOverlay(); gl_ = false; }
    if (bridge_) { nativeBridge().uninstall(); bridge_ = false; }
    return true;
}

bool AutoMinerMod::unload() {
    disable();
    (void)runtime().settings.save();
    return true;
}
}  // namespace am
