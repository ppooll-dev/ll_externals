#!/bin/bash

# =========================
# build_sign_package.sh
# Build, Code Sign, and Package ll_externals
# =========================

# ===== Load Environment Variables =====
if [ -f ".env" ]; then
    echo "Loading environment variables from .env file..."
    set -a
    source .env
    set +a
fi

# ===== Environment Variables Required =====
#   - LL_DEVELOPER_ID
#   - LL_NOTARIZATION_PROFILE

if [ -z "$LL_DEVELOPER_ID" ] || [ -z "$LL_NOTARIZATION_PROFILE" ]; then
    echo "Error: Required environment variables LL_DEVELOPER_ID or LL_NOTARIZATION_PROFILE not set."
    echo "Tip: Create a .env file or export these variables manually."
    exit 1
fi

# ===== Parse Platform Option =====
PLATFORM="all"  # Default platform

for arg in "$@"
do
    case $arg in
        --platform=*)
        PLATFORM="${arg#*=}"
        shift
        ;;
        *)
        ;;
    esac
done

echo "=== Building for platform: $PLATFORM ==="

# =========================
# Path Setup
# =========================
REPO_ROOT="$(pwd)"
BUILD_DIR_MAC="$REPO_ROOT/build-mac"
BUILD_DIR_WIN="$REPO_ROOT/build-win"
PACKAGE_DIR="$REPO_ROOT/package"
EXTERNALS_DIR="$PACKAGE_DIR/externals"
HELP_DIR="$PACKAGE_DIR/help"
ZIP_NAME="ll_externals.zip"

# Clean old builds
rm -rf "$BUILD_DIR_MAC" "$BUILD_DIR_WIN" "$PACKAGE_DIR" "$ZIP_NAME"

# =========================
# Build macOS
# =========================
if [ "$PLATFORM" = "mac" ] || [ "$PLATFORM" = "all" ]; then
    echo "=== Building for macOS ==="
    mkdir "$BUILD_DIR_MAC"
    cd "$BUILD_DIR_MAC"
    cmake ..
    cmake --build . --config Release
    cd "$REPO_ROOT"
fi

# =========================
# Build Windows
# =========================
if [ "$PLATFORM" = "win" ] || [ "$PLATFORM" = "all" ]; then
    echo "=== Building for Windows ==="
    mkdir "$BUILD_DIR_WIN"
    cd "$BUILD_DIR_WIN"
    cmake .. -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/toolchains/WindowsToolchain.cmake"
    cmake --build . --config Release
    cd "$REPO_ROOT"
fi

# =========================
# Assemble Package
# =========================
echo "=== Assembling package ==="
mkdir -p "$EXTERNALS_DIR" "$HELP_DIR"

# Copy built externals
echo "=== Copying externals ==="
cp -R "$REPO_ROOT/externals/"*.mxo "$EXTERNALS_DIR/" 2>/dev/null || true
cp "$REPO_ROOT/externals/"*.mxe64 "$EXTERNALS_DIR/" 2>/dev/null || true

# Copy help files and package-info
cp "$REPO_ROOT/help/"*.maxhelp "$HELP_DIR/"
cp "$REPO_ROOT/package-info.json" "$PACKAGE_DIR/"

# =========================
# Code Sign and Notarize macOS externals
# =========================
if [ "$PLATFORM" = "mac" ] || [ "$PLATFORM" = "all" ]; then
    echo "=== Signing macOS externals ==="
    cd "$EXTERNALS_DIR"
    for mxo in *.mxo; do
        [ -e "$mxo" ] || continue
        echo "Signing $mxo"
        codesign --force --deep -s "$LL_DEVELOPER_ID" "$mxo" -v --timestamp
    done

    echo "=== Preparing notarization zip ==="
    cd "$PACKAGE_DIR/.."
    zip -r notarize_mac.zip package/

    echo "=== Submitting for notarization ==="
    NOTARIZATION_INFO=$(xcrun notarytool submit notarize_mac.zip --keychain-profile "$LL_NOTARIZATION_PROFILE" --wait --output-format json)
    NOTARIZATION_ID=$(echo "$NOTARIZATION_INFO" | jq -r '.id')

    if [ -z "$NOTARIZATION_ID" ]; then
        echo "Failed to submit for notarization."
        exit 1
    fi

    echo "=== Stapling notarization tickets ==="
    cd "$EXTERNALS_DIR"
    for mxo in *.mxo; do
        [ -e "$mxo" ] || continue
        xcrun stapler staple "$mxo"
    done

    echo "=== Verifying macOS signatures ==="
    for mxo in *.mxo; do
        [ -e "$mxo" ] || continue
        codesign --verify --deep --strict --verbose=2 "$mxo"
        spctl --assess --type execute --verbose=2 "$mxo"
    done

    echo "=== macOS signing and notarization complete ==="
    rm "$PACKAGE_DIR/../notarize_mac.zip"
fi

# =========================
# Final Package Zip
# =========================
echo "=== Creating final release zip ==="
cd "$PACKAGE_DIR/.."
zip -r "$ZIP_NAME" package/

echo "=== Build complete: $ZIP_NAME created ==="
