#include "Settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>

#include "Templates.hpp"

namespace am {

void sanitize(Settings& s) {
    s.templateIdx = std::clamp(s.templateIdx, 0, kTemplateCount - 1);
    s.length = std::clamp(s.length, 1, kMaxLength);
    s.tunnelW = std::clamp(s.tunnelW, 1, 9);
    s.tunnelH = std::clamp(s.tunnelH, 1, 9);
    s.fov = std::clamp(s.fov, 30.0f, 110.0f);
    s.reach = std::clamp(s.reach, 2.0f, 5.0f);
    s.turnRate = std::clamp(s.turnRate, 3.0f, 60.0f);
    s.planNodeCap = std::clamp(s.planNodeCap, 100, 20000);
    s.tickBudgetUs = std::clamp(s.tickBudgetUs, 200, 10000);
    s.breakTimeoutTicks = std::clamp(s.breakTimeoutTicks, 20, 2400);
    s.stuckTicks = std::clamp(s.stuckTicks, 5, 200);
    s.navTimeoutTicks = std::clamp(s.navTimeoutTicks, 40, 4000);
    for (std::string* k : {&s.keyMenu, &s.keyNext, &s.keyPos1, &s.keyPos2, &s.keyStart, &s.keyStop})
        if (k->empty()) *k = "?";
}

std::string serialize(const Settings& s) {
    std::ostringstream o;
    o << "template=" << s.templateIdx << "\nlength=" << s.length << "\ntunnel_w=" << s.tunnelW
      << "\ntunnel_h=" << s.tunnelH << "\nstairs_down=" << s.stairsDown << "\nshow_outline=" << s.showOutline
      << "\nshow_hud=" << s.showHud << "\nfov=" << s.fov << "\navoid_liquids=" << s.avoidLiquids
      << "\nreach=" << s.reach << "\nturn_rate=" << s.turnRate << "\nplan_node_cap=" << s.planNodeCap
      << "\ntick_budget_us=" << s.tickBudgetUs << "\nbreak_timeout_ticks=" << s.breakTimeoutTicks
      << "\nstuck_ticks=" << s.stuckTicks << "\nnav_timeout_ticks=" << s.navTimeoutTicks
      << "\nkey_menu=" << s.keyMenu << "\nkey_next=" << s.keyNext << "\nkey_pos1=" << s.keyPos1
      << "\nkey_pos2=" << s.keyPos2 << "\nkey_start=" << s.keyStart << "\nkey_stop=" << s.keyStop << "\n";
    return o.str();
}

namespace {
std::string trim(std::string v) {
    const auto b = v.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = v.find_last_not_of(" \t\r");
    return v.substr(b, e - b + 1);
}
bool asBool(const std::string& v, bool def) {
    if (v == "1" || v == "true" || v == "on") return true;
    if (v == "0" || v == "false" || v == "off") return false;
    return def;
}
int asInt(const std::string& v, int def) {
    char* end = nullptr;
    const long x = std::strtol(v.c_str(), &end, 10);
    return end && end != v.c_str() ? static_cast<int>(x) : def;
}
float asFloat(const std::string& v, float def) {
    char* end = nullptr;
    const float x = std::strtof(v.c_str(), &end);
    return end && end != v.c_str() && std::isfinite(x) ? x : def;
}
}  // namespace

void parseInto(Settings& s, std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "template") s.templateIdx = asInt(v, s.templateIdx);
        else if (k == "length") s.length = asInt(v, s.length);
        else if (k == "tunnel_w") s.tunnelW = asInt(v, s.tunnelW);
        else if (k == "tunnel_h") s.tunnelH = asInt(v, s.tunnelH);
        else if (k == "stairs_down") s.stairsDown = asBool(v, s.stairsDown);
        else if (k == "show_outline") s.showOutline = asBool(v, s.showOutline);
        else if (k == "show_hud") s.showHud = asBool(v, s.showHud);
        else if (k == "fov") s.fov = asFloat(v, s.fov);
        else if (k == "avoid_liquids") s.avoidLiquids = asBool(v, s.avoidLiquids);
        else if (k == "reach") s.reach = asFloat(v, s.reach);
        else if (k == "turn_rate") s.turnRate = asFloat(v, s.turnRate);
        else if (k == "plan_node_cap") s.planNodeCap = asInt(v, s.planNodeCap);
        else if (k == "tick_budget_us") s.tickBudgetUs = asInt(v, s.tickBudgetUs);
        else if (k == "break_timeout_ticks") s.breakTimeoutTicks = asInt(v, s.breakTimeoutTicks);
        else if (k == "stuck_ticks") s.stuckTicks = asInt(v, s.stuckTicks);
        else if (k == "nav_timeout_ticks") s.navTimeoutTicks = asInt(v, s.navTimeoutTicks);
        else if (k == "key_menu" && !v.empty()) s.keyMenu = v;
        else if (k == "key_next" && !v.empty()) s.keyNext = v;
        else if (k == "key_pos1" && !v.empty()) s.keyPos1 = v;
        else if (k == "key_pos2" && !v.empty()) s.keyPos2 = v;
        else if (k == "key_start" && !v.empty()) s.keyStart = v;
        else if (k == "key_stop" && !v.empty()) s.keyStop = v;
    }
    sanitize(s);
}

Settings SettingsStore::get() const {
    std::lock_guard<std::mutex> l(m_);
    return s_;
}

void SettingsStore::update(const std::function<void(Settings&)>& fn) {
    {
        std::lock_guard<std::mutex> l(m_);
        fn(s_);
        sanitize(s_);
    }
    (void)save();
}

void SettingsStore::setPath(std::string path) {
    std::lock_guard<std::mutex> l(m_);
    path_ = std::move(path);
}

bool SettingsStore::load() {
    std::string path;
    {
        std::lock_guard<std::mutex> l(m_);
        path = path_;
    }
    if (path.empty()) return false;
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::lock_guard<std::mutex> l(m_);
    parseInto(s_, ss.str());
    return true;
}

bool SettingsStore::save() const {
    std::string path, text;
    {
        std::lock_guard<std::mutex> l(m_);
        path = path_;
        text = serialize(s_);
    }
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f.good()) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

void CommandQueue::push(Command c) {
    std::lock_guard<std::mutex> l(m_);
    if (q_.size() < 64) q_.push_back(c);
}

std::vector<Command> CommandQueue::drain() {
    std::lock_guard<std::mutex> l(m_);
    std::vector<Command> out;
    out.swap(q_);
    return out;
}

Status StatusBoard::get() const {
    std::lock_guard<std::mutex> l(m_);
    return s_;
}
void StatusBoard::set(Status s) {
    std::lock_guard<std::mutex> l(m_);
    s.bridgeMissing = s_.bridgeMissing;
    s_ = std::move(s);
}
void StatusBoard::setBridgeMissing(std::string m) {
    std::lock_guard<std::mutex> l(m_);
    s_.bridgeMissing = std::move(m);
}

}  // namespace am
