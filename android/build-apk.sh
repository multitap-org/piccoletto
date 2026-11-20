#!/bin/sh
# Builds piccoletto's Android APK without Gradle (Gradle's Android plugin only
# ships aapt2 for x86_64; the development machine is Linux arm64). The same
# steps as piccolo's android/build-apk.sh.
#
#   android/build-apk.sh      debug build -> android/build/piccoletto.apk
#   adb install -r android/build/piccoletto.apk
#   adb logcat -s piccoletto SDL
#
# Steps: native library (CMake, with hosts-common's Android toolchain), Java
# (javac), dex (d8), manifest and resources (aapt2: icon, TV banner), then the native library and dex go into
# the APK, which is aligned (zipalign) and signed with the debug key (apksigner).
#
# Tools, overridable from the environment:
#   ANDROID_NDK_ROOT  NDK, only its sysroot is used (default ~/Android/ndk/android-ndk-r30)
#   ANDROID_JAR       android.jar that Java compiles against (default /usr/lib/android-sdk/platforms/android-35/android.jar)
#   AAPT_ANDROID_JAR  android.jar that aapt2 links the manifest against (default .../android-32/android.jar):
#                     Debian's aapt2 (2.19) can't read the resource table of API 33+ jars.
#   D8_JAR            the r8/d8 jar (default ~/Android/r8/r8.jar)
#   JAVA_HOME         a JDK for javac/java (default /usr/lib/jvm/java-21-openjdk-arm64)
#   BUILD_TYPE        CMake build type (default Debug: Vulkan validation on if the layers are in the APK)

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build="$here/build"
native="$build/native"
apk_dir="$build/apk"

ANDROID_JAR=${ANDROID_JAR:-/usr/lib/android-sdk/platforms/android-35/android.jar}
AAPT_ANDROID_JAR=${AAPT_ANDROID_JAR:-/usr/lib/android-sdk/platforms/android-32/android.jar}
D8_JAR=${D8_JAR:-$HOME/Android/r8/r8.jar}
JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk-arm64}
BUILD_TYPE=${BUILD_TYPE:-Debug}
MIN_API=28

for f in "$ANDROID_JAR" "$AAPT_ANDROID_JAR" "$D8_JAR" "$JAVA_HOME/bin/javac"; do
    if [ ! -e "$f" ]; then
        echo "missing: $f" >&2
        exit 1
    fi
done

echo "== native library"
# piccoletto's own CMakeLists: with ANDROID set (the toolchain) it builds
# libmain.so instead of the program.
cmake -S "$root" -B "$native" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$root/deps/common/hosts-common/cmake/android-toolchain.cmake" \
    -DMULTITAP_BUILD_TESTS=OFF -DMULTITAP_WARNINGS_AS_ERRORS=ON >/dev/null
cmake --build "$native" --target piccoletto

echo "== java"
rm -rf "$apk_dir"
mkdir -p "$apk_dir/classes" "$apk_dir/dex" "$apk_dir/lib/arm64-v8a"
# SDL's Java side straight from the submodule, plus our activity.
find "$root/deps/SDL/android-project/app/src/main/java" "$here/java" -name '*.java' > "$apk_dir/sources.txt"
# Java 8 bytecode against android.jar as the boot class path (not the JDK's
# classes): only Android APIs can be used. d8 handles the rest.
"$JAVA_HOME/bin/javac" -nowarn -Xlint:-options -source 8 -target 8 -bootclasspath "$ANDROID_JAR" \
    -d "$apk_dir/classes" @"$apk_dir/sources.txt"

echo "== dex"
find "$apk_dir/classes" -name '*.class' > "$apk_dir/classes.txt"
"$JAVA_HOME/bin/java" -cp "$D8_JAR" com.android.tools.r8.D8 --debug --min-api "$MIN_API" \
    --lib "$ANDROID_JAR" --output "$apk_dir/dex" @"$apk_dir/classes.txt"

echo "== package"
# Resources: the adaptive launcher icon (with the themed monochrome layer) and
# the TV banner, straight from assets/branding/android/res (one copy: no
# exports kept here to fall behind).
aapt2 compile --dir "$root/assets/branding/android/res" -o "$apk_dir/res.zip"
# Assets: REMOTE PLAY's shader presets (deps/piccolo-shaders) with a list of
# their files, which the app copies out to its own folder (librashader reads
# plain files, not the APK).
rm -rf "$apk_dir/assets"
mkdir -p "$apk_dir/assets/shaders/presets"
(cd "$root/deps/piccolo-shaders" && find . -type f ! -path './.git*' | sed 's|^\./||' | sort) > "$apk_dir/presets.txt"
(cd "$root/deps/piccolo-shaders" && tar cf - -T "$apk_dir/presets.txt") | (cd "$apk_dir/assets/shaders/presets" && tar xf -)
cp "$apk_dir/presets.txt" "$apk_dir/assets/shaders/presets.txt"
aapt2 link --manifest "$here/AndroidManifest.xml" -I "$AAPT_ANDROID_JAR" -R "$apk_dir/res.zip" --auto-add-overlay \
    -A "$apk_dir/assets" -o "$apk_dir/unaligned.apk"
cp "$native/libmain.so" "$apk_dir/lib/arm64-v8a/libmain.so"
# librashader (android/build-librashader.sh), loaded at run time: without it,
# no shaders.
libs="lib/arm64-v8a/libmain.so"
if [ -f "$build/librashader/librashader.so" ]; then
    cp "$build/librashader/librashader.so" "$apk_dir/lib/arm64-v8a/librashader.so"
    libs="$libs lib/arm64-v8a/librashader.so"
else
    echo "note: no android/build/librashader/librashader.so (android/build-librashader.sh): the APK has no shaders"
fi
# Paths inside the APK are relative to the folder zip runs in.
(cd "$apk_dir/dex" && zip -q "$apk_dir/unaligned.apk" classes.dex)
# shellcheck disable=SC2086
(cd "$apk_dir" && zip -q "$apk_dir/unaligned.apk" $libs)
zipalign -f -p 4 "$apk_dir/unaligned.apk" "$apk_dir/aligned.apk"

echo "== sign"
# The standard Android debug key, the one Android Studio would create (shared
# with piccolo's builds).
keystore="$HOME/.android/debug.keystore"
if [ ! -f "$keystore" ]; then
    mkdir -p "$HOME/.android"
    "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$keystore" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi
JAVA_HOME="$JAVA_HOME" apksigner sign --ks "$keystore" --ks-pass pass:android --ks-key-alias androiddebugkey \
    --out "$build/piccoletto.apk" "$apk_dir/aligned.apk"
apksigner verify "$build/piccoletto.apk"
echo "built $build/piccoletto.apk"
