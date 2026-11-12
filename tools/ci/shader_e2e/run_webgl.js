const { chromium } = require('playwright');
const fs = require('fs');
const d = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const fix = (s) => s.replace('#version 320 es', '#version 300 es').replace(/layout\(binding = \d+\) /g, '').replace(/layout\(std140, binding = \d+\)/g, 'layout(std140)');
for (let v = 0; v < 5; v++) { d['vs' + v] = fix(d['vs' + v]); d['fs' + v] = fix(d['fs' + v]); }
(async () => {
  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage();
  const res = await page.evaluate((d) => {
    const gl = document.createElement('canvas').getContext('webgl2');
    const out = [];
    const mk = (t, s) => { const sh = gl.createShader(t); gl.shaderSource(sh, s); gl.compileShader(sh); if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh) + s); return sh; };
    const kW = 2048;
    const data = [new Float32Array(d.rows), new Uint32Array(d.recs), new Float32Array(d.states), new Uint32Array(d.vtxRec)];
    for (let i = 0; i < 4; i++) {
      const integer = i == 1 || i == 3;
      gl.activeTexture(gl.TEXTURE8 + i); gl.bindTexture(gl.TEXTURE_2D, gl.createTexture());
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texImage2D(gl.TEXTURE_2D, 0, integer ? gl.RGBA32UI : gl.RGBA32F, kW, data[i].length / 4 / kW, 0, integer ? gl.RGBA_INTEGER : gl.RGBA, integer ? gl.UNSIGNED_INT : gl.FLOAT, data[i]);
    }
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gl.createTexture());
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    const T = [200, 100, 50, 255], V = [128, 255, 64, 255];
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(T));
    // vertices: pos (3 floats), color (4 bytes), tex (2 floats) = 24 bytes
    const vb = new ArrayBuffer(4 * 24), dv = new DataView(vb);
    [[-1, -1], [1, -1], [1, 1], [-1, 1]].forEach((p, i) => { dv.setFloat32(i * 24, p[0], true); dv.setFloat32(i * 24 + 4, p[1], true); dv.setFloat32(i * 24 + 8, 0, true);
      V.forEach((c, k) => dv.setUint8(i * 24 + 12 + k, c)); dv.setFloat32(i * 24 + 16, 0.5, true); dv.setFloat32(i * 24 + 20, 0.5, true); });
    gl.bindVertexArray(gl.createVertexArray());
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer()); gl.bufferData(gl.ARRAY_BUFFER, vb, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer()); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(5); gl.vertexAttribPointer(5, 4, gl.UNSIGNED_BYTE, true, 24, 12);
    gl.enableVertexAttribArray(7); gl.vertexAttribPointer(7, 2, gl.FLOAT, false, 24, 16);
    const ubo = gl.createBuffer(); gl.bindBuffer(gl.UNIFORM_BUFFER, ubo); gl.bufferData(gl.UNIFORM_BUFFER, new Float32Array(64), gl.STATIC_DRAW); gl.bindBufferBase(gl.UNIFORM_BUFFER, 4, ubo);
    const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t); gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, 4, 4);
    const fb = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fb); gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0); gl.viewport(0, 0, 4, 4);
    const lerp = (b, c, bias) => ((b * (c + (c >> 7))) + bias) >> 8;
    const s0 = [0, 1, 2].map((i) => lerp(T[i], V[i], 128)).concat([lerp(T[3], V[3], 0)]);
    const K1 = [255, 128, 0], C0 = [64, 128, 255];
    const s1 = [0, 1, 2].map((i) => Math.min(255, s0[i] + lerp(K1[i], C0[i], 128))).concat([s0[3] - 55]);
    // lit variants: material * (ambient + light), the light square on the surface
    const lit = [200, 100, 50].map((m) => Math.floor((m / 255) * ((40 + 128) / 255) * 255 + 0.5)).concat([255]);
    const expect = [s0, s1, lit, lit, lit];
    const litVao = gl.createVertexArray(); const texVao = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
    gl.bindVertexArray(litVao);
    const lv = new Float32Array([-1, -1, 0, 0, 0, 1, 1, -1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, -1, 1, 0, 0, 0, 1]);
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer()); gl.bufferData(gl.ARRAY_BUFFER, lv, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer()); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 3, gl.FLOAT, false, 24, 12);
    for (let v = 0; v < 5; v++) {
      gl.bindVertexArray(v < 2 ? texVao : litVao);
      const p = gl.createProgram(); gl.attachShader(p, mk(gl.VERTEX_SHADER, d['vs' + v])); gl.attachShader(p, mk(gl.FRAGMENT_SHADER, d['fs' + v]));
      gl.linkProgram(p);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
      gl.useProgram(p);
      [['s_rowTex', 8], ['s_recTex', 9], ['s_stateTex', 10], ['s_vtxRecTex', 11], ['s_tex0', 0]].forEach(([n, u]) => { const l = gl.getUniformLocation(p, n); if (l) gl.uniform1i(l, u); });
      const bi = gl.getUniformBlockIndex(p, 'EyeBlock'); if (bi != gl.INVALID_INDEX) gl.uniformBlockBinding(p, bi, 4);
      gl.uniform1ui(gl.getUniformLocation(p, 'u_firstOrdinal'), d.ordinal);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, null);
      const tex0 = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, tex0);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(T));
      gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT); gl.disable(gl.CULL_FACE); gl.disable(gl.DEPTH_TEST);
      gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_INT, 0);
      const px = new Uint8Array(4); gl.readPixels(1, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
      out.push('variant ' + v + ': got ' + Array.from(px) + ' expected ' + expect[v] + (Array.from(px).join() == expect[v].join() ? '  OK' : '  MISMATCH') + ' glerr ' + gl.getError());
    }
    return out.join('\n');
  }, d);
  console.log(res);
  await browser.close();
})();
