#include "GlHooks.hpp"

#include <pl/memory/Hook.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
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
constexpr GLenum GL_DEPTH_FUNC = 0x0B74, GL_DEPTH_WRITEMASK = 0x0B72, GL_DEPTH_TEST = 0x0B71, GL_DEPTH_CLEAR_VALUE = 0x0B73;
constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037, GL_LESS = 0x0201;
constexpr GLenum GL_BLEND = 0x0BE2, GL_COLOR_WRITEMASK = 0x0C23, GL_COLOR_CLEAR_VALUE = 0x0C22;
constexpr int kEglDraw = 0x3059;
// ---- shadow maps ----
// Three maps, the same split Iris/OptiFine use for transparent shadows:
//   s_ShadowMap    depth of OPAQUE casters only                       (Iris: shadowtex1)
//   s_ShadowAll    depth of ALL casters, opaque + transparent         (Iris: shadowtex0)
//   s_ShadowColor  RGBA written by the nearest of those casters       (Iris: shadowcolor0)
// A program that declares "ShadowPass" is an opaque caster: drawn into s_ShadowMap. A program that declares
// "ShadowTransparentPass" is a transparent caster: it is drawn into the all-casters map only (its fragment shader
// writes the colour). The all-casters depth starts as a copy of the opaque depth the first time a transparent
// caster is drawn in a frame, so the opaque casters are not drawn a third time.
// Every draw whose program declares the uniform "ShadowPass" is drawn a second time into a depth-only
// texture while that uniform is 1. Programs that declare the sampler "s_ShadowMap" get the finished map
// of the PREVIOUS frame (double buffered), so receivers never read a half-built map.
// Resolution comes from the material: the uniform "ShadowRes" (vec4: x = texels per side, y = depth bits, z/w unused) of the caster
// program, read once per frame. The map is recreated whenever it changes. Without that uniform: the default.
constexpr int kShadowSize = 1024;                 // default texels per side
constexpr int kShadowSizeMin = 256, kShadowSizeMax = 4096;
constexpr GLenum kShadowFormat = GL_DEPTH_COMPONENT16;   // default (ShadowRes.y unset / 0); ShadowRes.y = 24 or 32 picks DEPTH_COMPONENT24 / 32F
constexpr float kShadowPolySlope = 2.0f, kShadowPolyUnits = 4.0f;  // slope-scaled depth bias while casting
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
    void (*GetUniformfv)(GLuint, GLint, float*);
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
    void (*Uniform4f)(GLint, float, float, float, float);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Clear)(GLbitfield);
    void (*ClearDepthf)(float);
    void (*DepthMask)(GLboolean);
    void (*DepthFunc)(GLenum);
    void (*DrawBuffers)(GLsizei, const GLenum*);
    void (*PolygonOffset)(float, float);
    void (*GetFloatv)(GLenum, float*);
    void (*ClearColor)(float, float, float, float);
    void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*GetBooleanv)(GLenum, GLboolean*);
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
    GLint shadowSampler = -1;   // texture unit of s_ShadowMap, -1 if the program has none
    GLint shadowPassLoc = -1;   // location of the "ShadowPass" uniform, -1 if the program is not a caster
    GLint shadowResLoc = -1;   // location of the "ShadowRes" uniform (vec4: x = shadow map resolution, y = depth bits, z/w unused), -1 if absent
    GLint transPassLoc = -1;    // location of "ShadowTransparentPass", -1 if the program is not a transparent caster
    GLint allSampler = -1;      // texture unit of s_ShadowAll, -1 if the program has none
    GLint colorSampler = -1;    // texture unit of s_ShadowColor, -1 if the program has none
    // chunk culling (see cullsOutsideMap): locations of u_model / ShadowCull / GameTimeOfDay, -1 if the program has none
    GLint modelLoc = -1, cullLoc = -1, timeLoc = -1;
    struct Cull {               // refreshed once per frame per program
        unsigned frame = 0xFFFFFFFFu;
        bool on = false;
        float radius = 0.0f, limit = 0.0f, lx = 0.0f, ly = 1.0f;
    };
    mutable Cull cull;
};
struct Shadow {
    GLuint tex[2] = {0, 0};
    GLuint fbo[2] = {0, 0};
    int write = 0;              // index being written this frame; the other one is read by receivers
    int size = 0;               // texels per side of the current (or last attempted) maps
    unsigned sizeFrame = 0xFFFFFFFFu;   // frame in which the material resolution was last read
    int wantedSize = 0;         // resolution the material asked for in that frame
    GLenum format = kShadowFormat, wantedFormat = kShadowFormat;   // depth format of the maps, and the one the material asked for
    unsigned castsSinceCreate = 0;   // caster draws since the maps were created (GL errors are only drained for the first few)
    bool created = false, failed = false, readValid = false;
    unsigned clearedFrame = 0xFFFFFFFFu;
    // all-casters map: depth + colour, double buffered like the opaque map, created when the first transparent caster draws
    GLuint allTex[2] = {0, 0}, colorTex[2] = {0, 0}, allFbo[2] = {0, 0};
    bool allCreated = false, allFailed = false, allReadValid = false;
    unsigned allFrame = 0xFFFFFFFFu;    // frame in which the all-casters map was started (depth copied, colour cleared)
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
    Shadow sh;
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
    int shadowUnit = -1;
    GLint prevShadow = 0;
    int allUnit = -1, colorUnit = -1;
    GLint prevAll = 0, prevColor = 0;
    GLint program = 0;
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
        u.shadowPassLoc = real.GetUniformLocation(program, "ShadowPass");
        u.shadowResLoc = real.GetUniformLocation(program, "ShadowRes");
        const GLint sloc = real.GetUniformLocation(program, "s_ShadowMap");
        if (sloc >= 0) {
            GLint unit = -1;
            real.GetUniformiv(program, sloc, &unit);
            if (unit >= 0 && unit < kTrackedUnits) u.shadowSampler = unit;
        }
        u.transPassLoc = real.GetUniformLocation(program, "ShadowTransparentPass");
        auto samplerUnit = [&](const char* name) {
            const GLint loc = real.GetUniformLocation(program, name);
            GLint unit = -1;
            if (loc >= 0) real.GetUniformiv(program, loc, &unit);
            return (unit >= 0 && unit < kTrackedUnits) ? unit : -1;
        };
        u.allSampler = samplerUnit("s_ShadowAll");
        u.colorSampler = samplerUnit("s_ShadowColor");
        u.cullLoc = real.GetUniformLocation(program, "ShadowCull");
        u.timeLoc = real.GetUniformLocation(program, "GameTimeOfDay");
        u.modelLoc = real.GetUniformLocation(program, "u_model");
        if (u.modelLoc < 0) u.modelLoc = real.GetUniformLocation(program, "u_model[0]");
        if (u.cullLoc < 0 || u.timeLoc < 0) u.modelLoc = -1;   // not a culling program: never query the model matrix
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
    if (units->unit[0] < 0 && units->unit[1] < 0 && units->shadowSampler < 0 && units->allSampler < 0 && units->colorSampler < 0) return bind;

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

    // shadow maps: last frame's finished maps; until they exist a white texture reads as depth 1.0 (= lit, uncoloured)
    auto bindExtra = [&](GLint unit, GLuint wanted, int& unitOut, GLint& prevOut) {
        if (unit < 0) return;
        const GLuint source = wanted ? wanted : fallbackTexture(st, kDepth);
        if (!source) return;
        if (!havePrevActive) {
            bind.prevActive = bind.current = trackedActive(st);
            havePrevActive = true;
        }
        prevOut = trackedTexture(st, unit);
        if (bind.current != unit) {
            real.ActiveTexture(GL_TEXTURE0 + unit);
            bind.current = unit;
        }
        real.BindTexture(GL_TEXTURE_2D, source);
        unitOut = unit;
    };
    const int rd = st.sh.write ^ 1;
    bindExtra(units->shadowSampler, st.sh.readValid ? st.sh.tex[rd] : 0, bind.shadowUnit, bind.prevShadow);
    // Without transparent casters in that frame the all-casters depth IS the opaque depth, so hand out the opaque map
    // (a white texture here would read as "nothing in the way" and wipe out every opaque shadow). Colour: white.
    const bool allOk = st.sh.readValid && st.sh.allReadValid;
    bindExtra(units->allSampler, allOk ? st.sh.allTex[rd] : (st.sh.readValid ? st.sh.tex[rd] : 0), bind.allUnit, bind.prevAll);
    bindExtra(units->colorSampler, allOk ? st.sh.colorTex[rd] : 0, bind.colorUnit, bind.prevColor);
    return bind;
}

void unbindBuffers(const Bind& bind) {
    if (bind.unit[kScreen] < 0 && bind.unit[kDepth] < 0 && bind.shadowUnit < 0 && bind.allUnit < 0 && bind.colorUnit < 0) return;
    GLint current = bind.current;
    auto restoreUnit = [&](int unit, GLint previous) {
        if (unit < 0) return;
        if (current != unit) {
            real.ActiveTexture(GL_TEXTURE0 + unit);
            current = unit;
        }
        real.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    };
    restoreUnit(bind.colorUnit, bind.prevColor);
    restoreUnit(bind.allUnit, bind.prevAll);
    restoreUnit(bind.shadowUnit, bind.prevShadow);
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


// Deletes the shadow maps so ensureShadow() can recreate them at another resolution.
void releaseShadow(State& st) {
    Shadow& sh = st.sh;
    for (int i = 0; i < 2; ++i) {
        if (sh.fbo[i]) real.DeleteFramebuffers(1, &sh.fbo[i]);
        if (sh.tex[i]) real.DeleteTextures(1, &sh.tex[i]);
        if (sh.allFbo[i]) real.DeleteFramebuffers(1, &sh.allFbo[i]);
        if (sh.allTex[i]) real.DeleteTextures(1, &sh.allTex[i]);
        if (sh.colorTex[i]) real.DeleteTextures(1, &sh.colorTex[i]);
    }
    const unsigned frame = sh.sizeFrame;
    const int wantedSize = sh.wantedSize;
    const GLenum wantedFormat = sh.wantedFormat;
    sh = Shadow{};
    sh.sizeFrame = frame;
    sh.wantedSize = wantedSize;
    sh.wantedFormat = wantedFormat;
}

// Resolution and depth format requested by the material (ShadowRes.x / .y), read at most once per frame.
void wantedShadowRes(GLuint program, const Units* units, int& size, GLenum& format) {
    float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // x = size, y = depth bits
    if (units->shadowResLoc >= 0 && real.GetUniformfv) real.GetUniformfv(program, units->shadowResLoc, v);
    size = kShadowSize;   // unset / zero: default
    if (v[0] >= 1.0f) size = std::clamp(static_cast<int>(v[0] + 0.5f), kShadowSizeMin, kShadowSizeMax);
    // 16 bit (default) halves the memory and bandwidth (good on tile based GPUs); 24 bit for more precision, 32 bit float for very long shadow ranges
    format = v[1] > 28.0f ? GL_DEPTH_COMPONENT32F : (v[1] >= 20.0f ? GL_DEPTH_COMPONENT24 : kShadowFormat);
}

// Creates the two depth textures + framebuffers once per context (and again when the resolution changes).
bool ensureShadow(State& st, int size) {
    Shadow& sh = st.sh;
    if (sh.created) return true;
    if (sh.failed) return false;
    sh.size = size;
    sh.format = sh.wantedFormat;
    Saved saved{};
    save(saved);
    while (real.GetError() != 0) {}
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i) {
        real.GenTextures(1, &sh.tex[i]);
        real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
        real.BindTexture(GL_TEXTURE_2D, sh.tex[i]);
        real.TexStorage2D(GL_TEXTURE_2D, 1, sh.format, size * 3, size);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        real.GenFramebuffers(1, &sh.fbo[i]);
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sh.fbo[i]);
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sh.tex[i], 0);
        const GLenum none = GL_NONE;   // depth-only: no colour output
        real.DrawBuffers(1, &none);
        ok = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    restore(saved);
    if (real.GetError() != 0) ok = false;
    while (real.GetError() != 0) {}
    if (!ok) {
        sh.failed = true;
        return false;
    }
    sh.created = true;
    return true;
}

// Creates the all-casters map (depth + colour, twice) the first time a transparent caster is drawn.
// On failure only the coloured shadows are lost, the opaque map keeps working.
bool ensureAllCasters(State& st) {
    Shadow& sh = st.sh;
    if (sh.allCreated) return true;
    if (sh.allFailed) return false;
    Saved saved{};
    save(saved);
    while (real.GetError() != 0) {}
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i) {
        auto makeTexture = [&](GLuint& tex, GLenum format) {
            real.GenTextures(1, &tex);
            real.ActiveTexture(GL_TEXTURE0 + kScratchUnit);
            real.BindTexture(GL_TEXTURE_2D, tex);
            real.TexStorage2D(GL_TEXTURE_2D, 1, format, sh.size * 3, sh.size);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        };
        makeTexture(sh.allTex[i], sh.format);
        makeTexture(sh.colorTex[i], GL_RGBA8);
        real.GenFramebuffers(1, &sh.allFbo[i]);
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sh.allFbo[i]);
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sh.allTex[i], 0);
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sh.colorTex[i], 0);
        const GLenum buffer = GL_COLOR_ATTACHMENT0;
        real.DrawBuffers(1, &buffer);
        ok = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    restore(saved);
    if (real.GetError() != 0) ok = false;
    while (real.GetError() != 0) {}
    if (!ok) {
        for (int i = 0; i < 2; ++i) {
            if (sh.allFbo[i]) real.DeleteFramebuffers(1, &sh.allFbo[i]);
            if (sh.allTex[i]) real.DeleteTextures(1, &sh.allTex[i]);
            if (sh.colorTex[i]) real.DeleteTextures(1, &sh.colorTex[i]);
            sh.allFbo[i] = sh.allTex[i] = sh.colorTex[i] = 0;
        }
        sh.allFailed = true;
        return false;
    }
    sh.allCreated = true;
    return true;
}

// Shadow-pass culling (the same idea as OptiFine's ShadowFrustum / applyFrustumShadow): a chunk draw whose box lies
// completely outside the shadow map cannot put a single pixel into it, so the second draw is skipped. The hardware
// would clip all of its triangles anyway, but only after the vertex shader ran for every vertex of the chunk.
// Opt-in per material through the uniform ShadowCull (vec4): x = map radius in blocks (also used by the shader),
// y = depth range, z = 1 to enable culling for this program (terrain only, entities have no known bounds).
// The chunk position comes from the u_model translation, which is relative to the camera like the caster maths.
// Anything unexpected (no uniform, instanced draw, matrix that is not a plain translation) -> not culled.
bool cullsOutsideMap(State& st, GLuint program, const Units* units, bool instanced) {
    if (instanced || units->modelLoc < 0) return false;
    Units::Cull& c = units->cull;
    if (c.frame != st.frame) {
        c.frame = st.frame;
        float p[4] = {0.0f, 0.0f, 0.0f, 0.0f}, t[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        real.GetUniformfv(program, units->cullLoc, p);
        real.GetUniformfv(program, units->timeLoc, t);
        c.on = p[2] > 0.5f && p[0] > 1.0f;
        c.radius = p[0];
        // Same circle as sunDirection() in the shader, but without its stepping: the sun is at most one step (half a degree
        // by default, SHADOW_SUN_STEP) away from this, and the margin below covers up to about 5 degrees.
        const float turns = t[0] - std::floor(t[0]);
        const float a = 6.28318530718f * turns;
        c.lx = -std::sin(a);
        c.ly = std::cos(a);
        if (!(c.ly > 0.0f)) { c.lx = -c.lx; c.ly = -c.ly; }
        c.limit = p[0] * 1.5f + 32.0f;
    }
    if (!c.on) return false;
    float m[16];
    real.GetUniformfv(program, units->modelLoc, m);
    // chunk draws use a plain translation (unit scale, no rotation); the translation is either in elements 12-14 or 3,7,11
    // depending on the matrix layout, the other three are zero
    const float eps = 0.01f;
    if (std::fabs(m[0] - 1.0f) > eps || std::fabs(m[5] - 1.0f) > eps || std::fabs(m[10] - 1.0f) > eps) return false;
    const bool zeroA = std::fabs(m[3]) < eps && std::fabs(m[7]) < eps && std::fabs(m[11]) < eps;
    const bool zeroB = std::fabs(m[12]) < eps && std::fabs(m[13]) < eps && std::fabs(m[14]) < eps;
    float x, y, z;
    if (zeroA) { x = m[12]; y = m[13]; z = m[14]; }
    else if (zeroB) { x = m[3]; y = m[7]; z = m[11]; }
    else return false;
    constexpr float kHalf = 32.0f;
    const float cx = z + 8.0f;                              // light space "right" = (0, 0, 1)
    const float cy = (x + 8.0f) * c.ly - (y + 8.0f) * c.lx; // light space "up" = (ly, -lx, 0)
    const float ey = kHalf * (std::fabs(c.lx) + std::fabs(c.ly));
    return std::fabs(cx) > c.limit + kHalf || std::fabs(cy) > c.limit + ey;
}

// Called right after a real draw: if the current program is a shadow caster, repeat the exact same draw
// into the shadow maps with the caster's pass uniform = 1. All other GL state (VAO, buffers, textures, uniforms) is the
// one the game just used, so nothing has to be recorded. Everything we touch is restored afterwards.
//   opaque caster      ("ShadowPass")            -> opaque depth map; also the all-casters map if that one is already running this frame
//   transparent caster ("ShadowTransparentPass") -> all-casters map only (depth test against the opaque depth copied into it)
template <class Redraw>
void castShadow(const Bind& bind, bool instanced, Redraw&& redraw) {
    if (bind.program <= 0) return;
    State& st = state();
    const Units* units = unitsFor(st, static_cast<GLuint>(bind.program));
    const bool opaque = units->shadowPassLoc >= 0;
    const bool transparent = !opaque && units->transPassLoc >= 0;
    if (!opaque && !transparent) return;
    if (cullsOutsideMap(st, static_cast<GLuint>(bind.program), units, instanced)) return;   // nothing of it can land in the map
    if (st.sh.sizeFrame != st.frame) {
        st.sh.sizeFrame = st.frame;
        int wanted = 0;
        GLenum wantedFormat = kShadowFormat;
        wantedShadowRes(static_cast<GLuint>(bind.program), units, wanted, wantedFormat);
        st.sh.wantedSize = wanted;
        st.sh.wantedFormat = wantedFormat;
        if ((st.sh.created || st.sh.failed) && (st.sh.size != wanted || st.sh.format != wantedFormat)) releaseShadow(st);
    }
    if (!ensureShadow(st, st.sh.wantedSize ? st.sh.wantedSize : kShadowSize)) return;
    if (transparent && !ensureAllCasters(st)) return;
    Shadow& sh = st.sh;

    GLint drawFbo = 0, depthFunc = 0, depthMask = 0, viewport[4] = {0, 0, 0, 0};
    float clearDepth = 1.0f;
    real.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    for (int i = 0; i < 2; ++i) {
        if (drawFbo == static_cast<GLint>(sh.fbo[i]) || (sh.allFbo[i] && drawFbo == static_cast<GLint>(sh.allFbo[i]))) return;
    }
    real.GetIntegerv(GL_VIEWPORT, viewport);
    real.GetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    real.GetIntegerv(GL_DEPTH_WRITEMASK, &depthMask);
    const GLboolean scissor = real.IsEnabled(GL_SCISSOR_TEST), discard = real.IsEnabled(GL_RASTERIZER_DISCARD);
    const GLboolean depthTest = real.IsEnabled(GL_DEPTH_TEST), polyOffset = real.IsEnabled(GL_POLYGON_OFFSET_FILL);
    // colour state is only touched while drawing into the all-casters map
    const bool colour = transparent || (opaque && sh.allCreated && sh.allFrame == st.frame);
    GLboolean blend = 0, colorMask[4] = {1, 1, 1, 1};
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (colour) {
        blend = real.IsEnabled(GL_BLEND);
        real.GetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        real.GetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    }

    // Only change what differs from the game's state (during chunk draws that is usually just the viewport,
    // the depth function and polygon offset); the same checks are used to restore it afterwards.
    const bool sameViewport = viewport[0] == 0 && viewport[1] == 0 && viewport[2] == sh.size && viewport[3] == sh.size;
    if (!sameViewport) real.Viewport(0, 0, sh.size, sh.size);
    if (scissor) real.Disable(GL_SCISSOR_TEST);
    if (discard) real.Disable(GL_RASTERIZER_DISCARD);
    if (!depthTest) real.Enable(GL_DEPTH_TEST);
    if (static_cast<GLenum>(depthFunc) != GL_LESS) real.DepthFunc(GL_LESS);
    if (!depthMask) real.DepthMask(1);
    if (!polyOffset) real.Enable(GL_POLYGON_OFFSET_FILL);
    real.PolygonOffset(kShadowPolySlope, kShadowPolyUnits);

    // the opaque depth map is cleared once per frame, whichever kind of caster comes first
    if (sh.clearedFrame != st.frame) {
        sh.clearedFrame = st.frame;
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sh.fbo[sh.write]);
        real.GetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        real.ClearDepthf(1.0f);
        real.Clear(GL_DEPTH_BUFFER_BIT);
        real.ClearDepthf(clearDepth);
    }

    auto drawCascade = [&](int cascade, GLint passLoc) {
        real.Viewport(cascade * sh.size, 0, sh.size, sh.size);
        real.Uniform4f(passLoc, 1.0f, static_cast<float>(cascade), 0.0f, 0.0f);
        redraw();
    };

    auto drawAll = [&](GLint passLoc) {
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sh.allFbo[sh.write]);
        real.Disable(GL_BLEND);
        real.ColorMask(1, 1, 1, 1);
        drawCascade(0, passLoc);
        drawCascade(1, passLoc);
        drawCascade(2, passLoc);
        real.Uniform4f(passLoc, 0.0f, 0.0f, 0.0f, 0.0f);
    };

    if (opaque) {
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sh.fbo[sh.write]);
        drawCascade(0, units->shadowPassLoc);
        drawCascade(1, units->shadowPassLoc);
        drawCascade(2, units->shadowPassLoc);
        real.Uniform4f(units->shadowPassLoc, 0.0f, 0.0f, 0.0f, 0.0f);
        if (colour) drawAll(units->shadowPassLoc);
    } else {
        if (sh.allFrame != st.frame) {
            sh.allFrame = st.frame;
            GLint readFbo = 0;
            real.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
            real.BindFramebuffer(GL_READ_FRAMEBUFFER, sh.fbo[sh.write]);
            real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sh.allFbo[sh.write]);
            real.BlitFramebuffer(0, 0, sh.size * 3, sh.size, 0, 0, sh.size * 3, sh.size, GL_DEPTH_BUFFER_BIT, static_cast<GLenum>(GL_NEAREST));
            real.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
            real.ColorMask(1, 1, 1, 1);
            real.ClearColor(1.0f, 1.0f, 1.0f, 0.0f);
            real.Clear(GL_COLOR_BUFFER_BIT);
        }
        drawAll(units->transPassLoc);
    }

    if (colour) {
        real.ColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        real.ClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        setEnabled(GL_BLEND, blend);
    }
    if (!polyOffset) real.Disable(GL_POLYGON_OFFSET_FILL);
    if (!depthMask) real.DepthMask(0);
    if (static_cast<GLenum>(depthFunc) != GL_LESS) real.DepthFunc(static_cast<GLenum>(depthFunc));
    if (!depthTest) real.Disable(GL_DEPTH_TEST);
    if (discard) real.Enable(GL_RASTERIZER_DISCARD);
    if (scissor) real.Enable(GL_SCISSOR_TEST);
    if (!sameViewport) real.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
    // Our calls were validated when the maps were created; draining the error queue after every single draw is a
    // driver round trip, so only do it for the first draws (keeps a one-off error from reaching the game's own checks).
    if (sh.castsSinceCreate < 240) {
        ++sh.castsSinceCreate;
        while (real.GetError() != 0) {}
    }
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
    st.sh = Shadow{};
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
    Bind bind = bindBuffers(st, program);
    bind.program = program;
    return bind;
}

void drawElementsDetour(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    const Bind bind = onDraw();
    resolve(kDrawElements, real.DrawElements)(mode, count, type, indices);
    if (count > 0) castShadow(bind, false, [&] { resolve(kDrawElements, real.DrawElements)(mode, count, type, indices); });
    unbindBuffers(bind);
}
void drawArraysDetour(GLenum mode, GLint first, GLsizei count) {
    const Bind bind = onDraw();
    resolve(kDrawArrays, real.DrawArrays)(mode, first, count);
    if (count > 0) castShadow(bind, false, [&] { resolve(kDrawArrays, real.DrawArrays)(mode, first, count); });
    unbindBuffers(bind);
}
void drawElementsInstancedDetour(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances) {
    const Bind bind = onDraw();
    resolve(kDrawElementsInstanced, real.DrawElementsInstanced)(mode, count, type, indices, instances);
    if (count > 0 && instances > 0) castShadow(bind, true, [&] { resolve(kDrawElementsInstanced, real.DrawElementsInstanced)(mode, count, type, indices, instances); });
    unbindBuffers(bind);
}
void drawArraysInstancedDetour(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    const Bind bind = onDraw();
    resolve(kDrawArraysInstanced, real.DrawArraysInstanced)(mode, first, count, instances);
    if (count > 0 && instances > 0) castShadow(bind, true, [&] { resolve(kDrawArraysInstanced, real.DrawArraysInstanced)(mode, first, count, instances); });
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
    // finished shadow map becomes the one receivers read next frame
    if (st.sh.created && st.sh.clearedFrame == st.frame) {
        st.sh.allReadValid = st.sh.allCreated && st.sh.allFrame == st.frame;   // no transparent casters this frame: receivers get the white fallback
        st.sh.write ^= 1;
        st.sh.readValid = true;
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
    LOAD(GenFramebuffers); LOAD(BlitFramebuffer); LOAD(CheckFramebufferStatus); LOAD(GetUniformLocation); LOAD(GetUniformiv); LOAD(GetUniformfv);
    LOAD(GetIntegerv); LOAD(IsEnabled); LOAD(Enable); LOAD(Disable); LOAD(GetError);
    LOAD(GetTexLevelParameteriv); LOAD(GetRenderbufferParameteriv); LOAD(BindRenderbuffer); LOAD(GetFramebufferAttachmentParameteriv);
    LOAD(UseProgram); LOAD(LinkProgram); LOAD(DeleteProgram); LOAD(DeleteFramebuffers); LOAD(DeleteRenderbuffers);
    LOAD(FramebufferTextureLayer); LOAD(FramebufferRenderbuffer); LOAD(TexImage2D); LOAD(RenderbufferStorage); LOAD(RenderbufferStorageMultisample); LOAD(ReadBuffer);
    LOAD(Uniform4f); LOAD(Viewport); LOAD(Clear); LOAD(ClearDepthf); LOAD(DepthMask); LOAD(DepthFunc); LOAD(DrawBuffers); LOAD(PolygonOffset); LOAD(GetFloatv); LOAD(ClearColor); LOAD(ColorMask); LOAD(GetBooleanv);
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
