#include "GlRenderer.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <pl/memory/Hook.hpp>

#include "NativeBridge.hpp"
#include "Render.hpp"
#include "Runtime.hpp"

namespace am {
namespace {
// constantes GLES (sem depender de headers)
constexpr unsigned GL_ARRAY_BUFFER = 0x8892, GL_DYNAMIC_DRAW = 0x88E8, GL_FLOAT = 0x1406, GL_TRIANGLES = 4;
constexpr unsigned GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30, GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82;
constexpr unsigned GL_CURRENT_PROGRAM = 0x8B8D, GL_VERTEX_ARRAY_BINDING = 0x85B5, GL_ARRAY_BUFFER_BINDING = 0x8894;
constexpr unsigned GL_FRAMEBUFFER = 0x8D40, GL_FRAMEBUFFER_BINDING = 0x8CA6, GL_VIEWPORT = 0x0BA2;
constexpr unsigned GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71, GL_CULL_FACE = 0x0B44, GL_SCISSOR_TEST = 0x0C11, GL_STENCIL_TEST = 0x0B90;
constexpr unsigned GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_BLEND_SRC_RGB = 0x80C9, GL_BLEND_DST_RGB = 0x80C8;
constexpr unsigned GL_BLEND_SRC_ALPHA = 0x80CB, GL_BLEND_DST_ALPHA = 0x80CA;
constexpr int EGL_WIDTH = 0x3057, EGL_HEIGHT = 0x3056;

struct Gl {
    unsigned (*createShader)(unsigned);
    void (*shaderSource)(unsigned, int, const char* const*, const int*);
    void (*compileShader)(unsigned);
    void (*getShaderiv)(unsigned, unsigned, int*);
    unsigned (*createProgram)();
    void (*attachShader)(unsigned, unsigned);
    void (*linkProgram)(unsigned);
    void (*getProgramiv)(unsigned, unsigned, int*);
    void (*deleteShader)(unsigned);
    void (*useProgram)(unsigned);
    int (*getUniformLocation)(unsigned, const char*);
    void (*uniform2f)(int, float, float);
    void (*genBuffers)(int, unsigned*);
    void (*bindBuffer)(unsigned, unsigned);
    void (*bufferData)(unsigned, std::intptr_t, const void*, unsigned);
    void (*genVertexArrays)(int, unsigned*);
    void (*bindVertexArray)(unsigned);
    void (*enableVertexAttribArray)(unsigned);
    void (*vertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void*);
    void (*drawArrays)(unsigned, int, int);
    void (*enable)(unsigned);
    void (*disable)(unsigned);
    unsigned char (*isEnabled)(unsigned);
    void (*getIntegerv)(unsigned, int*);
    void (*blendFunc)(unsigned, unsigned);
    void (*blendFuncSeparate)(unsigned, unsigned, unsigned, unsigned);
    void (*bindFramebuffer)(unsigned, unsigned);
    void (*viewport)(int, int, int, int);
    void (*bindAttribLocation)(unsigned, unsigned, const char*);
};
Gl gl{};
bool glLoaded = false, glFailed = false;
unsigned prog = 0, vao = 0, vbo = 0;
int uView = -1;

using SwapFn = unsigned (*)(void*, void*);
using QueryFn = unsigned (*)(void*, void*, int, int*);
std::atomic<SwapFn> swapOrig{nullptr};
void* swapTarget = nullptr;
QueryFn eglQuery = nullptr;
Batch batch;

template <class T>
bool load(void* lib, T& fn, const char* name) {
    fn = reinterpret_cast<T>(::dlsym(lib, name));
    return fn != nullptr;
}

bool loadGl() {
    if (glLoaded) return true;
    if (glFailed) return false;
    void* lib = ::dlopen("libGLESv3.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) lib = ::dlopen("libGLESv3.so", RTLD_NOW);
    if (!lib) lib = ::dlopen("libGLESv2.so", RTLD_NOW);
    if (!lib) { glFailed = true; return false; }
    bool ok = load(lib, gl.createShader, "glCreateShader") && load(lib, gl.shaderSource, "glShaderSource") &&
              load(lib, gl.compileShader, "glCompileShader") && load(lib, gl.getShaderiv, "glGetShaderiv") &&
              load(lib, gl.createProgram, "glCreateProgram") && load(lib, gl.attachShader, "glAttachShader") &&
              load(lib, gl.linkProgram, "glLinkProgram") && load(lib, gl.getProgramiv, "glGetProgramiv") &&
              load(lib, gl.deleteShader, "glDeleteShader") && load(lib, gl.useProgram, "glUseProgram") &&
              load(lib, gl.getUniformLocation, "glGetUniformLocation") && load(lib, gl.uniform2f, "glUniform2f") &&
              load(lib, gl.genBuffers, "glGenBuffers") && load(lib, gl.bindBuffer, "glBindBuffer") &&
              load(lib, gl.bufferData, "glBufferData") && load(lib, gl.genVertexArrays, "glGenVertexArrays") &&
              load(lib, gl.bindVertexArray, "glBindVertexArray") && load(lib, gl.enableVertexAttribArray, "glEnableVertexAttribArray") &&
              load(lib, gl.vertexAttribPointer, "glVertexAttribPointer") && load(lib, gl.drawArrays, "glDrawArrays") &&
              load(lib, gl.enable, "glEnable") && load(lib, gl.disable, "glDisable") && load(lib, gl.isEnabled, "glIsEnabled") &&
              load(lib, gl.getIntegerv, "glGetIntegerv") && load(lib, gl.blendFunc, "glBlendFunc") &&
              load(lib, gl.blendFuncSeparate, "glBlendFuncSeparate") && load(lib, gl.bindFramebuffer, "glBindFramebuffer") &&
              load(lib, gl.viewport, "glViewport") && load(lib, gl.bindAttribLocation, "glBindAttribLocation");
    glLoaded = ok;
    glFailed = !ok;
    return ok;
}

unsigned compile(unsigned type, const char* src) {
    const unsigned s = gl.createShader(type);
    gl.shaderSource(s, 1, &src, nullptr);
    gl.compileShader(s);
    int ok = 0;
    gl.getShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { gl.deleteShader(s); return 0; }
    return s;
}

bool buildProgram() {
    if (prog) return true;
    static const char* vs =
        "#version 300 es\nlayout(location=0) in vec2 aPos; layout(location=1) in vec4 aCol; uniform vec2 uView; out vec4 vCol;\n"
        "void main(){ vec2 n = vec2(aPos.x/uView.x*2.0-1.0, 1.0-aPos.y/uView.y*2.0); gl_Position=vec4(n,0.0,1.0); vCol=aCol; }";
    static const char* fs = "#version 300 es\nprecision mediump float; in vec4 vCol; out vec4 o; void main(){ o=vCol; }";
    const unsigned v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { glFailed = true; return false; }
    const unsigned p = gl.createProgram();
    gl.attachShader(p, v);
    gl.attachShader(p, f);
    gl.linkProgram(p);
    int ok = 0;
    gl.getProgramiv(p, GL_LINK_STATUS, &ok);
    gl.deleteShader(v);
    gl.deleteShader(f);
    if (!ok) { glFailed = true; return false; }
    prog = p;
    uView = gl.getUniformLocation(prog, "uView");
    gl.genVertexArrays(1, &vao);
    gl.genBuffers(1, &vbo);
    return true;
}

void drawOverlay(void* display, void* surface) {
    if (!loadGl() || !buildProgram() || !eglQuery) return;
    int w = 0, h = 0;
    eglQuery(display, surface, EGL_WIDTH, &w);
    eglQuery(display, surface, EGL_HEIGHT, &h);
    if (w < 16 || h < 16) return;

    Runtime& rt = runtime();
    const Settings s = rt.settings.get();
    const Status st = rt.status.get();
    NativeBridge& nb = nativeBridge();

    batch.clear();
    Pose pose;
    if (nb.ready() && nb.pose(pose)) {
        if (auto ov = rt.controller.overlay()) {
            Camera cam;
            cam.eye = pose.eye();
            cam.yaw = pose.yaw;
            cam.pitch = pose.pitch;
            cam.fovYDeg = s.fov;
            cam.width = w;
            cam.height = h;
            buildOutline(batch, cam, *ov, s);
        }
    }
    Status shown = st;
    shown.bridgeMissing = nb.missing();
    buildHud(batch, w, h, shown, s);
    if (batch.v.empty()) return;

    // salva estado
    int oProg = 0, oVao = 0, oVbo = 0, oFbo = 0, oVp[4]{}, oSrcRgb = 0, oDstRgb = 0, oSrcA = 0, oDstA = 0;
    gl.getIntegerv(GL_CURRENT_PROGRAM, &oProg);
    gl.getIntegerv(GL_VERTEX_ARRAY_BINDING, &oVao);
    gl.getIntegerv(GL_ARRAY_BUFFER_BINDING, &oVbo);
    gl.getIntegerv(GL_FRAMEBUFFER_BINDING, &oFbo);
    gl.getIntegerv(GL_VIEWPORT, oVp);
    gl.getIntegerv(GL_BLEND_SRC_RGB, &oSrcRgb);
    gl.getIntegerv(GL_BLEND_DST_RGB, &oDstRgb);
    gl.getIntegerv(GL_BLEND_SRC_ALPHA, &oSrcA);
    gl.getIntegerv(GL_BLEND_DST_ALPHA, &oDstA);
    const bool bBlend = gl.isEnabled(GL_BLEND), bDepth = gl.isEnabled(GL_DEPTH_TEST), bCull = gl.isEnabled(GL_CULL_FACE),
               bScissor = gl.isEnabled(GL_SCISSOR_TEST), bStencil = gl.isEnabled(GL_STENCIL_TEST);

    gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.viewport(0, 0, w, h);
    gl.disable(GL_DEPTH_TEST);  // outline visivel atraves das paredes
    gl.disable(GL_CULL_FACE);
    gl.disable(GL_SCISSOR_TEST);
    gl.disable(GL_STENCIL_TEST);
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl.useProgram(prog);
    gl.uniform2f(uView, static_cast<float>(w), static_cast<float>(h));
    gl.bindVertexArray(vao);
    gl.bindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.bufferData(GL_ARRAY_BUFFER, static_cast<std::intptr_t>(batch.v.size() * sizeof(Vertex)), batch.v.data(), GL_DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(Vertex), nullptr);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(2 * sizeof(float)));
    gl.drawArrays(GL_TRIANGLES, 0, static_cast<int>(batch.v.size()));

    // restaura
    gl.bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(oVbo));
    gl.bindVertexArray(static_cast<unsigned>(oVao));
    gl.useProgram(static_cast<unsigned>(oProg));
    gl.bindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(oFbo));
    gl.viewport(oVp[0], oVp[1], oVp[2], oVp[3]);
    gl.blendFuncSeparate(static_cast<unsigned>(oSrcRgb), static_cast<unsigned>(oDstRgb), static_cast<unsigned>(oSrcA), static_cast<unsigned>(oDstA));
    auto set = [](unsigned cap, bool on) { on ? gl.enable(cap) : gl.disable(cap); };
    set(GL_BLEND, bBlend); set(GL_DEPTH_TEST, bDepth); set(GL_CULL_FACE, bCull);
    set(GL_SCISSOR_TEST, bScissor); set(GL_STENCIL_TEST, bStencil);
}

unsigned swapDetour(void* display, void* surface) {
    try {
        drawOverlay(display, surface);
    } catch (...) {
    }
    const SwapFn o = swapOrig.load();
    return o ? o(display, surface) : 0;
}
}  // namespace

bool installGlOverlay() {
    if (swapOrig.load()) return true;
    void* swap = ::dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    eglQuery = reinterpret_cast<QueryFn>(::dlsym(RTLD_DEFAULT, "eglQuerySurface"));
    if (!swap || !eglQuery) return false;
    void* original = nullptr;
    if (pl::memory::hook(swap, reinterpret_cast<void*>(&swapDetour), &original) != 0 || !original) return false;
    swapTarget = swap;
    swapOrig.store(reinterpret_cast<SwapFn>(original));
    return true;
}

void removeGlOverlay() {
    if (!swapOrig.load()) return;
    (void)pl::memory::unhook(swapTarget, reinterpret_cast<void*>(&swapDetour));
    // swapOrig fica setado: um frame em voo ainda pode chamar o detour
}
}  // namespace am
