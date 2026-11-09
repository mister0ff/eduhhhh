#pragma once
namespace am {
class AutoMinerMod {
public:
    bool load();
    bool enable();
    bool disable();
    bool unload();
private:
    bool menu_{}, gl_{}, bridge_{};
};
}
