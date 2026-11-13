#!/bin/bash
# Packages build-mobile/*.so, the installer and the resources into the signed
# universal APK out-mobile/GalaxyAndroid.apk.  Run ./build_mobile.sh first
# (or tools/ci/build_apks.sh, which builds everything).
set -e
cd "$(dirname "$0")"
exec bash tools/ci/build_apks.sh --skip-native "$@"
