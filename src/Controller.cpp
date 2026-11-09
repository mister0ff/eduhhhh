#include "Controller.hpp"

#include <cmath>

namespace am {
namespace {
bool sameParams(const TemplateParams& a, const TemplateParams& b) {
    return a.kind == b.kind && a.facing == b.facing && a.origin == b.origin && a.length == b.length &&
           a.width == b.width && a.height == b.height && a.stairsDown == b.stairsDown && a.pos1 == b.pos1 &&
           a.pos2 == b.pos2;
}
Vec3i feetCellOf(const Pose& p) {
    return {static_cast<int>(std::floor(p.feet.x)), static_cast<int>(std::floor(p.feet.y + 0.05f)),
            static_cast<int>(std::floor(p.feet.z))};
}
}  // namespace

std::shared_ptr<const OverlayData> Controller::overlay() const {
    std::lock_guard<std::mutex> l(ovMutex_);
    return overlay_;
}

TemplateParams Controller::paramsFor(const Settings& s, const Pose& p) const {
    TemplateParams t;
    t.kind = static_cast<TemplateKind>(std::clamp(s.templateIdx, 0, kTemplateCount - 1));
    t.facing = facingFromYaw(p.yaw);
    t.origin = feetCellOf(p);
    t.length = s.length;
    t.width = s.tunnelW;
    t.height = s.tunnelH;
    t.stairsDown = s.stairsDown;
    t.pos1 = pos1_;
    t.pos2 = pos2_;
    return t;
}

void Controller::rebuildPreview(const Settings& s, const Pose& p) {
    const TemplateParams tp = paramsFor(s, p);
    if (haveParams_ && sameParams(tp, lastParams_)) return;
    lastParams_ = tp;
    haveParams_ = true;
    error_.clear();
    if (tp.kind == TemplateKind::Custom && !(pos1Set_ && pos2Set_)) {
        previewCells_.clear();
        previewBounds_ = {};
        error_ = "MARQUE POS1 E POS2";
        return;
    }
    previewCells_ = buildCells(tp, &error_);
    previewBounds_ = boundsOf(previewCells_);
}

void Controller::startRun(IGameBridge& b, const Settings& s, const Pose& p) {
    if (exec_.active()) return;
    const TemplateParams tp = paramsFor(s, p);
    if (tp.kind == TemplateKind::Custom && !(pos1Set_ && pos2Set_)) {
        error_ = "MARQUE POS1 E POS2";
        return;
    }
    std::string err;
    std::vector<Vec3i> cells = buildCells(tp, &err);
    if (cells.empty()) {
        error_ = err.empty() ? "NADA PARA ESCAVAR" : err;
        return;
    }
    error_.clear();
    cells = orderCells(std::move(cells), tp, tp.origin);
    previewOn_ = false;  // o outline passa a mostrar o progresso
    (void)b;
    exec_.begin(std::move(cells));
}

void Controller::handle(Command c, IGameBridge& b, const Pose& p, const Settings& s) {
    switch (c) {
        case Command::MarkPos1Feet:
        case Command::MarkPos1Look:
        case Command::MarkPos2Feet:
        case Command::MarkPos2Look: {
            const bool first = (c == Command::MarkPos1Feet || c == Command::MarkPos1Look);
            const bool look = (c == Command::MarkPos1Look || c == Command::MarkPos2Look);
            Vec3i cell{};
            if (look) {
                if (!b.lookedAtBlock(cell)) { error_ = "NENHUM BLOCO NA MIRA"; break; }
            } else {
                cell = feetCellOf(p);
            }
            if (first) { pos1_ = cell; pos1Set_ = true; } else { pos2_ = cell; pos2Set_ = true; }
            error_.clear();
            // marcar pontos = modo area; mostra o outline para o jogador confirmar
            settings_.update([](Settings& st) { st.templateIdx = static_cast<int>(TemplateKind::Custom); });
            previewOn_ = true;
            haveParams_ = false;
            break;
        }
        case Command::TogglePreview:
            previewOn_ = !previewOn_;
            haveParams_ = false;
            break;
        case Command::ToggleMenu:
            menuOpen_ = !menuOpen_;
            if (menuOpen_) { previewOn_ = true; haveParams_ = false; }
            break;
        case Command::NextTemplate:
        case Command::PrevTemplate: {
            const int d = (c == Command::NextTemplate) ? 1 : kTemplateCount - 1;
            settings_.update([d](Settings& st) { st.templateIdx = (st.templateIdx + d) % kTemplateCount; });
            previewOn_ = true;
            haveParams_ = false;
            break;
        }
        case Command::Start:
            startRun(b, s, p);
            break;
        case Command::Stop:
            if (exec_.active()) exec_.stop(&b, "PARADO PELO USUARIO", true);
            else previewOn_ = false;
            break;
    }
}

void Controller::onTick(IGameBridge& b) {
    ++ticks_;
    Pose p;
    const bool havePose = b.pose(p) && p.valid;
    const std::vector<Command> cmds = commands_.drain();
    Settings s = settings_.get();

    if (havePose) {
        for (Command c : cmds) handle(c, b, p, s);
        s = settings_.get();  // handle() pode ter mudado o template
    } else if (!cmds.empty()) {
        error_ = "JOGADOR INDISPONIVEL";
    }

    if (exec_.active()) {
        exec_.tick(b, s);
    } else if (previewOn_ && havePose) {
        rebuildPreview(s, p);
    }
    publish(s, false);
    publishStatus(s);
}

void Controller::shutdown(IGameBridge* bridge) {
    if (exec_.active()) exec_.stop(bridge, "PARADO", true);
    previewOn_ = false;
    menuOpen_ = false;
    std::lock_guard<std::mutex> l(ovMutex_);
    overlay_.reset();
}

void Controller::publish(const Settings& s, bool force) {
    const bool running = exec_.active();
    // enquanto executa, publica a cada 3 ticks (alvo muda); preview publica sempre (barato: ja esta pronto)
    if (!force && running && (ticks_ % 3) != 0) return;

    auto d = std::make_shared<OverlayData>();
    d->pos1Set = pos1Set_;
    d->pos2Set = pos2Set_;
    d->pos1 = pos1_;
    d->pos2 = pos2_;
    if (running) {
        d->running = true;
        d->cells = exec_.upcoming(512);
        d->hasBounds = !exec_.allCells().empty();
        d->bounds = boundsOf(exec_.allCells());
        d->hasTarget = exec_.currentTarget(d->target);
        d->skipped = exec_.skippedCells(64);
    } else if (previewOn_ && !previewCells_.empty()) {
        d->preview = true;
        d->hasBounds = true;
        d->bounds = previewBounds_;
        if (previewCells_.size() <= 512) d->cells = previewCells_;
    }
    (void)s;
    std::lock_guard<std::mutex> l(ovMutex_);
    overlay_ = std::move(d);
}

void Controller::publishStatus(const Settings& s) {
    Status st;
    st.state = exec_.state();
    st.done = exec_.done();
    st.total = exec_.total();
    st.skipped = exec_.skippedCount();
    st.cells = static_cast<int>(previewCells_.size());
    st.templateIdx = s.templateIdx;
    st.previewOn = previewOn_;
    st.menuOpen = menuOpen_;
    st.pos1Set = pos1Set_;
    st.pos2Set = pos2Set_;
    st.pos1 = pos1_;
    st.pos2 = pos2_;
    if (!error_.empty() && !exec_.active()) st.message = error_;
    else if (exec_.state() != RunState::Idle) st.message = exec_.message();
    status_.set(std::move(st));
}

}  // namespace am
