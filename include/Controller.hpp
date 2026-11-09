// Cola tudo: comandos da UI/teclas, selecao (template ou Pos1/Pos2), preview e execucao.
// onTick() roda na thread do jogo, uma vez por tick.
#pragma once

#include <memory>
#include <mutex>

#include "Executor.hpp"
#include "Render.hpp"

namespace am {

class Controller {
public:
    Controller(SettingsStore& settings, StatusBoard& status, CommandQueue& commands)
        : settings_(settings), status_(status), commands_(commands) {}

    void onTick(IGameBridge& bridge);
    // chamado pela thread de render; barato (troca de shared_ptr)
    std::shared_ptr<const OverlayData> overlay() const;
    // solta entradas virtuais e para (descarregar o mod, mundo fechado...)
    void shutdown(IGameBridge* bridge);

    const Executor& executor() const { return exec_; }

private:
    void handle(Command c, IGameBridge& b, const Pose& p, const Settings& s);
    TemplateParams paramsFor(const Settings& s, const Pose& p) const;
    void rebuildPreview(const Settings& s, const Pose& p);
    void startRun(IGameBridge& b, const Settings& s, const Pose& p);
    void publish(const Settings& s, bool force);
    void publishStatus(const Settings& s);

    SettingsStore& settings_;
    StatusBoard& status_;
    CommandQueue& commands_;
    Executor exec_;

    bool previewOn_{false}, menuOpen_{false};
    bool pos1Set_{false}, pos2Set_{false};
    Vec3i pos1_{}, pos2_{};
    std::string error_;

    // preview
    TemplateParams lastParams_{};
    bool haveParams_{false};
    std::vector<Vec3i> previewCells_;
    Box previewBounds_{};
    std::string lastMessage_;
    long long ticks_{0};

    mutable std::mutex ovMutex_;
    std::shared_ptr<const OverlayData> overlay_;
};

}  // namespace am
