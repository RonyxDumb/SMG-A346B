#!/bin/bash
# Builds everything and packages the two phone APKs on Linux (CI or local):
#   out-mobile/GalaxyMobilePlus.apk   GalaxyMobile+ (mods enabled)
#   out-mobile/GalaxyMobile.apk       GalaxyMobile, with the Plus APK and the installer bundled
# The Linux counterpart of package_mobile_apk.ps1.  Needs ANDROID_HOME (build-tools,
# platforms/android-34+), ANDROID_NDK_HOME, a JDK 17+, cmake, ninja and python3.
#   tools/ci/build_apks.sh [--skip-native]
set -eo pipefail
cd "$(dirname "$0")/../.."
source tools/env.sh

SDK=$(find_sdk) || { echo "Android SDK not found: set ANDROID_HOME" >&2; exit 1; }
NDK=$(find_ndk) || { echo "Android NDK not found: set ANDROID_NDK_HOME" >&2; exit 1; }
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
PLATFORM=$( (ls -d "$SDK"/platforms/android-3[4-9]* "$SDK"/platforms/android-[4-9][0-9]* 2> /dev/null || true) | sort -V | head -1)
[ -n "$PLATFORM" ] || { echo "no platforms/android-34 or newer in $SDK" >&2; exit 1; }
STRIP=$(ndk_llvm_bin)/llvm-strip
CMAKE_BIN=$(find_cmake)
NINJA_BIN=$(find_ninja)
echo "SDK $SDK | build-tools $BT | platform $PLATFORM | NDK $NDK"

BUILD=build-mobile
WORK=build-apk
OUT=out-mobile
# A fixed debug key kept in the repository, so every CI build installs over
# the one before (it is not the original author's key: an install signed by
# another key has to be uninstalled first).
KEYSTORE=tools/ci/ci-debug.keystore

if [ "${1:-}" != "--skip-native" ]; then
  python3 tools/ci/fix_include_case.py --check
  bash ./build_mobile.sh
  mkdir -p build-installer/native
  if [ ! -f build-installer/native/build.ninja ]; then
    "$CMAKE_BIN" -S installer-mobile/jni -B build-installer/native -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
      -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
      -DCMAKE_BUILD_TYPE=Release
  fi
  "$NINJA_BIN" -C build-installer/native
fi

rm -rf "$WORK"
mkdir -p "$WORK/apk/lib/arm64-v8a" "$OUT"
LIBS=$WORK/apk/lib/arm64-v8a
tr -d '\r' < platform/android/wrap.sh > "$LIBS/wrap.sh"
for lib in libgame.so libmain.so libreserve.so; do
  "$STRIP" --strip-unneeded -o "$LIBS/$lib" "$BUILD/$lib"
done
"$STRIP" --strip-unneeded -o "$LIBS/libinstaller.so" build-installer/native/libinstaller.so
cp installer-mobile/vendor/nodtool/libnodtool.so "$LIBS/"
cp installer-mobile/vendor/python/lib/arm64-v8a/*.so "$LIBS/"

"$BT/aapt2" compile --dir platform/android/res -o "$WORK/res_platform.zip"
"$BT/aapt2" compile --dir installer-mobile/res -o "$WORK/res_installer.zip"

package_flavor() {  # manifest, output APK, assets directory
  local manifest=$1 out_apk=$2 assets=$3
  local dir=$WORK/flavor
  rm -rf "$dir"
  mkdir -p "$dir/obj" "$dir/dex" "$dir/r_gen"
  "$BT/aapt2" link -o "$dir/base.apk" --manifest "$manifest" -R "$WORK/res_platform.zip" -R "$WORK/res_installer.zip" --auto-add-overlay \
    --extra-packages com.galaxy.installer --java "$dir/r_gen" -A "$assets" -I "$PLATFORM/android.jar" \
    --min-sdk-version 30 --target-sdk-version 34
  javac -nowarn -source 17 -target 17 -cp "$PLATFORM/android.jar" -d "$dir/obj" \
    installer-mobile/src/com/galaxy/installer/CookerEngine.java installer-mobile/src/com/galaxy/installer/InstallerActivity.java \
    installer-mobile/src/com/galaxy/installer/GalaxyFileProvider.java installer-mobile/src/com/galaxy/installer/AppInstaller.java \
    "$dir/r_gen/com/galaxy/installer/R.java"
  "$BT/d8" --min-api 26 --lib "$PLATFORM/android.jar" --output "$dir/dex" $(find "$dir/obj" -name '*.class')
  python3 - "$dir/base.apk" "$dir/dex/classes.dex" "$WORK/apk" << 'PY'
import os, sys, zipfile
apk, dex, root = sys.argv[1:4]
with zipfile.ZipFile(apk, 'a', zipfile.ZIP_DEFLATED) as z:
    z.write(dex, 'classes.dex')
    for base, _, files in os.walk(os.path.join(root, 'lib')):
        for f in sorted(files):
            p = os.path.join(base, f)
            z.write(p, os.path.relpath(p, root).replace(os.sep, '/'))
PY
  "$BT/zipalign" -f -p 16 "$dir/base.apk" "$dir/aligned.apk"
  "$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android --out "$out_apk" "$dir/aligned.apk"
  rm -f "$out_apk.idsig"
  echo "packaged $out_apk ($(wc -c < "$out_apk") bytes)"
}

# The Plus APK first: the standard one carries it as an asset.
mkdir -p "$WORK/assets-plus" "$WORK/assets-vanilla"
cp -r installer-mobile/assets/. "$WORK/assets-plus/"
find "$WORK/assets-plus" -name '__pycache__' -prune -exec rm -rf {} +
rm -f "$WORK/assets-plus/GalaxyMobilePlus.apk"
package_flavor platform/mobile/AndroidManifest_plus.xml "$OUT/GalaxyMobilePlus.apk" "$WORK/assets-plus"
cp -r "$WORK/assets-plus/." "$WORK/assets-vanilla/"
cp "$OUT/GalaxyMobilePlus.apk" "$WORK/assets-vanilla/GalaxyMobilePlus.apk"
package_flavor platform/mobile/AndroidManifest.xml "$OUT/GalaxyMobile.apk" "$WORK/assets-vanilla"
rm -rf "$WORK"
ls -l "$OUT"/GalaxyMobile.apk "$OUT"/GalaxyMobilePlus.apk
