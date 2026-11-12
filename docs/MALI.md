# Mali GPUs (Galaxy A34 5G and similar)

The renderer was written for Adreno (Quest).  Arm's Mali drivers differ in
ways that matter to it; this page lists what the renderer now does about
each, how to get test builds, and how to report what a device does.

None of this could be run on a Mali device while it was written: each point
below is a documented driver difference handled defensively, and the
start-up self-test reports what the actual driver does.

## What differs on Mali, and the answer to each

| Mali behaviour | What it can cause | Now |
|---|---|---|
| No (or too few) shader storage blocks in the vertex stage, none guaranteed in the fragment stage | shaders failed to link, or half the pipeline read a buffer the driver does not offer | a data path with **no storage buffer in any stage**: matrices, draw records and light / pixel states are fetched from four data textures (`texelFetch`) by both stages |
| Precision qualifiers are honoured: a sampler without one is `lowp`, and its samples come back as 16-bit floats | 24-bit depth copies lost their low bits (depth of field, shadows and other effects that compare against a depth copy) | every sampler is declared `highp` |
| Bitwise AND of integer vectors is miscompiled (Dolphin's `BUG_BROKEN_VECTOR_BITWISE_AND`) | wrong TEV colours / alpha (washed out actors, eyelids failing their alpha test) | the TEV masks one scalar at a time |
| A NaN / inf out of the lighting maths saturates to white | "bleached" characters when a light's unused fields held garbage | overflow-safe normalisation and guarded divisions; a non-finite attenuation contributes nothing |
| What a compositor does with a window whose alpha is not 1 is device specific | black or flickering 3D behind an intact HUD (the EFB's alpha is 0 over most of a scene) | the window's alpha is forced to 1 before every swap |
| `glBufferSubData` after orphaning is slow | frame time | one `glBufferData` per buffer and frame |

Per-pixel cost matters on a mid-range Mali: a fragment shader now fetches
only the pixel state it uses (typically 1-3 texels instead of 27).

## Start-up self-test

Nothing above is taken on trust.  When the renderer starts it

1. logs the GL vendor / renderer / version strings and the limits it relies on,
2. draws a small indexed quad that fetches known data through each data path
   (storage buffers, data textures) exactly as the game's shaders do, and
   reads the result back,
3. runs the TEV's integer forms with operands the compiler cannot fold,

and picks the data path from the results (textures first on Mali, storage
buffers first elsewhere).  The lines look like this (an example, not a
measurement):

```
gl: vendor 'ARM', renderer 'Mali-G68', version 'OpenGL ES 3.2 ...'
gl: self-test (storage buffers): vertex stage ok, fragment stage ok, GL error 0x0
gl: self-test (data textures): vertex stage ok, fragment stage ok, GL error 0x0
gl: self-test (integer ops): vector&vector BROKEN, vector&scalar ok, scalar masks ok, TEV lerp ok
gl: data path self-test: storage buffers ok, data textures ok -> using data textures
```

They go to `petari_mobile_log.txt` (Settings > share debug log, or
`Android/data/com.galaxy.mobile/files/`).  **If something still renders
wrong, that log is what tells why**: please attach it to the report, with a
screenshot.

## Renderer switches without a rebuild

Create `Android/data/com.galaxy.mobile/files/gpu_options.txt` (for
GalaxyMobile+: `com.galaxy.mobile.plus`) with `NAME=VALUE` lines; they are
applied at the next start.

| Line | Effect |
|---|---|
| `PETARI_VTF=1` | force the data-texture path |
| `PETARI_VTF=0` | force the storage-buffer path |
| `PETARI_GLVIS=tex` | show texture 0 unlit (isolates TEV / lighting problems) |
| `PETARI_GLVIS=uv` | show texture coordinates |
| `PETARI_GLSRC=1` | log the source of every generated shader |
| `PETARI_PERFLOG=1` | log frame statistics |

## Test builds

Every push builds both APKs with GitHub Actions
(`.github/workflows/android-apk.yml`):

- the run's **Artifacts** (Actions tab > Build Android APKs > a run), or
- the rolling pre-release `build-<branch>` under Releases (direct link, no
  login needed on the phone).

These builds are signed with the CI debug key (`tools/ci/ci-debug.keystore`),
not the original author's.  Android refuses to install one over a
GalaxyMobile signed with another key: that one has to be uninstalled first,
**which deletes its save files** (they are in the app's private storage; the
game data in `/sdcard` stays).  From then on CI builds update each other.

To build locally on Linux: `tools/ci/build_apks.sh` (needs `ANDROID_HOME`,
`ANDROID_NDK_HOME`, a JDK 17+, cmake, ninja, python3).
`tools/ci/shadergen_check.sh` runs a spread of generated shaders through the
NDK's `glslc`.
