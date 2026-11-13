// GLSL ES 3.2 generation for the GX pipeline.
#include "gl_shadergen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpu.h"
#include "gx_record.h"

namespace gpu {

namespace {

struct Out {
    std::string s;
    void w(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        s += buf;
    }
};

const char* kEyeBlock = R"(
layout(std140, binding = 4) uniform EyeBlock {
    vec4 vrView[4];
    vec4 vrProj[4];
    ivec4 vrFlags;
    vec4 vrFocus;
    vec4 vrCut;
    vec4 vrEyePos;
    vec4 vrStereo;
    vec4 vrStereo2;
};
)";

}  // namespace

// How a shader reaches the frame's data (see gl_shadergen.h): storage buffers,
// or texel fetches from 2048 texel wide textures when `useTextures`.  That
// path uses no storage buffer in either stage: Mali drivers give the vertex
// stage none (or too few), and a pixel state is as easy to fetch as to index.
std::string dataAccessDecls(bool fragment, bool useTextures) {
    std::string s;
    if (!useTextures) {
        if (!fragment) {
            s += "layout(std430, binding = 0) readonly buffer RowBuf { vec4 rows[]; };\n";
            s += "layout(std430, binding = 1) readonly buffer RecBuf { uint recs[]; };\n";
            s += "layout(std430, binding = 3) readonly buffer VtxRecBuf { uint vtxRec[]; };\n";
            s += "#define FETCH_ROW(i) rows[i]\n#define FETCH_REC(i) recs[i]\n#define FETCH_VTXREC(i) vtxRec[i]\n";
            s += "#define FETCH_REC4(i) uvec4(recs[i], recs[(i) + 1u], recs[(i) + 2u], recs[(i) + 3u])\n";
        }
        s += "layout(std430, binding = 2) readonly buffer StateBuf { vec4 st[]; };\n";
        s += "#define FETCH_ST(i) st[i]\n";
    } else {
        // highp samplers: the default sampler precision is lowp, which Mali
        // honours with 16-bit floats.
        if (!fragment) {
            s += "layout(binding = 8) uniform highp sampler2D s_rowTex;\n";
            s += "layout(binding = 9) uniform highp usampler2D s_recTex;\n";
            s += "layout(binding = 11) uniform highp usampler2D s_vtxRecTex;\n";
            s += "#define FETCH_ROW(i) texelFetch(s_rowTex, ivec2(int((i) & 2047u), int((i) >> 11u)), 0)\n";
            // Four words per texel; the component is picked with a scalar
            // index into a local copy.
            s += "uint pickWord(uvec4 v, uint k) { return k == 0u ? v.x : k == 1u ? v.y : k == 2u ? v.z : v.w; }\n";
            s += "#define FETCH_REC(i) pickWord(texelFetch(s_recTex, ivec2(int(((i) >> 2u) & 2047u), int((i) >> 13u)), 0), (i) & 3u)\n";
            s += "#define FETCH_VTXREC(i) pickWord(texelFetch(s_vtxRecTex, ivec2(int(((i) >> 2u) & 2047u), int((i) >> 13u)), 0), (i) & 3u)\n";
            // Four consecutive words from a multiple of four: one texel.
            s += "#define FETCH_REC4(i) texelFetch(s_recTex, ivec2(int(((i) >> 2u) & 2047u), int((i) >> 13u)), 0)\n";
        }
        s += "layout(binding = 10) uniform highp sampler2D s_stateTex;\n";
        s += "#define FETCH_ST(i) texelFetch(s_stateTex, ivec2(int((i) & 2047u), int((i) >> 11u)), 0)\n";
    }
    s += "#define ST(i) FETCH_ST(i)\n";
    return s;
}

// ---------------------------------------------------------------------------
// Probe shaders of the renderer's driver self-tests (gl_renderer.cpp)
// ---------------------------------------------------------------------------
std::string genProbeQuadVs() {
    return "#version 320 es\nvoid main() {\n  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
           "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}\n";
}

std::string genProbeDataVs(bool useTextures) {
    char body[1024];
    snprintf(body, sizeof(body),
             "layout(location = 0) in vec2 a_pos;\n"
             "uniform highp uint u_firstOrdinal;\nflat out highp uint v_pix;\nflat out highp uint v_ok;\nvoid main() {\n"
             "  uint rb = FETCH_VTXREC(u_firstOrdinal + uint(gl_VertexID)) * %uu;\n"
             "  uint row = FETCH_REC(rb + %uu);\n  v_pix = FETCH_REC(rb + %uu);\n"
             "  bool ok = FETCH_REC(rb + %uu) == 0x89ABCDEFu;\n"
             "  ok = ok && FETCH_ROW(row) == vec4(1.5, -2.25, 3.0e6, 0.125);\n"
             "  ok = ok && FETCH_ST(v_pix + 1u) == vec4(-7.5, 0.5, 2.0, 1.0);\n"
             "  v_ok = ok ? 1u : 0u;\n"
             "  gl_Position = vec4(a_pos, 0.0, 1.0);\n}\n",
             (unsigned)kRecWords, (unsigned)(kRecPosSlots + 1), (unsigned)kRecPixel, (unsigned)kRecFlags);
    return "#version 320 es\nprecision highp float;\nprecision highp int;\n" + dataAccessDecls(false, useTextures) + body;
}

std::string genProbeDataFs(bool useTextures) {
    return "#version 320 es\nprecision highp float;\nprecision highp int;\n" + dataAccessDecls(true, useTextures) +
           "flat in highp uint v_pix;\nflat in highp uint v_ok;\nout vec4 o;\nvoid main() {\n"
           "  bool fok = ST(v_pix) == vec4(0.25, 0.5, -3.0, 1000.0);\n"
           "  o = vec4(v_ok == 1u ? 1.0 : 0.0, fok ? 1.0 : 0.0, 0.0, 1.0);\n}\n";
}

std::string genProbeIntFs() {
    return "#version 320 es\nprecision highp float;\nprecision highp int;\n"
           "uniform ivec3 u_a, u_b, u_c, u_d, u_mask, u_lerp;\nout vec4 o;\n"
           "ivec3 lo8(ivec3 v) { return ivec3(v.x & 255, v.y & 255, v.z & 255); }\n"
           "void main() {\n"
           "  bool vv = (u_a & ivec3(255)) == u_mask;\n"
           "  bool vsc = (u_a & 255) == u_mask;\n"
           "  bool sc = lo8(u_a) == u_mask;\n"
           "  ivec3 a = lo8(u_a), b = lo8(u_b), c = lo8(u_c);\n"
           "  ivec3 r = u_d + ((((a << 8) + (b - a) * (c + (c >> 7))) + ivec3(128)) >> 8);\n"
           "  o = vec4(vv ? 1.0 : 0.0, vsc ? 1.0 : 0.0, sc ? 1.0 : 0.0, r == u_lerp ? 1.0 : 0.0);\n}\n";
}

ShaderUid makeShaderUid(const uint32_t* bp, const uint32_t* xf, uint32_t vtxFlags, bool hud) {
    ShaderUid u;
    memset(&u, 0, sizeof(u));
    u.vtxFlags = vtxFlags;
    u.numTexGens = xf[0x3F] & 15;
    u.numChans = xf[0x09] & 3;
    u.dualTex = xf[0x12] & 1;
    for (int i = 0; i < 2; i++) {
        u.colorCtrl[i] = i < (int)u.numChans ? (xf[0x0E + i] & 0x7FFF) : 0;
        u.alphaCtrl[i] = i < (int)u.numChans ? (xf[0x10 + i] & 0x7FFF) : 0;
    }
    for (uint32_t i = 0; i < u.numTexGens && i < 8; i++) {
        u.texMtxInfo[i] = xf[0x40 + i] & 0x3FFFF;
        u.postMtxInfo[i] = u.dualTex ? (xf[0x50 + i] & 0x100) : 0;  // the matrix comes from the draw record
    }
    uint32_t gen = bp[0x00];
    u.numTevStages = ((gen >> 10) & 15) + 1;
    u.numIndStages = (gen >> 16) & 7;
    for (uint32_t s = 0; s < u.numTevStages; s++) {
        u.tevColor[s] = bp[0xC0 + 2 * s] & 0xFFFFFF;
        u.tevAlpha[s] = bp[0xC1 + 2 * s] & 0xFFFFFF;
    }
    for (uint32_t i = 0; i < (u.numTevStages + 1) / 2; i++) {
        u.tevOrder[i] = bp[0x28 + i] & 0xFFFFFF;
    }
    for (int i = 0; i < 8; i++) {
        u.ksel[i] = bp[0xF6 + i] & 0xFFFFFF;
    }
    u.alphaFunc = (bp[0xF3] >> 16) & 0xFF;
    u.fogSel = (bp[0xF1] >> 20) & 0xF;
    if (u.numIndStages) {
        for (uint32_t s = 0; s < u.numTevStages; s++) {
            u.indCmd[s] = bp[0x10 + s] & 0x1FFFFF;
        }
        u.iref = bp[0x27] & 0xFFFFFF;
        u.indScale[0] = bp[0x25] & 0xFF;
        u.indScale[1] = bp[0x26] & 0xFF;
    }
    uint32_t pixFmt = bp[0x43] & 7;
    u.efbHasAlpha = pixFmt == 1;  // RGBA6_Z24
    u.hud = hud;
    u.zTex = ((bp[0xF5] >> 2) & 3) ? (bp[0xF5] & 0xF) : 0;
    return u;
}

// ---------------------------------------------------------------------------
// Vertex shader
// ---------------------------------------------------------------------------
std::string genVertexShader(const ShaderUid& u, bool useVertexTextures) {
    Out o;
    o.w("#version 320 es\nprecision highp float;\nprecision highp int;\n");
    o.s += dataAccessDecls(false, useVertexTextures);
    o.s += kEyeBlock;
    uint32_t f = u.vtxFlags;
    o.w("layout(location = 0) in vec3 a_pos;\n");
    if (f & VF_POSMTX) o.w("layout(location = 1) in uint a_posmtx;\n");
    if (f & VF_NRM) o.w("layout(location = 2) in vec3 a_nrm;\n");
    if (f & VF_NBT) o.w("layout(location = 3) in vec3 a_bin;\nlayout(location = 4) in vec3 a_tan;\n");
    if (f & VF_CLR0) o.w("layout(location = 5) in vec4 a_clr0;\n");
    if (f & VF_CLR1) o.w("layout(location = 6) in vec4 a_clr1;\n");
    for (int i = 0; i < 8; i++) {
        if (f & (VF_TEX0 << i)) o.w("layout(location = %d) in vec2 a_tex%d;\n", 7 + i, i);
    }
    if (f & VF_TEXMTX) o.w("layout(location = 15) in uvec2 a_texmtx;\n");
    // Draw record of this vertex: indices are relative to the batch's first
    // vertex, so gl_VertexID + the batch's first ordinal indexes vtxRec[].
    o.w("uniform highp uint u_firstOrdinal;\n");
    o.w("flat out highp uint v_pix;\n");
    o.w("out vec4 v_clr0;\nout vec4 v_clr1;\n");
    for (uint32_t i = 0; i < u.numTexGens; i++) o.w("out vec3 v_tex%u;\n", i);
    o.w("out float v_gxz;\n");
    if (u.cutaway) o.w("out vec3 v_eye;\n");

    // Matrix rows: slot bases come from the draw record (3 rows per slot).
    o.w("#define slotRow(rb, firstSlot, idx) (FETCH_REC((rb) + (firstSlot) + uint((idx) / 3)) + uint((idx) %% 3))\n");
    o.w("#define mulRows(r, v) vec3(dot(FETCH_ROW(r), (v)), dot(FETCH_ROW((r) + 1u), (v)), dot(FETCH_ROW((r) + 2u), (v)))\n");
    o.w("#define L(i) FETCH_ST(lbase + uint(i))\n");
    // Direction and length of a vector of any magnitude: the components are
    // scaled before the dot product, so neither a far away light (specular
    // lights sit a million units out) nor garbage in an unused light can
    // overflow to inf / NaN, which drivers disagree on (Mali saturates a NaN
    // to white where Adreno yields black).  w = length, 0 for a null vector.
    o.w("vec4 dirLen(vec3 v, vec3 fallback) {\n");
    o.w("  float m = max(max(abs(v.x), abs(v.y)), abs(v.z));\n");
    o.w("  if (!(m > 1.0e-30) || !(m < 1.0e30)) return vec4(fallback, 0.0);\n");
    o.w("  vec3 s = v / m;\n  float l = sqrt(dot(s, s));\n");
    o.w("  return vec4(s / l, m * l);\n}\n");
    o.w("float safeDiv(float n, float d) { return (abs(d) > 1.0e-30) ? n / d : 0.0; }\n");
    o.w("void main() {\n");
    o.w("  uint rb = FETCH_VTXREC(u_firstOrdinal + uint(gl_VertexID)) * %uu;\n", (unsigned)kRecWords);
    // The record's state bases, matrix indices and flags sit in two groups
    // of four words (two texels on the texture path).
    static_assert(kRecWords % 4 == 0 && kRecLights == 41 && kRecProj == 42 && kRecPixel == 43, "record layout");
    static_assert(kRecMtxIdxA == 44 && kRecMtxIdxB == 45 && kRecFlags == 46, "record layout");
    o.w("  uvec4 recA = FETCH_REC4(rb + 40u);\n  uvec4 recB = FETCH_REC4(rb + 44u);\n");
    o.w("  uint lbase = recA.y;\n  uint pjbase = recA.z;\n  v_pix = recA.w;\n");
    o.w("  uint mA = recB.x;\n  uint mB = recB.y;\n  uint rflags = recB.z;\n");
    o.w("  vec4 proj[4] = vec4[4](FETCH_ST(pjbase), FETCH_ST(pjbase + 1u), FETCH_ST(pjbase + 2u), FETCH_ST(pjbase + 3u));\n");
    o.w("  vec4 depthParams = FETCH_ST(pjbase + 4u);\n");
    if (u.numChans) {
        o.w("  vec4 matColor[2] = vec4[2](L(40), L(41));\n  vec4 ambColor[2] = vec4[2](L(42), L(43));\n");
    }
    if (f & VF_POSMTX) {
        o.w("  int pidx = int(a_posmtx);\n");
    } else {
        o.w("  int pidx = int(mA & 63u);\n");
    }
    o.w("  vec4 p4 = vec4(a_pos, 1.0);\n");
    o.w("  uint pr_pos = slotRow(rb, %uu, pidx);\n", (unsigned)kRecPosSlots);
    o.w("  vec3 pos = mulRows(pr_pos, p4);\n");
    o.w("  int nidx = pidx & 31;\n");
    o.w("  uint nr = slotRow(rb, %uu, nidx);\n", (unsigned)kRecNrmSlots);
    o.w("  vec3 nrm = vec3(0.0, 0.0, 1.0);\n  vec3 bin = vec3(0.0);\n  vec3 tan = vec3(0.0);\n");
    if (f & VF_NRM) {
        o.w("  nrm = vec3(dot(FETCH_ROW(nr).xyz, a_nrm), dot(FETCH_ROW(nr + 1u).xyz, a_nrm), dot(FETCH_ROW(nr + 2u).xyz, a_nrm));\n");
        o.w("  nrm = dirLen(nrm, vec3(0.0, 0.0, 1.0)).xyz;\n");
    }
    if (f & VF_NBT) {
        o.w("  bin = vec3(dot(FETCH_ROW(nr).xyz, a_bin), dot(FETCH_ROW(nr + 1u).xyz, a_bin), dot(FETCH_ROW(nr + 2u).xyz, a_bin));\n");
        o.w("  tan = vec3(dot(FETCH_ROW(nr).xyz, a_tan), dot(FETCH_ROW(nr + 1u).xyz, a_tan), dot(FETCH_ROW(nr + 2u).xyz, a_tan));\n");
    }

    // Projection (GX clip space: z/w in [-1, 0]) with GX depth mapping, or
    // the VR camera for 3D draws.
    o.w("  vec4 v4 = vec4(pos, 1.0);\n");
    // GX clip position and window depth (also drives fog in VR, where the
    // rasterized depth comes from the eye projection instead).
    o.w("  vec4 c = vec4(dot(proj[0], v4), dot(proj[1], v4), dot(proj[2], v4), dot(proj[3], v4));\n");
    o.w("  float depth = (c.z * depthParams.x + c.w * depthParams.y) * (1.0 / 16777215.0);\n");
    o.w("  v_gxz = c.w != 0.0 ? depth / c.w : depth;\n");
    o.w("  bool vrDraw = (rflags & 1u) != 0u && vrFlags.x == 1;\n");
    o.w("  bool stereoDraw = (rflags & 1u) != 0u && vrFlags.x == 2;\n");
    o.w("  vec4 clipPos;\n");
    o.w("  if (vrDraw && (rflags & 2u) != 0u) {\n");
    // A sky is modelled around the game camera, which is metres away from
    // the eye in the diorama: draw it around the eye, at the far plane.
    o.w("    vec4 e = vec4(dot(vrView[0].xyz, pos), dot(vrView[1].xyz, pos), dot(vrView[2].xyz, pos), 0.0);\n");
    o.w("    clipPos = vec4(dot(vrProj[0], e), dot(vrProj[1], e), 0.0, dot(vrProj[3], e));\n");
    o.w("    clipPos.z = clipPos.w * 0.99999;\n");
    if (u.cutaway) o.w("    v_eye = vec3(0.0, 0.0, 1.0e6);  // never cut\n");
    o.w("  } else if (vrDraw) {\n");
    o.w("    vec4 e = vec4(dot(vrView[0], v4), dot(vrView[1], v4), dot(vrView[2], v4), 1.0);\n");
    o.w("    clipPos = vec4(dot(vrProj[0], e), dot(vrProj[1], e), dot(vrProj[2], e), dot(vrProj[3], e));\n");
    if (u.cutaway) o.w("    v_eye = e.xyz;\n");
    o.w("  } else {\n");
    // One picture of the flat screen's stereo pair: the game's own camera a
    // little to the side, its frustum sheared so what is vrStereo.y away
    // lands where it does without (the screen's own depth).  Nearer things
    // shift the other way, at most vrStereo.z times as far as the farthest
    // ones; a sky belongs with the farthest.
    o.w("    if (stereoDraw) c.x += vrStereo.x * min(((rflags & 2u) != 0u ? 0.0 : vrStereo.y) - c.w, vrStereo.z * c.w);\n");
    if (u.hud) {
        // The HUD of a stereo pair: shifted as a whole (it floats before
        // the screen, in front of the scene it is drawn over) and widened by
        // as much, so what covers the whole picture (a fade) still does.
        // The pointer's cursor on the scene goes where what it points at is
        // drawn instead (on menus it stays with the HUD).
        o.w("    if (vrFlags.x == 2) {\n");
        o.w("      if ((rflags & 4u) != 0u && vrStereo2.y == 0.0) c.x += vrStereo2.x * c.w;\n");
        o.w("      else c.x = c.x * (1.0 + abs(vrStereo.w)) + vrStereo.w * c.w;\n");
        o.w("    }\n");
    }
    o.w("    clipPos = vec4(c.x, c.y, depth * 2.0 * 1.0 - c.w, c.w);\n");
    if (u.cutaway) o.w("    v_eye = vec3(0.0, 0.0, 1.0e6);  // never cut\n");
    o.w("  }\n");
    o.w("  gl_Position = clipPos;\n");
    // Undefined unless written (GX points are rare; Mali draws nothing or
    // garbage sizes without it).
    o.w("  gl_PointSize = 1.0;\n");

    // Color channels.
    o.w("  vec4 vclr0 = %s;\n", (f & VF_CLR0) ? "a_clr0" : "vec4(1.0)");
    o.w("  vec4 vclr1 = %s;\n", (f & VF_CLR1) ? "a_clr1" : "vec4(1.0)");
    for (int c = 0; c < 2; c++) {
        if ((uint32_t)c >= u.numChans) {
            o.w("  v_clr%d = vclr%d;\n", c, c);
            continue;
        }
        for (int alpha = 0; alpha < 2; alpha++) {
            uint32_t ctrl = alpha ? u.alphaCtrl[c] : u.colorCtrl[c];
            const char* sw = alpha ? "a" : "rgb";
            char mat[64], amb[64];
            snprintf(mat, sizeof(mat), (ctrl & 1) ? "vclr%d.%s" : "matColor[%d].%s", c, sw);
            snprintf(amb, sizeof(amb), ((ctrl >> 6) & 1) ? "vclr%d.%s" : "ambColor[%d].%s", c, sw);
            bool enable = (ctrl >> 1) & 1;
            uint32_t mask = ((ctrl >> 2) & 15) | (((ctrl >> 11) & 15) << 4);
            if (!enable) {
                o.w("  v_clr%d.%s = %s;\n", c, sw, mat);
                continue;
            }
            uint32_t diffuse = (ctrl >> 7) & 3;
            bool atten = (ctrl >> 9) & 1;
            bool spot = (ctrl >> 10) & 1;
            o.w("  {\n    %s lit = %s;\n", alpha ? "float" : "vec3", amb);
            for (int l = 0; l < 8; l++) {
                if (!(mask & (1u << l))) {
                    continue;
                }
                o.w("    {\n");
                o.w("      vec4 lcol = L(%d); vec3 lcos = L(%d).xyz; vec3 ldist = L(%d).xyz;\n", l * 5, l * 5 + 1, l * 5 + 2);
                o.w("      vec3 lpos = L(%d).xyz; vec3 ldirv = L(%d).xyz;\n", l * 5 + 3, l * 5 + 4);
                // A light at the view space origin sits on the camera (the
                // characters' rim light, LightFunction::loadActorLightInfo):
                // in VR it belongs on the eye, or rims land mid-face.
                o.w("      if (vrDraw && lpos == vec3(0.0)) lpos = vrEyePos.xyz;\n");
                // GX lighting, as Dolphin's LightingShaderGen has it.  A
                // specular light (attenuation without spot) is a direction
                // stored as a far away position.
                if (!atten) {
                    o.w("      vec4 dl = dirLen(lpos - pos, nrm);\n      vec3 ldir = dl.xyz;\n      float att = 1.0;\n");
                } else if (spot) {
                    o.w("      vec4 dl = dirLen(lpos - pos, nrm);\n      vec3 ldir = dl.xyz;\n      float d = dl.w;\n");
                    o.w("      float a = max(0.0, dot(ldir, ldirv));\n");
                    o.w("      float att = safeDiv(max(0.0, lcos.x + a * (lcos.y + a * lcos.z)), ldist.x + d * (ldist.y + d * ldist.z));\n");
                } else {
                    o.w("      vec3 ldir = dirLen(lpos, nrm).xyz;\n");
                    o.w("      float a = (dot(nrm, ldir) >= 0.0) ? max(0.0, dot(nrm, ldirv)) : 0.0;\n");
                    o.w("      float att = safeDiv(max(0.0, lcos.x + a * (lcos.y + a * lcos.z)), ldist.x + a * (ldist.y + a * ldist.z));\n");
                }
                // Garbage in, nothing out (never inf / NaN into the sum).
                o.w("      if (!(att >= 0.0) || !(att < 1.0e6)) att = 0.0;\n");
                const char* dif = diffuse == 0 ? "1.0" : diffuse == 1 ? "dot(ldir, nrm)" : "max(0.0, dot(ldir, nrm))";
                o.w("      lit += att * %s * lcol.%s;\n", dif, sw);
                o.w("    }\n");
            }
            o.w("    v_clr%d.%s = %s * clamp(lit, 0.0, 1.0);\n  }\n", c, sw, mat);
        }
    }

    // Texture coordinate generation.
    for (uint32_t i = 0; i < u.numTexGens; i++) {
        uint32_t info = u.texMtxInfo[i];
        bool stq = (info >> 1) & 1;
        bool abc1 = (info >> 2) & 1;
        uint32_t type = (info >> 4) & 7;
        uint32_t src = (info >> 7) & 31;
        o.w("  {\n");
        if (type == 2 || type == 3) {  // color texgen
            o.w("    v_tex%u = vec3(v_clr%u.rg, 1.0);\n  }\n", i, type - 2);
            continue;
        }
        if (type == 1) {  // emboss
            uint32_t esrc = (info >> 12) & 7, elight = (info >> 15) & 7;
            o.w("    vec3 ldir = dirLen(L(%u).xyz - pos, vec3(0.0, 0.0, 1.0)).xyz;\n", elight * 5 + 3);
            if (esrc < i) {
                o.w("    v_tex%u = v_tex%u + vec3(dot(ldir, tan), dot(ldir, bin), 0.0);\n  }\n", i, esrc);
            } else {
                o.w("    v_tex%u = vec3(0.0, 0.0, 1.0);\n  }\n", i);
            }
            continue;
        }
        const char* srcExpr;
        char buf[96];
        switch (src) {
        case 0:
            srcExpr = "vec4(a_pos, 1.0)";
            break;
        case 1:
            srcExpr = (f & VF_NRM) ? "vec4(a_nrm, 1.0)" : "vec4(0.0, 0.0, 1.0, 1.0)";
            break;
        case 3:
            srcExpr = (f & VF_NBT) ? "vec4(a_tan, 1.0)" : "vec4(0.0, 0.0, 1.0, 1.0)";
            break;
        case 4:
            srcExpr = (f & VF_NBT) ? "vec4(a_bin, 1.0)" : "vec4(0.0, 0.0, 1.0, 1.0)";
            break;
        default:
            if (src >= 5 && src <= 12 && (f & (VF_TEX0 << (src - 5)))) {
                snprintf(buf, sizeof(buf), "vec4(a_tex%u, 1.0, 1.0)", src - 5);
                srcExpr = buf;
            } else {
                srcExpr = "vec4(0.0, 0.0, 1.0, 1.0)";
            }
            break;
        }
        o.w("    vec4 src = %s;\n", srcExpr);
        if (!abc1) {
            o.w("    src.z = 1.0;\n");
        }
        // Matrix index: per-vertex or default.
        if (f & VF_TEXMTX) {
            o.w("    int midx = int((a_texmtx[%u] >> %uu) & 255u);\n", i / 4, (i % 4) * 8);
        } else {
            if (i < 4) {
                o.w("    int midx = int((mA >> %uu) & 63u);\n", 6 * (i + 1));
            } else {
                o.w("    int midx = int((mB >> %uu) & 63u);\n", 6 * (i - 4));
            }
        }
        o.w("    uint tr = slotRow(rb, %uu, midx);\n", (unsigned)kRecPosSlots);
        if (stq) {
            o.w("    vec3 t = mulRows(tr, src);\n");
        } else {
            o.w("    vec3 t = vec3(dot(FETCH_ROW(tr), src), dot(FETCH_ROW(tr + 1u), src), 1.0);\n");
        }
        if (u.dualTex) {
            uint32_t post = u.postMtxInfo[i];
            if ((post >> 8) & 1) {
                o.w("    t = dirLen(t, vec3(0.0, 0.0, 1.0)).xyz;\n");
            }
            o.w("    vec4 t4 = vec4(t, 1.0);\n");
            o.w("    uint pr = FETCH_REC(rb + %uu);\n", (unsigned)kRecPost + i);
            o.w("    t = mulRows(pr, t4);\n");
        }
        if ((u.screenTexGens >> i) & 1) {
            // A capture of the screen sampled where the vertex lands on it.
            // In VR (and in a stereo pair) the capture holds this eye's view:
            // use the eye's screen position (texture rows run top-down).
            o.w("    if (vrDraw || stereoDraw) t = vec3((clipPos.x + clipPos.w) * 0.5, (clipPos.w - clipPos.y) * 0.5, clipPos.w);\n");
        }
        o.w("    v_tex%u = t;\n  }\n", i);
    }
    o.w("}\n");
    return o.s;
}

// ---------------------------------------------------------------------------
// Fragment shader (TEV)
// ---------------------------------------------------------------------------
namespace {

// The TEV works on integers, like Dolphin's shaders: registers hold 11-bit
// signed values, texture / rasterized / konst inputs 8-bit ones.
const char* kColorIn[16] = {"prev.rgb", "prev.aaa", "c0.rgb", "c0.aaa", "c1.rgb", "c1.aaa", "c2.rgb", "c2.aaa",
                            "tex.rgb",  "tex.aaa",  "ras.rgb", "ras.aaa", "ivec3(255)", "ivec3(128)", "kc", "ivec3(0)"};
const char* kAlphaIn[8] = {"prev.a", "c0.a", "c1.a", "c2.a", "tex.a", "ras.a", "ka", "0"};
const char* kDest[4] = {"prev", "c0", "c1", "c2"};
const int kFrac[8] = {255, 223, 191, 159, 128, 96, 64, 32};  // konst 8/8 .. 1/8
const char kComp[4] = {'r', 'g', 'b', 'a'};

std::string konstColor(uint32_t sel) {
    char buf[64];
    if (sel < 8) {
        snprintf(buf, sizeof(buf), "ivec3(%d)", kFrac[sel]);
    } else if (sel >= 12 && sel < 16) {
        snprintf(buf, sizeof(buf), "konst[%u].rgb", sel - 12);
    } else if (sel >= 16) {
        snprintf(buf, sizeof(buf), "konst[%u].%c%c%c", (sel - 16) & 3, kComp[(sel - 16) >> 2], kComp[(sel - 16) >> 2], kComp[(sel - 16) >> 2]);
    } else {
        snprintf(buf, sizeof(buf), "ivec3(0)");
    }
    return buf;
}

std::string konstAlpha(uint32_t sel) {
    char buf[64];
    if (sel < 8) {
        snprintf(buf, sizeof(buf), "%d", kFrac[sel]);
    } else if (sel >= 16) {
        snprintf(buf, sizeof(buf), "konst[%u].%c", (sel - 16) & 3, kComp[(sel - 16) >> 2]);
    } else {
        snprintf(buf, sizeof(buf), "0");
    }
    return buf;
}

// A regular TEV operation, (d + bias +- lerp(a, b, c)) * scale, with the
// hardware's rounding: c runs 0..256, a scale above 1 is applied inside the
// lerp, and a rounding bias precedes the division by 256 (as in Dolphin).
std::string tevRegular(uint32_t bias, uint32_t sub, uint32_t scale, bool alpha) {
    static const char* biasStrAlpha[3] = {"", " + 128", " - 128"};
    static const char* biasStrColor[3] = {"", " + ivec3(128)", " - ivec3(128)"};
    static const char* scaleLeft[4] = {"", " << 1", " << 2", ""};
    static const char* scaleRight[4] = {"", "", "", " >> 1"};
    const char* biasOp = alpha ? biasStrAlpha[bias] : biasStrColor[bias];
    // Dolphin's tev_lerp_bias[2 * op + ((scale == Divide2) == alpha)].
    const char* lerpBias = "";
    if ((scale == 3) == alpha) {
        lerpBias = alpha ? (sub ? " + 127" : " + 128") : (sub ? " + ivec3(127)" : " + ivec3(128)");
    }
    const char* p = alpha ? "a" : "c";
    char buf[512];
    snprintf(buf, sizeof(buf),
             "(((%s_d%s)%s) %c (((((%s_a << 8) + (%s_b - %s_a) * (%s_c + (%s_c >> 7)))%s)%s) >> 8))%s", p,
             biasOp, scaleLeft[scale], sub ? '-' : '+', p, p, p, p, p, scaleLeft[scale], lerpBias, scaleRight[scale]);
    return buf;
}

std::string swapString(const ShaderUid& u, uint32_t table) {
    uint32_t lo = u.ksel[table * 2], hi = u.ksel[table * 2 + 1];
    std::string s = ".";
    s += kComp[lo & 3];
    s += kComp[(lo >> 2) & 3];
    s += kComp[hi & 3];
    s += kComp[(hi >> 2) & 3];
    return s;
}

std::string alphaTest(uint32_t func, const std::string& ref) {
    switch (func) {
    case 0:
        return "false";
    case 1:
        return "(a < " + ref + ")";
    case 2:
        return "(a == " + ref + ")";
    case 3:
        return "(a <= " + ref + ")";
    case 4:
        return "(a > " + ref + ")";
    case 5:
        return "(a != " + ref + ")";
    case 6:
        return "(a >= " + ref + ")";
    default:
        return "true";
    }
}

}  // namespace

std::string genFragmentShader(const ShaderUid& u, bool useVertexTextures) {
    Out o;
    o.w("#version 320 es\nprecision highp float;\nprecision highp int;\n");
    o.s += dataAccessDecls(true, useVertexTextures);
    o.s += kEyeBlock;
    // Texture units the TEV and indirect stages sample.
    uint32_t usedMaps = 0;
    for (uint32_t s = 0; s < u.numTevStages; s++) {
        uint32_t order = (u.tevOrder[s / 2] >> ((s & 1) * 12)) & 0xFFF;
        if ((order >> 6) & 1) usedMaps |= 1u << (order & 7);
    }
    for (uint32_t s = 0; s < u.numIndStages; s++) usedMaps |= 1u << ((u.iref >> (s * 6)) & 7);
    // highp samplers: a sampler's default precision is lowp, which Mali
    // honours by returning 16-bit floats.
    for (int i = 0; i < 8; i++) {
        if ((usedMaps >> i) & 1) o.w("layout(binding = %d) uniform highp sampler2D s_tex%d;\n", i, i);
    }
    o.w("flat in highp uint v_pix;\n");
    o.w("in vec4 v_clr0;\nin vec4 v_clr1;\n");
    for (uint32_t i = 0; i < u.numTexGens; i++) o.w("in vec3 v_tex%u;\n", i);
    o.w("in float v_gxz;\nout vec4 o_color;\n");
    o.w("vec2 projUv(vec3 t) { return (t.z != 0.0) ? t.xy / t.z : t.xy; }\n");
    o.w("int idot(ivec3 a, ivec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }\n");
    // The low 8 bits of each component, one scalar at a time: Mali's
    // compiler miscompiles a bitwise AND of integer vectors (Dolphin's
    // BUG_BROKEN_VECTOR_BITWISE_AND).
    o.w("ivec3 lo8(ivec3 v) { return ivec3(v.x & 255, v.y & 255, v.z & 255); }\n");
    // A texture sample / a 0..1 color as 8-bit integers.
    o.w("ivec4 to8(vec4 c) { return ivec4(floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5)); }\n");
    if (u.cutaway) {
        o.w("in vec3 v_eye;\n");
        o.w("const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);\n");
    }

    o.w("void main() {\n");
    // VR cutaway (see gpu::EyeView): dither out 3D geometry between the eye
    // and the player, and geometry right in front of the eye.  Only in the
    // cutaway variant: a discard costs Adreno its early depth rejection.
    if (u.cutaway) {
    o.w("  if (vrFocus.w > 0.0) {\n");
    o.w("    float len = length(vrFocus.xyz);\n");
    o.w("    float along = clamp(dot(v_eye, vrFocus.xyz) / len, 0.0, len);\n");
    o.w("    float d = length(v_eye - vrFocus.xyz * (along / len));\n");
    o.w("    float radius = mix(vrCut.x, vrCut.y, along / len);\n");
    o.w("    float cut = (1.0 - smoothstep(radius * 0.7, radius, d)) * (1.0 - smoothstep(len - vrCut.z - 0.06, len - vrCut.z, along));\n");
    o.w("    cut = max(cut, 1.0 - smoothstep(vrCut.w * 0.6, vrCut.w, length(v_eye)));\n");
    o.w("    ivec2 q = ivec2(gl_FragCoord.xy) & 3;\n");
    o.w("    if (cut > (kBayer[q.y * 4 + q.x] + 0.5) / 16.0) discard;\n");
    o.w("  }\n");
    }
    // Pixel state of the draw (struct PixelState), only the parts in use:
    // each is a fetch per pixel.  A TEV register is loaded when a stage
    // reads it or it is the output; a konst color when a stage selects it.
    uint32_t regsRead = 0, konstRead = 0;
    for (uint32_t s = 0; s < u.numTevStages; s++) {
        uint32_t cc = u.tevColor[s], ac = u.tevAlpha[s];
        uint32_t ksel = u.ksel[s / 2] >> ((s & 1) * 10);
        uint32_t kcsel = (ksel >> 4) & 31, kasel = (ksel >> 9) & 31;
        for (int k = 0; k < 4; k++) {
            uint32_t csel = (cc >> (k * 4)) & 15, asel = (ac >> (4 + k * 3)) & 7;
            if (csel < 8) regsRead |= 1u << (csel >> 1);
            if (asel < 4) regsRead |= 1u << asel;
            if (csel == 14 && kcsel >= 12) konstRead |= 1u << ((kcsel - (kcsel >= 16 ? 16 : 12)) & 3);
            if (asel == 6 && kasel >= 16) konstRead |= 1u << ((kasel - 16) & 3);
        }
        if (s == u.numTevStages - 1) regsRead |= (1u << ((cc >> 22) & 3)) | (1u << ((ac >> 22) & 3));
    }
    for (int i = 0; i < 4; i++) {
        if ((regsRead >> i) & 1) {
            o.w("  ivec4 %s = ivec4(floor(ST(v_pix + %du) * 255.0 + 0.5));\n", kDest[i], i);
        } else {
            o.w("  ivec4 %s = ivec4(0);\n", kDest[i]);
        }
    }
    o.w("  ivec4 konst[4];\n");
    for (int i = 0; i < 4; i++) {
        if ((konstRead >> i) & 1) {
            o.w("  konst[%d] = to8(ST(v_pix + %du));\n", i, 4 + i);
        } else {
            o.w("  konst[%d] = ivec4(0);\n", i);
        }
    }
    o.w("  vec4 alphaRef = ST(v_pix + 8u);\n");
    if (((u.fogSel >> 1) & 7) >= 2) {
        o.w("  vec4 fogColor = ST(v_pix + 9u);\n  vec4 fogParams = ST(v_pix + 10u);\n");
    }
    for (int i = 0; i < 8; i++) {
        if ((usedMaps >> i) & 1) o.w("  vec4 texSize%d = ST(v_pix + %du);\n", i, 12 + i);
    }
    if (u.numIndStages) {
        for (int i = 0; i < 6; i++) o.w("  vec4 indMtx%d = ST(v_pix + %du);\n", i, 20 + i);
    }
    o.w("  ivec4 tex = ivec4(0);\n  ivec4 ras = ivec4(0);\n");
    o.w("  const ivec3 comp16 = ivec3(1, 256, 0);\n  const ivec3 comp24 = ivec3(1, 256, 65536);\n");
    if (u.zTex) {
        o.w("  ivec4 ztex = ivec4(0);\n");
    }

    // Indirect texture lookups (per indirect stage): 8-bit S/T/U offsets from
    // the texture's A/B/G channels.
    for (uint32_t s = 0; s < u.numIndStages; s++) {
        uint32_t map = (u.iref >> (s * 6)) & 7, coord = (u.iref >> (s * 6 + 3)) & 7;
        uint32_t scale = (u.indScale[s / 2] >> ((s & 1) * 8)) & 0xFF;
        uint32_t scaleS = scale & 15, scaleT = (scale >> 4) & 15;
        if (coord < u.numTexGens) {
            o.w("  vec3 ind%u = floor(texture(s_tex%u, projUv(v_tex%u) / vec2(%.1f, %.1f), texSize%u.x).abg * 255.0 + 0.5);\n", s, map, coord,
                (double)(1u << (scaleS & 15)), (double)(1u << (scaleT & 15)), map);
        } else {
            o.w("  vec3 ind%u = vec3(0.0);\n", s);
        }
    }

    uint32_t lastColorDest = 0, lastAlphaDest = 0;
    for (uint32_t s = 0; s < u.numTevStages; s++) {
        uint32_t order = (u.tevOrder[s / 2] >> ((s & 1) * 12)) & 0xFFF;
        uint32_t map = order & 7, coord = (order >> 3) & 7, en = (order >> 6) & 1, chan = (order >> 7) & 7;
        uint32_t cc = u.tevColor[s], ac = u.tevAlpha[s];
        uint32_t ksel = u.ksel[s / 2] >> ((s & 1) * 10);
        uint32_t kcsel = (ksel >> 4) & 31, kasel = (ksel >> 9) & 31;
        uint32_t rswap = ac & 3, tswap = (ac >> 2) & 3;
        o.w("  // stage %u\n  {\n", s);

        // Texture coordinate (with optional indirect offset).
        std::string uv = "vec2(0.0)";
        if (coord < u.numTexGens) {
            char b[64];
            snprintf(b, sizeof(b), "projUv(v_tex%u)", coord);
            uv = b;
        }
        uint32_t ind = u.indCmd[s];
        if (u.numIndStages && ind) {
            uint32_t bt = ind & 3, bias = (ind >> 4) & 7, mtx = (ind >> 9) & 15;
            if (bt < u.numIndStages && (mtx & 3)) {
                uint32_t m = (mtx & 3) - 1;
                const char* bs = (bias & 1) ? "128.0" : "0.0";
                const char* bt2 = (bias & 2) ? "128.0" : "0.0";
                const char* bu = (bias & 4) ? "128.0" : "0.0";
                char b[512];
                snprintf(b, sizeof(b), "(%s + vec2(dot(indMtx%u.xyz, ind%u - vec3(%s, %s, %s)), dot(indMtx%u.xyz, ind%u - vec3(%s, %s, %s))) * texSize%u.zw)",
                         uv.c_str(), m * 2, bt, bs, bt2, bu, m * 2 + 1, bt, bs, bt2, bu, map);
                uv = b;
            }
        }
        if (en) {
            o.w("  tex = to8(texture(s_tex%u, %s, texSize%u.x)%s);\n", map, uv.c_str(), map, swapString(u, tswap).c_str());
            if (u.zTex) {
                o.w("  ztex = tex;\n");
            }
        } else {
            o.w("  tex = ivec4(0);\n");
        }
        const char* rasSrc = chan == 0 ? "v_clr0" : chan == 1 ? "v_clr1" : "vec4(0.0)";
        o.w("  ras = to8(%s%s);\n", rasSrc, swapString(u, rswap).c_str());
        // Inputs A, B and C are the low 8 bits of their source (a register's
        // sign and overflow bits are dropped); D keeps all 11 bits.
        auto colorIn = [&](uint32_t sel) -> std::string {
            return sel == 14 ? konstColor(kcsel) : std::string(kColorIn[sel]);
        };
        auto alphaIn = [&](uint32_t sel) -> std::string {
            return sel == 6 ? konstAlpha(kasel) : std::string(kAlphaIn[sel]);
        };
        uint32_t ca = (cc >> 12) & 15, cb = (cc >> 8) & 15, cc2 = (cc >> 4) & 15, cd = cc & 15;
        uint32_t aa = (ac >> 13) & 7, ab = (ac >> 10) & 7, ac2 = (ac >> 7) & 7, ad = (ac >> 4) & 7;
        o.w("  ivec3 c_a = lo8(%s);\n", colorIn(ca).c_str());
        o.w("  ivec3 c_b = lo8(%s);\n", colorIn(cb).c_str());
        o.w("  ivec3 c_c = lo8(%s);\n", colorIn(cc2).c_str());
        o.w("  ivec3 c_d = %s;\n", colorIn(cd).c_str());
        o.w("  int a_a = (%s) & 255;\n", alphaIn(aa).c_str());
        o.w("  int a_b = (%s) & 255;\n", alphaIn(ab).c_str());
        o.w("  int a_c = (%s) & 255;\n", alphaIn(ac2).c_str());
        o.w("  int a_d = %s;\n", alphaIn(ad).c_str());

        // Color combiner.
        {
            uint32_t bias = (cc >> 16) & 3, sub = (cc >> 18) & 1, clampv = (cc >> 19) & 1, scale = (cc >> 20) & 3, dest = (cc >> 22) & 3;
            std::string expr;
            if (bias != 3) {
                expr = tevRegular(bias, sub, scale, false);
            } else {
                static const char* cmp[8] = {
                    "((c_a.r > c_b.r) ? c_c : ivec3(0))",
                    "((c_a.r == c_b.r) ? c_c : ivec3(0))",
                    "((idot(c_a, comp16) > idot(c_b, comp16)) ? c_c : ivec3(0))",
                    "((idot(c_a, comp16) == idot(c_b, comp16)) ? c_c : ivec3(0))",
                    "((idot(c_a, comp24) > idot(c_b, comp24)) ? c_c : ivec3(0))",
                    "((idot(c_a, comp24) == idot(c_b, comp24)) ? c_c : ivec3(0))",
                    "(max(sign(c_a - c_b), ivec3(0)) * c_c)",
                    "((ivec3(1) - sign(abs(c_a - c_b))) * c_c)",
                };
                expr = std::string("c_d + ") + cmp[(scale << 1) | sub];
            }
            o.w("  %s.rgb = clamp(%s, ivec3(%s), ivec3(%s));\n", kDest[dest], expr.c_str(), clampv ? "0" : "-1024", clampv ? "255" : "1023");
            if (s == u.numTevStages - 1) {
                lastColorDest = dest;
            }
        }
        // Alpha combiner (its compare modes other than A8 test the color
        // inputs).
        {
            uint32_t bias = (ac >> 16) & 3, sub = (ac >> 18) & 1, clampv = (ac >> 19) & 1, scale = (ac >> 20) & 3, dest = (ac >> 22) & 3;
            std::string expr;
            if (bias != 3) {
                expr = tevRegular(bias, sub, scale, true);
            } else {
                static const char* cmp[8] = {
                    "((c_a.r > c_b.r) ? a_c : 0)",
                    "((c_a.r == c_b.r) ? a_c : 0)",
                    "((idot(c_a, comp16) > idot(c_b, comp16)) ? a_c : 0)",
                    "((idot(c_a, comp16) == idot(c_b, comp16)) ? a_c : 0)",
                    "((idot(c_a, comp24) > idot(c_b, comp24)) ? a_c : 0)",
                    "((idot(c_a, comp24) == idot(c_b, comp24)) ? a_c : 0)",
                    "((a_a > a_b) ? a_c : 0)",
                    "((a_a == a_b) ? a_c : 0)",
                };
                expr = std::string("a_d + ") + cmp[(scale << 1) | sub];
            }
            o.w("  %s.a = clamp(%s, %s, %s);\n", kDest[dest], expr.c_str(), clampv ? "0" : "-1024", clampv ? "255" : "1023");
            if (s == u.numTevStages - 1) {
                lastAlphaDest = dest;
            }
        }
        o.w("  }\n");
    }
    // The TEV's output is 8 bits wide: the upper bits are dropped, not
    // clamped (Mario's eyelids rely on it for their alpha test).
    o.w("  ivec4 outi = ivec4(lo8(%s.rgb), %s.a & 255);\n", kDest[lastColorDest], kDest[lastAlphaDest]);
    o.w("  vec4 outc = vec4(outi) / 255.0;\n");
    // Alpha test (on the 8-bit value).
    {
        uint32_t f0 = u.alphaFunc & 7, f1 = (u.alphaFunc >> 3) & 7, logic = (u.alphaFunc >> 6) & 3;
        if (!(f0 == 7 && f1 == 7)) {
            o.w("  float a = float(outi.a);\n");
            std::string t0 = alphaTest(f0, "alphaRef.x");
            std::string t1 = alphaTest(f1, "alphaRef.y");
            const char* ops[4] = {"&&", "||", "!=", "=="};
            o.w("  if (!((%s) %s (%s))) discard;\n", t0.c_str(), ops[logic], t1.c_str());
        }
    }
    // Z texture: the last texture sample, read as a 8/16/24-bit depth, plus
    // the bias, replaces or offsets the fragment depth.
    if (u.zTex) {
        uint32_t type = u.zTex & 3, op = (u.zTex >> 2) & 3;
        o.w("  {\n    ivec4 zt = ztex;\n");
        if (type == 0) {
            o.w("    int zv = zt.a;\n");
        } else if (type == 1) {
            o.w("    int zv = zt.r + zt.a * 256;\n");
        } else {
            o.w("    int zv = zt.r * 65536 + zt.g * 256 + zt.b;\n");
        }
        o.w("    zv += int(alphaRef.z);\n");
        if (op == 1) {
            o.w("    zv += int(gl_FragCoord.z * 16777215.0 + 0.5);\n");
        }
        o.w("    gl_FragDepth = float(zv & 0xFFFFFF) / 16777215.0;\n  }\n");
    }
    // Fog.
    // GX fog: ze = A / (B - (Z >> shift)) for perspective, A * Z for
    // orthographic (Z = 24-bit window depth), minus C; then the curve.
    uint32_t fogType = (u.fogSel >> 1) & 7, fogOrtho = u.fogSel & 1;
    if (fogType >= 2) {
        o.w("  {\n    float zc = floor(clamp(v_gxz, 0.0, 1.0) * 16777215.0);\n");
        if (fogOrtho) {
            o.w("    float ze = fogParams.x * zc / 16777216.0;\n");
        } else {
            o.w("    float ze = (fogParams.x * 16777216.0) / (fogParams.z - floor(zc / exp2(fogParams.w)));\n");
        }
        o.w("    float fz = clamp(ze - fogParams.y, 0.0, 1.0);\n");
        switch (fogType) {
        case 4:
            o.w("    fz = 1.0 - exp2(-8.0 * fz);\n");
            break;
        case 5:
            o.w("    fz = 1.0 - exp2(-8.0 * fz * fz);\n");
            break;
        case 6:
            o.w("    fz = exp2(-8.0 * (1.0 - fz));\n");
            break;
        case 7:
            o.w("    fz = exp2(-8.0 * (1.0 - fz) * (1.0 - fz));\n");
            break;
        default:
            break;
        }
        o.w("    outc.rgb = mix(outc.rgb, fogColor.rgb, fz);\n  }\n");
    }
    // Without an EFB alpha channel the blend still uses this alpha; the
    // renderer masks alpha writes instead.
    o.w("  o_color = clamp(outc, 0.0, 1.0);\n");
    // Debug views (PETARI_GLVIS=uv|tex): texture coordinates / raw texture 0.
    const char* vis = debugEnv("PETARI_GLVIS");
    if (vis && u.numTexGens > 0) {
        if (!strcmp(vis, "uv")) {
            o.w("  o_color = vec4(fract(projUv(v_tex0)), 0.0, 1.0);\n");
        } else if (!strcmp(vis, "red")) {
            o.w("  o_color = vec4(1.0, 0.0, 0.0, 1.0);\n");
        } else if (!strcmp(vis, "tex")) {
            if (usedMaps & 1) o.w("  o_color = vec4(texture(s_tex0, projUv(v_tex0)).rgb, 1.0);\n");
        }
    }
    o.w("}\n");
    return o.s;
}

}  // namespace gpu
