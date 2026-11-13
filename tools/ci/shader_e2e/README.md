# Generated shaders, run for real (no phone needed)

`gen_case.cpp` asks the renderer's shader generator for the vertex and
fragment shaders of five small GX states (two TEV setups, three lit
channels: plain, spot and specular attenuation) on the data-texture path (the one
used on Mali) and writes them, with a hand-built frame (draw record, matrix
rows, projection, pixel state), to `case.json`.  `run_webgl.js` draws that
frame in headless Chromium (WebGL2 on SwiftShader; the shaders only need
their `#version` and `layout(binding)` adapted) and compares the pixel with
the TEV result computed on the CPU.

```
c++ -std=c++20 -Iplatform/src/gx -o /tmp/gen_case tools/ci/shader_e2e/gen_case.cpp platform/src/gx/gl_shadergen.cpp
(cd /tmp && ./gen_case)
node tools/ci/shader_e2e/run_webgl.js /tmp/case.json     # needs the playwright package
```

Expected output:

```
variant 0: got 101,100,13,255 expected 101,100,13,255  OK glerr 0
variant 1: got 165,165,13,200 expected 165,165,13,200  OK glerr 0
variant 2: got 132,66,33,255 expected 132,66,33,255  OK glerr 0
variant 3: got 132,66,33,255 expected 132,66,33,255  OK glerr 0
variant 4: got 132,66,33,255 expected 132,66,33,255  OK glerr 0
```

This checks the generator's logic (record / matrix / state addressing, TEV
integer maths), not a Mali driver: that is what the start-up self-test in
`gl_renderer.cpp` is for.
