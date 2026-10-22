#include "GlHooks.hpp"

#include <pl/memory/Hook.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace buffers {
namespace {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;

constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6, GL_READ_FRAMEBUFFER_BINDING = 0x8CAA, GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_RENDERBUFFER_BINDING = 0x8CA7, GL_RENDERBUFFER_WIDTH = 0x8D42, GL_RENDERBUFFER_HEIGHT = 0x8D43;
constexpr GLenum GL_RENDERBUFFER_INTERNAL_FORMAT = 0x8D44, GL_RENDERBUFFER_SAMPLES = 0x8CAB;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0, GL_ACTIVE_TEXTURE = 0x84E0, GL_TEXTURE_BINDING_2D = 0x8069;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLint GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE_WIDTH = 0x1000, GL_TEXTURE_HEIGHT = 0x1001, GL_TEXTURE_INTERNAL_FORMAT = 0x1003;
constexpr GLenum GL_TEXTURE = 0x1702;
constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE = 0x8CD0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME = 0x8CD1,
                 GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL = 0x8CD2;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
constexpr GLenum GL_DEPTH = 0x1801;
constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE = 0x8216, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE = 0x8217;
constexpr GLenum GL_DEPTH_COMPONENT16 = 0x81A5, GL_DEPTH_COMPONENT24 = 0x81A6, GL_DEPTH_COMPONENT32F = 0x8CAC,
                 GL_DEPTH24_STENCIL8 = 0x88F0, GL_DEPTH32F_STENCIL8 = 0x8CAD;
constexpr GLenum GL_TEXTURE_COMPARE_MODE = 0x884C, GL_NONE = 0;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11, GL_RASTERIZER_DISCARD = 0x8C89, GL_VIEWPORT = 0x0BA2, GL_CURRENT_PROGRAM = 0x8B8D;
constexpr GLenum GL_RGBA8 = 0x8058, GL_RGB8 = 0x8051, GL_SRGB8_ALPHA8 = 0x8C43, GL_RGBA16F = 0x881A, GL_R11F_G11F_B10F = 0x8C3A,
                 GL_RGB10_A2 = 0x8059, GL_RGB565 = 0x8D62, GL_RGBA4 = 0x8056, GL_RGB5_A1 = 0x8057;
constexpr GLenum GL_BACK = 0x0405, GL_SAMPLES = 0x80A9;
constexpr GLenum GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_READ_BUFFER = 0x0C02, GL_COLOR_ATTACHMENT0_ENUM = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE = 0x8CD3, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER = 0x8CD4;
constexpr int kEglDraw = 0x3059;
constexpr int kTrackedUnits = 32;
constexpr unsigned kRevalidateFrames = 60;
constexpr unsigned kVerifyMask = 1023;
constexpr GLenum GL_ATTACHMENT_RED_SIZE = 0x8212, GL_ATTACHMENT_ALPHA_SIZE = 0x8215;

struct Gl {
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*DrawElementsInstanced)(GLenum, GLsizei, GLenum, const void*, GLsizei);
    void (*DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    void (*ActiveTexture)(GLenum);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    GLenum (*CheckFramebufferStatus)(GLenum);
    GLint (*GetUniformLocation)(GLuint, const char*);
    void (*GetUniformiv)(GLuint, GLint, GLint*);
    void (*GetIntegerv)(GLenum, GLint*);
    GLboolean (*IsEnabled)(GLenum);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    GLenum (*GetError)();
    void (*GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*);
    void (*GetRenderbufferParameteriv)(GLenum, GLenum, GLint*);
    void (*BindRenderbuffer)(GLenum, GLuint);
    void (*GetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint*);
    void (*UseProgram)(GLuint);
    void (*LinkProgram)(GLuint);
    void (*DeleteProgram)(GLuint);
    void (*ProgramBinary)(GLuint, GLenum, const void*, GLsizei);
    void (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void (*DeleteRenderbuffers)(GLsizei, const GLuint*);
    void (*FramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
    void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void (*RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    void (*ReadBuffer)(GLenum);
};
Gl real{};

enum Slot {
    kDrawElements, kDrawArrays, kDrawElementsInstanced, kDrawArraysInstanced,
    kUseProgram, kActiveTexture, kBindTexture, kLinkProgram, kDeleteProgram, kProgramBinary,
    kDeleteFramebuffers, kDeleteRenderbuffers, kDeleteTextures, kFramebufferTexture2D, kFramebufferTextureLayer,
    kFramebufferRenderbuffer, kTexStorage2D, kTexImage2D, kRenderbufferStorage, kRenderbufferStorageMultisample,
    kSlotCount
};
std::atomic<void*> gOrig[kSlotCount];
std::atomic<void*> gSwapOrig{nullptr};
std::atomic<void*> gEglOrig{nullptr};
std::atomic_bool gActive{false};
void* (*gGetCurrentContext)() = nullptr;
void* (*gGetCurrentSurface)(int) = nullptr;

template <class F>
F resolve(Slot slot, F fallback) {
    void* p = gOrig[slot].load(std::memory_order_acquire);
    return p ? reinterpret_cast<F>(p) : fallback;
}

struct Attach {
    GLuint object = 0;
    bool texture = false;
};
enum Kind { kScreen, kDepth, kKinds };
constexpr const char* kSamplerName[kKinds] = {"s_ScreenBuffer", "s_SceneDepthBuffer"};

struct Own {
    GLuint tex = 0;
    GLuint fbo = 0;
    GLenum format = 0;
    int width = 0, height = 0;
    bool valid = false;
    bool failed = false;
};
struct Units {
    GLint unit[kKinds] = {-1, -1};
};
struct Probe {
    bool known = false, ok = false;
    GLuint fbo = 0;
    void* surface = nullptr;
    unsigned epoch = 0, frame = 0;
    int width = 0, height = 0, samples = 0;
    GLenum format = 0;
};
struct Track {
    GLint program = -1;
    GLint active = -1;
    GLint bound2d[kTrackedUnits];
    bool bad = false;
    Track() { std::fill(std::begin(bound2d), std::end(bound2d), -1); }
};
struct State {
    void* ctx = nullptr;
    unsigned frame = 0;
    unsigned epoch = 0;
    unsigned long long draws = 0;
    Track tr;
    Probe probe[kKinds];
    unsigned copyFrame[kKinds] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    unsigned failedFrames[kKinds] = {}, retries[kKinds] = {};
    std::unordered_map<GLuint, Units> unitsOf;
    Own own[kKinds];
    GLuint fallback[kKinds] = {0, 0};
    GLuint lastProgram = 0;
    const Units* lastUnits = nullptr;
};
State& state() {
    static thread_local State s;
    return s;
}

constexpr int kScratchUnit = 15;

bool queryAttachment(const Attach& a, int& w, int& h, GLenum& format, int& samples) {
    GLint iw = 0, ih = 0, fmt = 0, ms = 0;
    if (a.texture) {
        real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
        real.BindTexture(GL_TEXTURE_2D, a.object);
        real.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iw);
        real.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &ih);
        real.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
    } else {
        real.BindRenderbuffer(GL_RENDERBUFFER, a.object);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &iw);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &ih);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &fmt);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &ms);
    }
    samples = ms;
    w = iw;
    h = ih;
    format = static_cast<GLenum>(fmt);
    return iw > 0 && ih > 0 && fmt != 0;
}

bool currentAttachment(GLenum attachment, Attach& out) {
    GLint type = 0, name = 0, level = 0;
    real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != static_cast<GLint>(GL_TEXTURE) && type != static_cast<GLint>(GL_RENDERBUFFER)) return false;
    real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    const bool isTexture = type == static_cast<GLint>(GL_TEXTURE);
    if (isTexture) {
        GLint layer = 0, face = 0;
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &layer);
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE, &face);
        if (level != 0 || layer != 0 || face != 0) return false;
    }
    out = {static_cast<GLuint>(name), isTexture};
    return name != 0;
}

struct Saved {
    GLint drawFbo, readFbo, renderbuffer, activeTexture, scratchTexture;
    GLint viewport[4];
    GLboolean scissor, discard;
};
void save(Saved& s) {
    real.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.drawFbo);
    real.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.readFbo);
    real.GetIntegerv(GL_RENDERBUFFER_BINDING, &s.renderbuffer);
    real.GetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexture);
    real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
    real.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.scratchTexture);
    real.GetIntegerv(GL_VIEWPORT, s.viewport);
    s.scissor = real.IsEnabled(GL_SCISSOR_TEST);
    s.discard = real.IsEnabled(GL_RASTERIZER_DISCARD);
}
void setEnabled(GLenum cap, GLboolean on) {
    if (on) real.Enable(cap);
    else real.Disable(cap);
}
void restore(const Saved& s) {
    real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
    real.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.scratchTexture));
    real.ActiveTexture(static_cast<GLenum>(s.activeTexture));
    real.BindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(s.renderbuffer));
    real.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(s.readFbo));
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s.drawFbo));
    setEnabled(GL_SCISSOR_TEST, s.scissor);
    setEnabled(GL_RASTERIZER_DISCARD, s.discard);
}

bool screenFormatOk(GLenum f) {
    return f == GL_RGBA8 || f == GL_RGB8 || f == GL_SRGB8_ALPHA8 || f == GL_RGBA16F || f == GL_R11F_G11F_B10F || f == GL_RGB10_A2 ||
           f == GL_RGB565 || f == GL_RGBA4 || f == GL_RGB5_A1;
}
bool depthFormatOk(GLenum f) {
    return f == GL_DEPTH_COMPONENT16 || f == GL_DEPTH_COMPONENT24 || f == GL_DEPTH_COMPONENT32F || f == GL_DEPTH24_STENCIL8 ||
           f == GL_DEPTH32F_STENCIL8;
}
bool hasStencil(GLenum f) { return f == GL_DEPTH24_STENCIL8 || f == GL_DEPTH32F_STENCIL8; }

bool ensureTexture(Own& own, Kind kind, int w, int h, GLenum format) {
    if (own.tex && own.width == w && own.height == h && own.format == format) return true;
    if (own.tex) real.DeleteTextures(1, &own.tex);
    own.tex = 0;
    if (!own.fbo) real.GenFramebuffers(1, &own.fbo);

    real.GenTextures(1, &own.tex);
    real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
    real.BindTexture(GL_TEXTURE_2D, own.tex);
    real.TexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
    const GLint filter = kind == kDepth ? GL_NEAREST : GL_LINEAR;
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (kind == kDepth) real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.fbo);
    if (kind == kDepth) {
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, hasStencil(format) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, own.tex, 0);
    } else {
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, own.tex, 0);
    }
    const GLenum status = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }
    own.width = w;
    own.height = h;
    own.format = format;
    return true;
}

GLuint fallbackTexture(State& st, Kind kind) {
    GLuint& tex = st.fallback[kind];
    if (tex) return tex;
    Saved saved{};
    save(saved);
    while (real.GetError() != 0) {}
    GLuint t = 0;
    real.GenTextures(1, &t);
    real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
    real.BindTexture(GL_TEXTURE_2D, t);
    const unsigned char white[4] = {255, 255, 255, 255};
    real.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    restore(saved);
    if (real.GetError() != 0) {
        while (real.GetError() != 0) {}
        if (t) real.DeleteTextures(1, &t);
        return 0;
    }
    tex = t;
    return tex;
}

void skip(State& st, Kind kind) {
    st.own[kind].valid = false;
}

void finishCapture(State& st, Kind kind, Saved& saved, GLuint sceneFbo, int vx, int vy, int vw, int vh, int w, int h, GLenum format,
                   int samples, bool known) {
    Own& own = st.own[kind];
    if (real.GetError() != 0) known = false;
    if (vx < 0) { vw += vx; vx = 0; }
    if (vy < 0) { vh += vy; vy = 0; }
    vw = std::min(vw, w - vx);
    vh = std::min(vh, h - vy);

    if (!known) {
        restore(saved);
        skip(st, kind);
        return;
    }
    const bool formatOk = kind == kDepth ? depthFormatOk(format) : screenFormatOk(format);
    if (!formatOk || vw < 16 || vh < 16) {
        restore(saved);
        skip(st, kind);
        return;
    }
    int dw = vw, dh = vh;
    GLint filter = GL_NEAREST;
    if (kind == kScreen && samples == 0) {
        dw = (vw + 1) / 2;
        dh = (vh + 1) / 2;
        filter = GL_LINEAR;
    }
    if (!ensureTexture(own, kind, dw, dh, format)) {
        restore(saved);
        own.failed = true;
        return;
    }

    real.Disable(GL_SCISSOR_TEST);
    real.Disable(GL_RASTERIZER_DISCARD);
    real.BindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.fbo);
    GLint prevRead = static_cast<GLint>(GL_COLOR_ATTACHMENT0_ENUM);
    bool forcedRead = false;
    if (kind == kScreen && sceneFbo != 0 && real.ReadBuffer) {
        real.GetIntegerv(GL_READ_BUFFER, &prevRead);
        if (prevRead != static_cast<GLint>(GL_COLOR_ATTACHMENT0_ENUM)) {
            real.ReadBuffer(GL_COLOR_ATTACHMENT0_ENUM);
            forcedRead = true;
        }
    }
    real.BlitFramebuffer(vx, vy, vx + vw, vy + vh, 0, 0, dw, dh, kind == kDepth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT, static_cast<GLenum>(filter));
    if (forcedRead) real.ReadBuffer(static_cast<GLenum>(prevRead));
    restore(saved);

    const GLenum error = real.GetError();
    while (real.GetError() != 0) {}
    if (error != 0) {
        own.failed = true;
        return;
    }
    own.valid = true;
}

bool measureScene(State& st, Kind kind, GLuint fbo, void* surface, Probe& pr) {
    int w = 0, h = 0, samples = 0;
    GLenum format = 0;
    if (fbo == 0) {
        if (!surface) return false;
        GLint red = 0, alpha = 0, depthBits = 0, stencilBits = 0, ms = 0;
        real.GetIntegerv(GL_SAMPLES, &ms);
        samples = ms;
        if (kind == kScreen) {
            real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK, GL_ATTACHMENT_RED_SIZE, &red);
            real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK, GL_ATTACHMENT_ALPHA_SIZE, &alpha);
            format = red == 8 ? (alpha > 0 ? GL_RGBA8 : GL_RGB8) : (red == 10 ? GL_RGB10_A2 : 0);
        } else {
            real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthBits);
            real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencilBits);
            if (depthBits >= 24) format = stencilBits > 0 ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
            else if (depthBits >= 16) format = GL_DEPTH_COMPONENT16;
        }
        w = h = 16384;
    } else {
        Attach attach;
        if (!currentAttachment(kind == kDepth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0, attach)) return false;
        if (!queryAttachment(attach, w, h, format, samples)) return false;
        if (real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    }
    if (real.GetError() != 0) return false;

    real.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    GLint readBuffer = 0;
    real.GetIntegerv(GL_READ_BUFFER, &readBuffer);
    if (real.GetError() != 0) return false;
    if (kind == kScreen ? (fbo == 0 && readBuffer != static_cast<GLint>(GL_BACK)) : readBuffer == static_cast<GLint>(GL_NONE))
        return false;

    pr.width = w;
    pr.height = h;
    pr.samples = samples;
    pr.format = format;
    if (kind == kDepth && samples != 0) return false;
    (void)st;
    return kind == kDepth ? depthFormatOk(format) : screenFormatOk(format);
}

bool probeScene(State& st, Kind kind, GLuint fbo, void* surface) {
    Probe& pr = st.probe[kind];
    if (pr.known && pr.fbo == fbo && pr.surface == surface && pr.epoch == st.epoch && st.frame - pr.frame < kRevalidateFrames) return pr.ok;
    pr = Probe{};
    pr.known = true;
    pr.fbo = fbo;
    pr.surface = surface;
    pr.epoch = st.epoch;
    pr.frame = st.frame;
    pr.ok = measureScene(st, kind, fbo, surface, pr);
    while (real.GetError() != 0) {}
    return pr.ok;
}

void capture(State& st, Kind kind) {
    st.own[kind].valid = false;
    while (real.GetError() != 0) {}

    Saved saved{};
    save(saved);
    const GLuint sceneFbo = static_cast<GLuint>(saved.drawFbo);
    void* surface = nullptr;
    if (sceneFbo == 0) surface = gGetCurrentSurface ? gGetCurrentSurface(kEglDraw) : reinterpret_cast<void*>(1);

    const int vx = saved.viewport[0], vy = saved.viewport[1], vw = saved.viewport[2], vh = saved.viewport[3];
    if (!probeScene(st, kind, sceneFbo, surface)) {
        restore(saved);
        skip(st, kind);
        return;
    }
    const Probe& pr = st.probe[kind];
    finishCapture(st, kind, saved, sceneFbo, vx, vy, vw, vh, pr.width, pr.height, pr.format, pr.samples, true);
}

struct Bind {
    int unit[kKinds] = {-1, -1};
    GLint prevTexture[kKinds] = {0, 0};
    GLint prevActive = 0;
    GLint current = 0;
};

GLint lookupUnit(GLuint program, Kind kind) {
    GLint unit = -1;
    const GLint loc = real.GetUniformLocation(program, kSamplerName[kind]);
    if (loc >= 0) {
        real.GetUniformiv(program, loc, &unit);
        if (unit < 0 || unit >= kTrackedUnits) unit = -1;
    }
    return unit;
}

GLint trackedActive(State& st) {
    if (!st.tr.bad && st.tr.active >= 0) return st.tr.active;
    GLint v = 0;
    real.GetIntegerv(GL_ACTIVE_TEXTURE, &v);
    const GLint unit = v - static_cast<GLint>(GL_TEXTURE0);
    if (!st.tr.bad) st.tr.active = unit;
    return unit;
}

GLint trackedTexture(State& st, GLint unit) {
    if (!st.tr.bad && st.tr.bound2d[unit] >= 0) return st.tr.bound2d[unit];
    const GLint active = trackedActive(st);
    real.ActiveTexture(GL_TEXTURE0 + unit);
    GLint v = 0;
    real.GetIntegerv(GL_TEXTURE_BINDING_2D, &v);
    real.ActiveTexture(GL_TEXTURE0 + active);
    if (!st.tr.bad) st.tr.bound2d[unit] = v;
    return v;
}

const Units* unitsFor(State& st, GLuint program) {
    if (st.lastUnits && st.lastProgram == program) return st.lastUnits;
    auto known = st.unitsOf.find(program);
    if (known == st.unitsOf.end()) {
        Units u;
        for (int k = 0; k < kKinds; ++k) u.unit[k] = lookupUnit(program, static_cast<Kind>(k));
        known = st.unitsOf.emplace(program, u).first;
    }
    st.lastProgram = program;
    st.lastUnits = &known->second;
    return st.lastUnits;
}

Bind bindBuffers(State& st, GLint program) {
    Bind bind;
    if (program <= 0) return bind;

    const Units* units = unitsFor(st, static_cast<GLuint>(program));
    if (units->unit[0] < 0 && units->unit[1] < 0) return bind;

    bool havePrevActive = false;
    for (int k = 0; k < kKinds; ++k) {
        const GLint unit = units->unit[k];
        Own& own = st.own[k];
        if (unit < 0) continue;

        if (!own.failed && st.copyFrame[k] != st.frame) {
            st.copyFrame[k] = st.frame;
            capture(st, static_cast<Kind>(k));
        }
        const GLuint source = (!own.failed && own.valid && own.tex) ? own.tex : fallbackTexture(st, static_cast<Kind>(k));
        if (!source) continue;

        if (!havePrevActive) {
            bind.prevActive = bind.current = trackedActive(st);
            havePrevActive = true;
        }
        bind.prevTexture[k] = trackedTexture(st, unit);
        if (bind.current != unit) {
            real.ActiveTexture(GL_TEXTURE0 + unit);
            bind.current = unit;
        }
        real.BindTexture(GL_TEXTURE_2D, source);
        bind.unit[k] = unit;
    }
    return bind;
}

void unbindBuffers(const Bind& bind) {
    if (bind.unit[kScreen] < 0 && bind.unit[kDepth] < 0) return;
    GLint current = bind.current;
    for (int k = kKinds - 1; k >= 0; --k) {
        if (bind.unit[k] < 0) continue;
        if (current != bind.unit[k]) {
            real.ActiveTexture(GL_TEXTURE0 + bind.unit[k]);
            current = bind.unit[k];
        }
        real.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bind.prevTexture[k]));
    }
    if (current != bind.prevActive) real.ActiveTexture(GL_TEXTURE0 + bind.prevActive);
}

void onContextChanged(State& st, void* ctx) {
    st.ctx = ctx;
    st.unitsOf.clear();
    st.lastProgram = 0;
    st.lastUnits = nullptr;
    const bool bad = st.tr.bad;
    st.tr = Track{};
    st.tr.bad = bad;
    ++st.epoch;
    for (int k = 0; k < kKinds; ++k) {
        st.own[k] = Own{};
        st.fallback[k] = 0;
        st.probe[k] = Probe{};
        st.copyFrame[k] = 0xFFFFFFFFu;
        st.failedFrames[k] = st.retries[k] = 0;
    }
}

bool hasContext(State& st) {
    void* ctx = gGetCurrentContext ? gGetCurrentContext() : nullptr;
    if (!ctx) return false;
    if (ctx != st.ctx) onContextChanged(st, ctx);
    return true;
}

void verifyTracking(State& st) {
    Track& t = st.tr;
    if (t.bad) return;
    GLint v = 0;
    const char* wrong = nullptr;
    if (t.program >= 0 && (real.GetIntegerv(GL_CURRENT_PROGRAM, &v), v != t.program)) wrong = "program";
    else if (t.active >= 0 && (real.GetIntegerv(GL_ACTIVE_TEXTURE, &v), v - static_cast<GLint>(GL_TEXTURE0) != t.active)) wrong = "active texture";
    else if (t.active >= 0 && t.active < kTrackedUnits && t.bound2d[t.active] >= 0 &&
             (real.GetIntegerv(GL_TEXTURE_BINDING_2D, &v), v != t.bound2d[t.active])) wrong = "texture";
    if (wrong) {
        t = Track{};
        t.bad = true;
        st.unitsOf.clear();
        st.lastProgram = 0;
        st.lastUnits = nullptr;
    }
}

Bind onDraw() {
    if (!gActive.load(std::memory_order_relaxed)) return {};
    State& st = state();
    if (!hasContext(st)) return {};
    if ((++st.draws & kVerifyMask) == 0) verifyTracking(st);
    GLint program = st.tr.bad ? -1 : st.tr.program;
    if (program < 0) {
        real.GetIntegerv(GL_CURRENT_PROGRAM, &program);
        if (!st.tr.bad) st.tr.program = program;
    }
    return bindBuffers(st, program);
}

void drawElementsDetour(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    const Bind bind = onDraw();
    resolve(kDrawElements, real.DrawElements)(mode, count, type, indices);
    unbindBuffers(bind);
}
void drawArraysDetour(GLenum mode, GLint first, GLsizei count) {
    const Bind bind = onDraw();
    resolve(kDrawArrays, real.DrawArrays)(mode, first, count);
    unbindBuffers(bind);
}
void drawElementsInstancedDetour(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances) {
    const Bind bind = onDraw();
    resolve(kDrawElementsInstanced, real.DrawElementsInstanced)(mode, count, type, indices, instances);
    unbindBuffers(bind);
}
void drawArraysInstancedDetour(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    const Bind bind = onDraw();
    resolve(kDrawArraysInstanced, real.DrawArraysInstanced)(mode, first, count, instances);
    unbindBuffers(bind);
}

void useProgramDetour(GLuint program) {
    state().tr.program = static_cast<GLint>(program);
    if (auto f = resolve(kUseProgram, real.UseProgram)) f(program);
}
void activeTextureDetour(GLenum texture) {
    state().tr.active = static_cast<GLint>(texture) - static_cast<GLint>(GL_TEXTURE0);
    if (auto f = resolve(kActiveTexture, real.ActiveTexture)) f(texture);
}
void bindTextureDetour(GLenum target, GLuint texture) {
    if (target == GL_TEXTURE_2D) {
        Track& t = state().tr;
        if (t.active >= 0 && t.active < kTrackedUnits) t.bound2d[t.active] = static_cast<GLint>(texture);
    }
    if (auto f = resolve(kBindTexture, real.BindTexture)) f(target, texture);
}
void forgetProgram(GLuint program) {
    State& st = state();
    st.unitsOf.erase(program);
    if (st.lastProgram == program) {
        st.lastProgram = 0;
        st.lastUnits = nullptr;
    }
}
void linkProgramDetour(GLuint program) {
    forgetProgram(program);
    if (auto f = resolve(kLinkProgram, real.LinkProgram)) f(program);
}
void deleteProgramDetour(GLuint program) {
    forgetProgram(program);
    if (auto f = resolve(kDeleteProgram, real.DeleteProgram)) f(program);
}
void programBinaryDetour(GLuint program, GLenum format, const void* binary, GLsizei length) {
    forgetProgram(program);
    if (auto f = resolve(kProgramBinary, real.ProgramBinary)) f(program, format, binary, length);
}
void deleteFramebuffersDetour(GLsizei n, const GLuint* framebuffers) {
    ++state().epoch;
    if (auto f = resolve(kDeleteFramebuffers, real.DeleteFramebuffers)) f(n, framebuffers);
}
void deleteRenderbuffersDetour(GLsizei n, const GLuint* renderbuffers) {
    ++state().epoch;
    if (auto f = resolve(kDeleteRenderbuffers, real.DeleteRenderbuffers)) f(n, renderbuffers);
}
void deleteTexturesDetour(GLsizei n, const GLuint* textures) {
    State& st = state();
    ++st.epoch;
    if (textures && n > 0) {
        for (GLsizei i = 0; i < n; ++i)
            for (GLint& bound : st.tr.bound2d)
                if (bound == static_cast<GLint>(textures[i])) bound = 0;
    }
    if (auto f = resolve(kDeleteTextures, real.DeleteTextures)) f(n, textures);
}
void framebufferTexture2DDetour(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    ++state().epoch;
    if (auto f = resolve(kFramebufferTexture2D, real.FramebufferTexture2D)) f(target, attachment, textarget, texture, level);
}
void framebufferTextureLayerDetour(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    ++state().epoch;
    if (auto f = resolve(kFramebufferTextureLayer, real.FramebufferTextureLayer)) f(target, attachment, texture, level, layer);
}
void framebufferRenderbufferDetour(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    ++state().epoch;
    if (auto f = resolve(kFramebufferRenderbuffer, real.FramebufferRenderbuffer)) f(target, attachment, renderbuffertarget, renderbuffer);
}
void texStorage2DDetour(GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height) {
    ++state().epoch;
    if (auto f = resolve(kTexStorage2D, real.TexStorage2D)) f(target, levels, format, width, height);
}
void texImage2DDetour(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels) {
    if (level == 0) ++state().epoch;
    if (auto f = resolve(kTexImage2D, real.TexImage2D)) f(target, level, internalformat, width, height, border, format, type, pixels);
}
void renderbufferStorageDetour(GLenum target, GLenum format, GLsizei width, GLsizei height) {
    ++state().epoch;
    if (auto f = resolve(kRenderbufferStorage, real.RenderbufferStorage)) f(target, format, width, height);
}
void renderbufferStorageMultisampleDetour(GLenum target, GLsizei samples, GLenum format, GLsizei width, GLsizei height) {
    ++state().epoch;
    if (auto f = resolve(kRenderbufferStorageMultisample, real.RenderbufferStorageMultisample)) f(target, samples, format, width, height);
}

void endFrame(State& st) {
    for (int k = 0; k < kKinds; ++k) {
        if (st.own[k].failed) {
            if (++st.failedFrames[k] >= 300 && st.retries[k] < 20) {
                ++st.retries[k];
                st.failedFrames[k] = 0;
                st.own[k].failed = false;
            }
        } else {
            st.failedFrames[k] = 0;
        }
    }
    ++st.frame;
}

using SwapFn = unsigned (*)(void*, void*);
unsigned eglSwapBuffersDetour(void* display, void* surface) {
    State& st = state();
    if (gActive.load(std::memory_order_relaxed) && hasContext(st)) endFrame(st);
    auto original = reinterpret_cast<SwapFn>(gSwapOrig.load(std::memory_order_acquire));
    return original ? original(display, surface) : 0;
}

struct HookName {
    const char* name;
    void* detour;
};
const HookName kHooks[kSlotCount] = {
    {"glDrawElements", reinterpret_cast<void*>(&drawElementsDetour)},
    {"glDrawArrays", reinterpret_cast<void*>(&drawArraysDetour)},
    {"glDrawElementsInstanced", reinterpret_cast<void*>(&drawElementsInstancedDetour)},
    {"glDrawArraysInstanced", reinterpret_cast<void*>(&drawArraysInstancedDetour)},
    {"glUseProgram", reinterpret_cast<void*>(&useProgramDetour)},
    {"glActiveTexture", reinterpret_cast<void*>(&activeTextureDetour)},
    {"glBindTexture", reinterpret_cast<void*>(&bindTextureDetour)},
    {"glLinkProgram", reinterpret_cast<void*>(&linkProgramDetour)},
    {"glDeleteProgram", reinterpret_cast<void*>(&deleteProgramDetour)},
    {"glProgramBinary", reinterpret_cast<void*>(&programBinaryDetour)},
    {"glDeleteFramebuffers", reinterpret_cast<void*>(&deleteFramebuffersDetour)},
    {"glDeleteRenderbuffers", reinterpret_cast<void*>(&deleteRenderbuffersDetour)},
    {"glDeleteTextures", reinterpret_cast<void*>(&deleteTexturesDetour)},
    {"glFramebufferTexture2D", reinterpret_cast<void*>(&framebufferTexture2DDetour)},
    {"glFramebufferTextureLayer", reinterpret_cast<void*>(&framebufferTextureLayerDetour)},
    {"glFramebufferRenderbuffer", reinterpret_cast<void*>(&framebufferRenderbufferDetour)},
    {"glTexStorage2D", reinterpret_cast<void*>(&texStorage2DDetour)},
    {"glTexImage2D", reinterpret_cast<void*>(&texImage2DDetour)},
    {"glRenderbufferStorage", reinterpret_cast<void*>(&renderbufferStorageDetour)},
    {"glRenderbufferStorageMultisample", reinterpret_cast<void*>(&renderbufferStorageMultisampleDetour)},
};

using EglGetProcAddressFn = void* (*)(const char*);
void* eglGetProcAddressDetour(const char* name) {
    auto original = reinterpret_cast<EglGetProcAddressFn>(gEglOrig.load(std::memory_order_acquire));
    if (!original) return nullptr;
    void* address = original(name);
    if (!address || !name) return address;
    for (int i = 0; i < kSlotCount; ++i) {
        if (std::strcmp(name, kHooks[i].name) != 0) continue;
        void* expected = nullptr;
        gOrig[i].compare_exchange_strong(expected, address);
        return kHooks[i].detour;
    }
    if (!std::strcmp(name, "eglSwapBuffers")) {
        void* expected = nullptr;
        gSwapOrig.compare_exchange_strong(expected, address);
        return reinterpret_cast<void*>(&eglSwapBuffersDetour);
    }
    return address;
}

struct SlotPatch {
    void** slot;
    void* original;
    void* detour;
};
std::vector<SlotPatch> gPatches;
void* gInlineTarget = nullptr;
void* gSwapInlineTarget = nullptr;

int protectionAt(std::uintptr_t address) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return PROT_READ;
    char line[4096];
    int prot = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3) continue;
        if (address >= start && address < end) {
            if (perms[0] == 'r') prot |= PROT_READ;
            if (perms[1] == 'w') prot |= PROT_WRITE;
            if (perms[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    std::fclose(f);
    return prot ? prot : PROT_READ;
}

bool writeSlot(void** slot, void* value) {
    const std::uintptr_t page = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(slot) & ~(page - 1);
    const int oldProtection = protectionAt(reinterpret_cast<std::uintptr_t>(slot));
    if (::mprotect(reinterpret_cast<void*>(start), page, PROT_READ | PROT_WRITE) != 0) return false;
    __atomic_store_n(slot, value, __ATOMIC_RELEASE);
    ::mprotect(reinterpret_cast<void*>(start), page, oldProtection);
    return true;
}

std::uintptr_t findModuleBase(const char* soName) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[4096];
    std::uintptr_t best = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0, offset = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s %llx", &start, &end, perms, &offset) != 4) continue;
        char* path = std::strchr(line, '/');
        if (!path) continue;
        char* newline = std::strpbrk(path, "\r\n");
        if (newline) *newline = '\0';
        const char* slash = std::strrchr(path, '/');
        if (!slash || std::strcmp(slash + 1, soName) != 0) continue;
        if (offset == 0 && (best == 0 || start < best)) best = static_cast<std::uintptr_t>(start);
    }
    std::fclose(f);
    return best;
}

#if defined(__aarch64__)
constexpr unsigned kRelGlobDat = 1025;
constexpr unsigned kRelJumpSlot = 1026;
constexpr unsigned kMachine = EM_AARCH64;
#elif defined(__x86_64__)
constexpr unsigned kRelGlobDat = 6;
constexpr unsigned kRelJumpSlot = 7;
constexpr unsigned kMachine = EM_X86_64;
#else
#error "unsupported architecture"
#endif

int patchImport(std::uintptr_t base, const char* name, void* detour, std::atomic<void*>& originalOut) {
    const auto* eh = reinterpret_cast<const ElfW(Ehdr)*>(base);
    if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_machine != kMachine) return 0;

    const auto* phdr = reinterpret_cast<const ElfW(Phdr)*>(base + eh->e_phoff);
    std::uintptr_t span = 0;
    const ElfW(Dyn)* dynamic = nullptr;
    for (unsigned i = 0; i < eh->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) span = std::max<std::uintptr_t>(span, phdr[i].p_vaddr + phdr[i].p_memsz);
        if (phdr[i].p_type == PT_DYNAMIC) dynamic = reinterpret_cast<const ElfW(Dyn)*>(base + phdr[i].p_vaddr);
    }
    if (!dynamic) return 0;

    auto absolute = [&](std::uintptr_t v) { return (v >= base && v < base + span) ? v : base + v; };

    std::uintptr_t jmprel = 0, rela = 0, symtab = 0, strtab = 0;
    std::size_t jmprelSize = 0, relaSize = 0;
    for (const ElfW(Dyn)* d = dynamic; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_JMPREL: jmprel = absolute(d->d_un.d_ptr); break;
            case DT_PLTRELSZ: jmprelSize = d->d_un.d_val; break;
            case DT_RELA: rela = absolute(d->d_un.d_ptr); break;
            case DT_RELASZ: relaSize = d->d_un.d_val; break;
            case DT_SYMTAB: symtab = absolute(d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = absolute(d->d_un.d_ptr); break;
            default: break;
        }
    }
    if (!symtab || !strtab) return 0;

    int patched = 0;
    auto scan = [&](std::uintptr_t table, std::size_t bytes) {
        if (!table || !bytes) return;
        const auto* entries = reinterpret_cast<const ElfW(Rela)*>(table);
        for (std::size_t i = 0; i < bytes / sizeof(ElfW(Rela)); ++i) {
            const unsigned type = ELF64_R_TYPE(entries[i].r_info);
            if (type != kRelGlobDat && type != kRelJumpSlot) continue;
            const auto* symbol = reinterpret_cast<const ElfW(Sym)*>(symtab) + ELF64_R_SYM(entries[i].r_info);
            const char* symbolName = reinterpret_cast<const char*>(strtab) + symbol->st_name;
            if (std::strcmp(symbolName, name) != 0) continue;

            void** slot = reinterpret_cast<void**>(base + entries[i].r_offset);
            void* current = *slot;
            if (current == detour) continue;
            void* expected = nullptr;
            originalOut.compare_exchange_strong(expected, current);
            if (writeSlot(slot, detour)) {
                gPatches.push_back({slot, current, detour});
                ++patched;
            }
        }
    };
    scan(jmprel, jmprelSize);
    scan(rela, relaSize);
    return patched;
}

}

bool installGlHooks() {
    if (gActive.load()) return true;

    void* gles = ::dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!gles) gles = ::dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    if (!gles) {
        return false;
    }
    bool complete = true;
    auto load = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(::dlsym(gles, name));
        if (!fn) {
            complete = false;
        }
    };
#define LOAD(n) load(real.n, "gl" #n)
    LOAD(BindFramebuffer); LOAD(FramebufferTexture2D); LOAD(DrawElements); LOAD(DrawArrays); LOAD(DrawElementsInstanced); LOAD(DrawArraysInstanced);
    LOAD(GenTextures); LOAD(DeleteTextures); LOAD(BindTexture); LOAD(TexParameteri); LOAD(TexStorage2D); LOAD(ActiveTexture);
    LOAD(GenFramebuffers); LOAD(BlitFramebuffer); LOAD(CheckFramebufferStatus); LOAD(GetUniformLocation); LOAD(GetUniformiv);
    LOAD(GetIntegerv); LOAD(IsEnabled); LOAD(Enable); LOAD(Disable); LOAD(GetError);
    LOAD(GetTexLevelParameteriv); LOAD(GetRenderbufferParameteriv); LOAD(BindRenderbuffer); LOAD(GetFramebufferAttachmentParameteriv);
    LOAD(UseProgram); LOAD(LinkProgram); LOAD(DeleteProgram); LOAD(DeleteFramebuffers); LOAD(DeleteRenderbuffers);
    LOAD(FramebufferTextureLayer); LOAD(FramebufferRenderbuffer); LOAD(TexImage2D); LOAD(RenderbufferStorage); LOAD(RenderbufferStorageMultisample); LOAD(ReadBuffer);
#undef LOAD
    real.ProgramBinary = reinterpret_cast<decltype(real.ProgramBinary)>(::dlsym(gles, "glProgramBinary"));
    if (!complete) return false;

    gGetCurrentContext = reinterpret_cast<void* (*)()>(::dlsym(RTLD_DEFAULT, "eglGetCurrentContext"));
    gGetCurrentSurface = reinterpret_cast<void* (*)(int)>(::dlsym(RTLD_DEFAULT, "eglGetCurrentSurface"));
    int gotSlots = 0;
    bool eglViaGot = false, swapViaGot = false;
    const std::uintptr_t base = findModuleBase("libminecraftpe.so");
    if (base) {
        for (int i = 0; i < kSlotCount; ++i) {
            const int patched = patchImport(base, kHooks[i].name, kHooks[i].detour, gOrig[i]);
            if (i <= kDrawArraysInstanced) gotSlots += patched;
        }
        swapViaGot = patchImport(base, "eglSwapBuffers", reinterpret_cast<void*>(&eglSwapBuffersDetour), gSwapOrig) > 0;
        eglViaGot = patchImport(base, "eglGetProcAddress", reinterpret_cast<void*>(&eglGetProcAddressDetour), gEglOrig) > 0;
    }

    bool eglInline = false;
    if (!eglViaGot) {
        void* egl = ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
        if (egl) {
            void* original = nullptr;
            if (pl::memory::hook(egl, reinterpret_cast<void*>(&eglGetProcAddressDetour), &original) == 0 && original) {
                gEglOrig.store(original);
                gInlineTarget = egl;
                eglInline = true;
            }
        }
    }
    bool swapInline = false;
    if (!swapViaGot) {
        void* swap = ::dlsym(RTLD_DEFAULT, "eglSwapBuffers");
        if (swap) {
            void* original = nullptr;
            if (pl::memory::hook(swap, reinterpret_cast<void*>(&eglSwapBuffersDetour), &original) == 0 && original) {
                gSwapOrig.store(original);
                gSwapInlineTarget = swap;
                swapInline = true;
            }
        }
    }

    if (!swapViaGot && !swapInline) {
        return false;
    }
    if (gotSlots == 0 && !eglViaGot && !eglInline) {
        return false;
    }
    gActive.store(true);
    return true;
}

void removeGlHooks() {
    if (!gActive.exchange(false)) return;
    for (const SlotPatch& p : gPatches) {
        if (*p.slot == p.detour) (void)writeSlot(p.slot, p.original);
    }
    gPatches.clear();
    if (gInlineTarget) {
        (void)pl::memory::unhook(gInlineTarget, reinterpret_cast<void*>(&eglGetProcAddressDetour));
        gInlineTarget = nullptr;
    }
    if (gSwapInlineTarget) {
        (void)pl::memory::unhook(gSwapInlineTarget, reinterpret_cast<void*>(&eglSwapBuffersDetour));
        gSwapInlineTarget = nullptr;
    }
}

bool glHooksActive() { return gActive.load(); }

}
