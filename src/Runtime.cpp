#include "Runtime.hpp"

#include <dlfcn.h>
#include <sys/stat.h>

#include <cstdlib>

namespace am {

Runtime& runtime() {
    static Runtime r;
    return r;
}

namespace {
bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty()) (void)::mkdir(cur.c_str(), 0700);
        }
        if (i < path.size()) cur += path[i];
    }
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string selfPath() {
    Dl_info info{};
    static int anchor = 0;
    if (::dladdr(&anchor, &info) != 0 && info.dli_fname && *info.dli_fname) return info.dli_fname;
    return {};
}
}  // namespace

std::string discoverConfigDir() {
    if (const char* o = std::getenv("AUTOMINER_DIR")) {
        if (*o && makeDirs(o)) return o;
    }
    const std::string self = selfPath();
    if (self.empty()) return {};
    // layout do LeviLauncher: <app>/cache/.../AutoMiner.so  ->  <app>/files/native_mod_configs/AutoMiner/
    const auto cache = self.find("/cache/");
    if (cache != std::string::npos) {
        const std::string dir = self.substr(0, cache) + "/files/native_mod_configs/AutoMiner";
        if (makeDirs(dir)) return dir;
    }
    // fallback: pasta ao lado do .so
    const auto slash = self.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string dir = self.substr(0, slash) + "/AutoMinerConfig";
        if (makeDirs(dir)) return dir;
    }
    return {};
}

}  // namespace am
