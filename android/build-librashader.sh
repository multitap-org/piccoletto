#!/bin/sh
# librashader for the APK: deps/librashader's C API built for arm64 Android,
# Vulkan runtime only, into android/build/librashader/librashader.so (the APK
# carries it in lib/arm64-v8a; frontends-common's shader_chain loads it at run
# time). Same toolchain as the rest of the APK: the distro's clang + lld with
# the NDK's sysroot (hosts-common's android-toolchain.cmake does the same for
# CMake), and rustup's aarch64-linux-android target.
#
# Environment (all optional):
#   ANDROID_NDK_ROOT     the unpacked NDK (default ~/Android/ndk/android-ndk-r30)
#   MULTITAP_LLVM_ROOT   clang + lld (default /usr/lib/llvm-21)
#   CARGO                rustup's cargo (default ~/.cargo/bin/cargo)
#   MIN_API              the APK's minSdk (default 28)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT:-$HOME/Android/ndk/android-ndk-r30}
MULTITAP_LLVM_ROOT=${MULTITAP_LLVM_ROOT:-/usr/lib/llvm-21}
CARGO=${CARGO:-$HOME/.cargo/bin/cargo}
MIN_API=${MIN_API:-28}

sysroot="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
# libunwind (Rust's std links it) sits with clang's runtime in the NDK.
unwind_dir=$(dirname "$(find "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/lib/clang" -path '*linux/aarch64/libunwind.a' | head -1)")
for f in "$sysroot/usr/include/android/api-level.h" "$MULTITAP_LLVM_ROOT/bin/clang" "$CARGO" \
         "$root/deps/librashader/Cargo.toml"; do
    if [ ! -e "$f" ]; then
        echo "missing: $f" >&2
        exit 1
    fi
done

out="$here/build/librashader"
mkdir -p "$out"

# cargo calls the linker (and cc-rs the C compiler) by name: small wrappers
# that are clang for Android with the NDK's sysroot. Our --target goes last:
# cc-rs passes its own without the API level, and the last one wins.
cat > "$out/clang-android" <<EOF
#!/bin/sh
# Linking the library: libc++abi too (the static libc++ needs it; cc-rs only
# asks for libc++_static).
case " \$* " in
    *" -shared "*) exec "$MULTITAP_LLVM_ROOT/bin/clang" "\$@" --target=aarch64-linux-android$MIN_API --sysroot="$sysroot" -fuse-ld=lld -L"$unwind_dir" -lc++abi ;;
esac
exec "$MULTITAP_LLVM_ROOT/bin/clang" "\$@" --target=aarch64-linux-android$MIN_API --sysroot="$sysroot" -fuse-ld=lld -L"$unwind_dir"
EOF
cat > "$out/clang++-android" <<EOF
#!/bin/sh
exec "$MULTITAP_LLVM_ROOT/bin/clang++" "\$@" --target=aarch64-linux-android$MIN_API --sysroot="$sysroot" -fuse-ld=lld
EOF
chmod +x "$out/clang-android" "$out/clang++-android"

export CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$out/clang-android"
export CC_aarch64_linux_android="$out/clang-android"
export CXX_aarch64_linux_android="$out/clang++-android"
export AR_aarch64_linux_android="$MULTITAP_LLVM_ROOT/bin/llvm-ar"
# libc++ linked in (glslang, SPIRV-Cross are C++), so the APK needs no
# libc++_shared.so next to it.
export CXXSTDLIB_aarch64_linux_android=c++_static
# Build output outside the submodule (deps/ stays untouched).
export CARGO_TARGET_DIR="$out/target"

echo "== librashader (arm64 Android, Vulkan)"
cd "$root/deps/librashader"
"$CARGO" build --locked -p librashader-capi --profile optimized --target aarch64-linux-android \
    --no-default-features --features runtime-vulkan

cp "$CARGO_TARGET_DIR/aarch64-linux-android/optimized/liblibrashader_capi.so" "$out/librashader.so"
"$MULTITAP_LLVM_ROOT/bin/llvm-strip" "$out/librashader.so"
echo "built $out/librashader.so"
