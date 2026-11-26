#!/bin/bash

# =========================
# build_package.sh
# Build, ad-hoc sign, and package ll_externals
#
# No Apple Developer account, certificates or API keys needed. macOS externals
# get an ad-hoc signature (required for arm64 binaries to load on Apple
# Silicon); users install ll_externals.maxpack, which makes Max strip the
# quarantine attribute so Gatekeeper never checks the externals.
#
# Usage:
#   ./scripts/build_package.sh [--platform=mac|win|all] [--build-only | --no-build]
#
#   --platform=...  What to build and package (default: all)
#   --build-only    Only compile into externals/ (CI uses this per platform)
#   --no-build      Only package what is already in externals/ (CI packaging step)
#
# Environment overrides:
#   MACOS_ARCHS               default "arm64;x86_64" (universal)
#   MACOS_DEPLOYMENT_TARGET   default 10.15
#
# Outputs (repo root):
#   ll_externals.maxpack   Recommended download (Max installs it, removes quarantine)
#   ll_externals.zip       Same contents; ppooll's build downloads this one
# =========================

set -euo pipefail
shopt -s nullglob

PLATFORM="all"
STEP="full"
for arg in "$@"; do
    case $arg in
        --platform=*) PLATFORM="${arg#*=}" ;;
        --build-only) STEP="build-only" ;;
        --no-build)   STEP="no-build" ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

case "$PLATFORM" in
    mac|win|all) ;;
    *) echo "Error: --platform must be mac, win or all"; exit 1 ;;
esac

want_mac() { [ "$PLATFORM" = "mac" ] || [ "$PLATFORM" = "all" ]; }
want_win() { [ "$PLATFORM" = "win" ] || [ "$PLATFORM" = "all" ]; }

MACOS_ARCHS="${MACOS_ARCHS:-arm64;x86_64}"
# 10.15 is the oldest target where libc++ has std::filesystem. Lower it only if
# none of the externals need that.
MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}"

# =========================
# Path Setup
# =========================
REPO_ROOT="$(pwd)"
BUILD_DIR_MAC="$REPO_ROOT/build-mac"
BUILD_DIR_WIN="$REPO_ROOT/build-win"
PACKAGE_DIR="$REPO_ROOT/package"
EXTERNALS_DIR="$PACKAGE_DIR/externals"
HELP_DIR="$PACKAGE_DIR/help"
DOCS_DIR="$PACKAGE_DIR/docs"
ZIP_NAME="ll_externals.zip"
MAXPACK_NAME="ll_externals.maxpack"

echo "=== Platform: $PLATFORM | step: $STEP ==="

# =========================
# Build
# =========================
if [ "$STEP" != "no-build" ]; then
    if want_mac; then
        echo "=== Building for macOS ($MACOS_ARCHS, min macOS $MACOS_DEPLOYMENT_TARGET) ==="
        rm -rf "$BUILD_DIR_MAC"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR_MAC" \
            -DCMAKE_OSX_ARCHITECTURES="$MACOS_ARCHS" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"
        cmake --build "$BUILD_DIR_MAC" --config Release --parallel
    fi

    if want_win; then
        echo "=== Building for Windows (MinGW-w64 cross-compile) ==="
        rm -rf "$BUILD_DIR_WIN"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR_WIN" \
            -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/toolchains/WindowsToolchain.cmake"
        cmake --build "$BUILD_DIR_WIN" --config Release --parallel
    fi
fi

if [ "$STEP" = "build-only" ]; then
    echo "=== Build complete (externals/) ==="
    exit 0
fi

# =========================
# Assemble Package
# =========================
echo "=== Assembling package ==="
rm -rf "$PACKAGE_DIR" "$REPO_ROOT/$ZIP_NAME" "$REPO_ROOT/$MAXPACK_NAME"
mkdir -p "$EXTERNALS_DIR" "$HELP_DIR" "$DOCS_DIR"

if want_mac; then
    mxos=("$REPO_ROOT/externals/"*.mxo)
    if [ ${#mxos[@]} -eq 0 ]; then
        echo "Error: no .mxo externals found in externals/"
        exit 1
    fi
    cp -R "${mxos[@]}" "$EXTERNALS_DIR/"
fi

if want_win; then
    mxes=("$REPO_ROOT/externals/"*.mxe64)
    if [ ${#mxes[@]} -eq 0 ]; then
        echo "Error: no .mxe64 externals found in externals/"
        exit 1
    fi
    cp "${mxes[@]}" "$EXTERNALS_DIR/"
fi

cp "$REPO_ROOT/help/"*.maxhelp "$HELP_DIR/"
cp "$REPO_ROOT/docs/"*.maxref.xml "$DOCS_DIR/"
cp "$REPO_ROOT/package-info.json" "$PACKAGE_DIR/"
cp "$REPO_ROOT/README.md" "$PACKAGE_DIR/"
for f in "$REPO_ROOT"/LICENSE* "$REPO_ROOT"/CHANGELOG*; do
    cp "$f" "$PACKAGE_DIR/"
done

# =========================
# Ad-hoc sign macOS externals
# =========================
sign() {
    # Ad-hoc signature ("-"): no identity, no timestamp, no Apple account.
    codesign --force --sign - "$1"
}

is_macho() {
    file -b "$1" | grep -q "Mach-O"
}

pkg_mxos=("$EXTERNALS_DIR/"*.mxo)
if [ ${#pkg_mxos[@]} -gt 0 ]; then
    if ! command -v codesign >/dev/null; then
        echo "Error: signing .mxo externals requires macOS (codesign not found)."
        exit 1
    fi

    # Must happen before signing: codesign --strict rejects bundles with
    # extended-attribute "detritus" (quarantine, Finder info, ...).
    echo "=== Removing extended attributes ==="
    xattr -cr "$EXTERNALS_DIR" 2>/dev/null || true

    echo "=== Ad-hoc signing macOS externals ==="
    for mxo in "${pkg_mxos[@]}"; do
        echo "  → $(basename "$mxo")"
        # Nested Mach-O binaries first (helper dylibs, frameworks, tools)...
        while IFS= read -r -d '' bin; do
            if is_macho "$bin"; then
                echo "    signing inner binary: ${bin#"$mxo"/}"
                sign "$bin"
            fi
        done < <(find "$mxo" -type f -perm -111 -print0)
        # ...then the bundle itself, which seals everything inside it.
        sign "$mxo"
    done
fi

# =========================
# Final Package Zip / Maxpack
# =========================
# The archive's top-level folder must be ll_externals/ (ppooll's build expects it).
# -y keeps symlinks as symlinks (needed inside bundles with frameworks).
echo "=== Creating $ZIP_NAME ==="
STAGE_DIR="$(mktemp -d)"
cp -R "$PACKAGE_DIR" "$STAGE_DIR/ll_externals"
(cd "$STAGE_DIR" && zip -qry "$REPO_ROOT/$ZIP_NAME" ll_externals -x '*.DS_Store')
rm -rf "$STAGE_DIR"

# A .maxpack is the same zip with a different extension.
cp "$REPO_ROOT/$ZIP_NAME" "$REPO_ROOT/$MAXPACK_NAME"

echo "=== Build complete: $MAXPACK_NAME and $ZIP_NAME created ==="