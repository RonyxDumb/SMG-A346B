// Host-side check of the shader generator: writes the vertex / fragment
// shaders of a spread of pseudo-random GX states (both data paths) and the
// driver self-test probes into the current directory, for a GLSL front end
// to validate (tools/ci/shadergen_check.sh runs the NDK's glslc over them).
#include "gl_shadergen.h"
#include "gx_record.h"
#include <stdio.h>
#include <stdlib.h>
using namespace gpu;
static void put(const char* name, const std::string& s) { FILE* f = fopen(name, "w"); fputs(s.c_str(), f); fclose(f); }
int main() {
    srand(1);
    int n = 0;
    for (int t = 0; t < 60; t++) {
        uint32_t bp[256] = {}, xf[256] = {};
        for (auto& v : bp) v = (uint32_t)rand() * 2654435761u >> 8;
        for (auto& v : xf) v = (uint32_t)rand() * 2654435761u >> 8;
        uint32_t stages = t % 16, ind = (t / 3) % 5, gens = (t * 7) % 9;
        bp[0] = (stages << 10) | (ind << 16) | ((t % 4) << 14);
        xf[0x3F] = gens; xf[0x09] = t % 3; xf[0x12] = t & 1;
        if (t < 8) { bp[0xF5] = (t & 15); }
        uint32_t flags = (uint32_t)rand() & 0x3FFF;
        if (flags & VF_NBT) flags |= VF_NRM;
        ShaderUid u = makeShaderUid(bp, xf, flags, t & 1);
        u.cutaway = (t % 5) == 0; u.screenTexGens = (t % 7) == 0 ? 3 : 0;
        for (int tex = 0; tex < 2; tex++) {
            char name[64];
            snprintf(name, sizeof(name), "s%02d_%d.vert", t, tex); put(name, genVertexShader(u, tex));
            snprintf(name, sizeof(name), "s%02d_%d.frag", t, tex); put(name, genFragmentShader(u, tex));
            n += 2;
        }
    }
    printf("%d shaders\n", n);
}
struct Probes { Probes() {
    put("p_quad.vert", genProbeQuadVs()); put("p_int.frag", genProbeIntFs());
    for (int t = 0; t < 2; t++) { char n[32]; snprintf(n, 32, "p_data%d.vert", t); put(n, genProbeDataVs(t)); snprintf(n, 32, "p_data%d.frag", t); put(n, genProbeDataFs(t)); }
} } sProbes;
