#include "gl_shadergen.h"
#include "gx_record.h"
#include <stdio.h>
#include <vector>
using namespace gpu;
static void jarr(FILE* f, const char* name, const std::vector<double>& v) { fprintf(f, "\"%s\":[", name); for (size_t i = 0; i < v.size(); i++) fprintf(f, "%s%.9g", i ? "," : "", v[i]); fprintf(f, "],\n"); }
static void jstr(FILE* f, const char* name, const std::string& s) { fprintf(f, "\"%s\":\"", name); for (char c : s) { if (c == '\n') fputs("\\n", f); else if (c == '"') fputs("\\\"", f); else if (c == '\\') fputs("\\\\", f); else fputc(c, f); } fprintf(f, "\",\n"); }
int main() {
    uint32_t bp[256] = {}, xf[256] = {};
    xf[0x3F] = 1; xf[0x09] = 1; xf[0x0E] = 1; xf[0x10] = 1; xf[0x40] = 5u << 7;
    bp[0x00] = 0; bp[0x28] = 0x40;
    bp[0xC0] = (15u << 12) | (8u << 8) | (10u << 4) | 15u | (1u << 19);
    bp[0xC1] = (7u << 13) | (4u << 10) | (5u << 7) | (7u << 4) | (1u << 19);
    bp[0xF6] = 4; bp[0xF7] = 14; bp[0xF3] = (7u | (7u << 3)) << 16;
    // second variant: stage 2 adds konst K1 color scaled, reads c0 register
    uint32_t flags = VF_CLR0 | VF_TEX0;
    FILE* f = fopen("case.json", "w");
    fprintf(f, "{\n");
    for (int variant = 0; variant < 2; variant++) {
        if (variant == 1) {
            bp[0x00] = 1u << 10;  // two stages
            bp[0x28] = 0x40 | (0x40u << 12);
            // stage 1: color = prev + konst(K1.rgb) * c0.rgb ; a = konst, b = c0? use a=ZERO b=KONST c=C0 d=PREV
            bp[0xC2] = (15u << 12) | (14u << 8) | (2u << 4) | 0u | (1u << 19);
            // alpha = prev.a - (konst K2.a) : a=ZERO(7), b=KONST(6), c=one? use a = konst, b = zero, c = zero -> lerp = a; d = prev, sub
            bp[0xC3] = (6u << 13) | (7u << 10) | (7u << 7) | (0u << 4) | (1u << 18) | (1u << 19);
            // ksel for stage 1: bits (10..) of ksel[0]: kcsel at >>4, kasel >>9 after shifting by 10
            bp[0xF6] = 4u | ((13u << 4 | (30u << 9)) << 10);  // color K1.rgb (13), alpha K2.a (16 + 2 + 3*4 = 30)
        }
        ShaderUid u = makeShaderUid(bp, xf, flags, false);
        char n[32];
        snprintf(n, 32, "vs%d", variant); jstr(f, n, genVertexShader(u, true));
        snprintf(n, 32, "fs%d", variant); jstr(f, n, genFragmentShader(u, true));
    }
    // Lit variants: one light on a channel whose material / ambient come from
    // the registers; the TEV passes the rasterized color through.
    // 2: no attenuation, 3: spot attenuation, 4: specular attenuation.
    for (int variant = 2; variant < 5; variant++) {
        uint32_t lbp[256] = {}, lxf[256] = {};
        lxf[0x3F] = 0; lxf[0x09] = 1;
        uint32_t ctrl = (1u << 1) | (1u << 2) | ((variant == 4 ? 0u : 2u) << 7);
        if (variant >= 3) ctrl |= 1u << 9;
        if (variant == 3) ctrl |= 1u << 10;
        lxf[0x0E] = ctrl; lxf[0x10] = 0;
        lbp[0xC0] = (15u << 12) | (15u << 8) | (15u << 4) | 10u | (1u << 19);
        lbp[0xC1] = (7u << 13) | (7u << 10) | (7u << 7) | (5u << 4) | (1u << 19);
        lbp[0xF6] = 4; lbp[0xF7] = 14; lbp[0xF3] = (7u | (7u << 3)) << 16;
        ShaderUid u = makeShaderUid(lbp, lxf, VF_NRM, false);
        char n[32];
        snprintf(n, 32, "vs%d", variant); jstr(f, n, genVertexShader(u, true));
        snprintf(n, 32, "fs%d", variant); jstr(f, n, genFragmentShader(u, true));
    }
    const unsigned kW = 2048;
    std::vector<double> rows(kW * 4, 0), recs(kW * 4 * 2, 0), states(kW * 4, 0), vtxRec(kW * 4 * 2, 9);
    // rows: identity 3x4 at row 10 (position) and row 20 (texture)
    for (int base : {10, 20}) for (int r = 0; r < 3; r++) rows[(base + r) * 4 + r] = 1.0;
    const unsigned rec = 3, rb = rec * kRecWords, lbase = 100, pjbase = 200, pix = 300, ordinal = 5000;
    recs[rb + kRecPosSlots + 0] = 10; recs[rb + kRecPosSlots + 20] = 20; recs[rb + kRecNrmSlots] = 10;
    recs[rb + kRecLights] = lbase; recs[rb + kRecProj] = pjbase; recs[rb + kRecPixel] = pix;
    recs[rb + kRecMtxIdxA] = 0u | (60u << 6); recs[rb + kRecMtxIdxB] = 0; recs[rb + kRecFlags] = 0;
    for (int i = 0; i < 4; i++) vtxRec[ordinal + i] = rec;
    // projection: x, y pass through, z -> 0, w = 1; depth 0.5
    states[(pjbase + 0) * 4 + 0] = 1; states[(pjbase + 1) * 4 + 1] = 1; states[(pjbase + 3) * 4 + 3] = 1;
    states[(pjbase + 4) * 4 + 1] = 16777215.0 * 0.5;
    // pixel state: c0 = (64, 128, 255, 255)/255, K1 = (255, 128, 0, 255)/255, K2.a = 55/255; texSize0
    double c0[4] = {64 / 255.0, 128 / 255.0, 255 / 255.0, 1.0}, k1[4] = {1.0, 128 / 255.0, 0, 1.0}, k2[4] = {0, 0, 0, 55 / 255.0};
    for (int i = 0; i < 4; i++) { states[(pix + 1) * 4 + i] = c0[i]; states[(pix + 5) * 4 + i] = k1[i]; states[(pix + 6) * 4 + i] = k2[i]; }
    states[(pix + 12) * 4 + 0] = 0; states[(pix + 12) * 4 + 1] = 1; states[(pix + 12) * 4 + 2] = 1; states[(pix + 12) * 4 + 3] = 1;
    // Light 0: grey, 1000 units in front of the surface (as a specular light:
    // a direction, its half angle along the normal, a2 = 1); material and
    // ambient colors.
    double lcol[4] = {128 / 255.0, 128 / 255.0, 128 / 255.0, 1}, lcos[4] = {0, 0, 1, 0}, ldist[4] = {1, 0, 0, 0}, lpos[4] = {0, 0, 1000, 0}, ldir[4] = {0, 0, 1, 0};
    double mat[4] = {200 / 255.0, 100 / 255.0, 50 / 255.0, 1}, amb[4] = {40 / 255.0, 40 / 255.0, 40 / 255.0, 1};
    for (int i = 0; i < 4; i++) {
        states[(lbase + 0) * 4 + i] = lcol[i]; states[(lbase + 1) * 4 + i] = lcos[i]; states[(lbase + 2) * 4 + i] = ldist[i];
        states[(lbase + 3) * 4 + i] = lpos[i]; states[(lbase + 4) * 4 + i] = ldir[i];
        states[(lbase + 40) * 4 + i] = mat[i]; states[(lbase + 42) * 4 + i] = amb[i];
    }
    jarr(f, "rows", rows); jarr(f, "recs", recs); jarr(f, "states", states); jarr(f, "vtxRec", vtxRec);
    fprintf(f, "\"ordinal\":%u}\n", ordinal);
    fclose(f);
}
