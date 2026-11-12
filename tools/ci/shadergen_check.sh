#!/bin/bash
# Generates sample shaders with the renderer's shader generator and runs them
# through glslc (from the NDK's shader-tools): catches GLSL the generator
# writes that a strict GLSL ES 3.20 front end rejects, without a device.
#   tools/ci/shadergen_check.sh
set -eo pipefail
cd "$(dirname "$0")/../.."
source tools/env.sh
NDK=$(find_ndk) || { echo "Android NDK not found: set ANDROID_NDK_HOME" >&2; exit 1; }
GLSLC=$(ls "$NDK"/shader-tools/*/glslc | head -1)
WORK=build-shadergen-check
rm -rf "$WORK"
mkdir -p "$WORK"
${CXX:-c++} -std=c++20 -O1 -Iplatform/src/gx -o "$WORK/dump" tools/ci/shadergen_check.cpp platform/src/gx/gl_shadergen.cpp
(cd "$WORK" && ./dump)
fail=0
for f in "$WORK"/*.vert "$WORK"/*.frag; do
  if ! out=$("$GLSLC" --target-env=opengl -fauto-map-locations -fauto-bind-uniforms -o /dev/null "$f" 2>&1); then
    fail=$((fail + 1))
    echo "--- $f"
    echo "$out" | head -10
  fi
done
echo "shader generator check: $fail failure(s) in $(ls "$WORK"/*.vert "$WORK"/*.frag | wc -l) shaders"
rm -rf "$WORK"
[ "$fail" = 0 ]
