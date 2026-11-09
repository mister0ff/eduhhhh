#include "Abi.hpp"
#include "Mod.hpp"

namespace {
am::AutoMinerMod gMod;

template <bool (am::AutoMinerMod::*Fn)()>
bool thunk(void*, void*) {
    try {
        return (gMod.*Fn)();
    } catch (...) {
        return false;
    }
}
}  // namespace

extern "C" __attribute__((visibility("default"))) ModRegistration* PLGetModRegistration() {
    static ModRegistration reg{&gMod, &thunk<&am::AutoMinerMod::load>, &thunk<&am::AutoMinerMod::enable>,
                               &thunk<&am::AutoMinerMod::disable>, &thunk<&am::AutoMinerMod::unload>};
    return &reg;
}
