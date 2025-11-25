#!/bin/bash

# =========================
# generate_project.sh
# Generate project ll_externals (local dev)
# =========================

# ===== Load Environment Variables =====
if [ -f ".env" ]; then
    echo "Loading environment variables from .env file..."
    set -a
    source .env
    set +a
fi

# ===== Auto-detect host OS =====
UNAME="$(uname -s)"
case "$UNAME" in
    Darwin) HOST_OS="mac" ;;
    MINGW*|MSYS*|CYGWIN*) HOST_OS="win" ;;
    *) HOST_OS="unknown" ;;
esac

# ===== Parse --platform override =====
PLATFORM="$HOST_OS"  # default: build for host OS

for arg in "$@"; do
    case $arg in
        --platform=*)
            PLATFORM="${arg#*=}"
            shift
            ;;
    esac
done

echo "=== Host OS: $HOST_OS ==="
echo "=== Target platform: $PLATFORM ==="

# =========================
# Build directory
# =========================
REPO_ROOT="$(pwd)"
BUILD_DIR="$REPO_ROOT/build"

rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# =========================
# macOS Generator
# =========================
if [ "$PLATFORM" = "mac" ]; then
    if [ "$HOST_OS" != "mac" ]; then
        echo "❌ Cannot generate macOS Xcode project on non-mac host."
        exit 1
    fi

    echo "=== Generating Xcode project ==="
    cmake -G Xcode ..

    echo "Opening Xcode project..."
    open ll_externals.xcodeproj
    exit 0
fi

# =========================
# Windows Generator
# =========================
if [ "$PLATFORM" = "win" ]; then
    if [ "$HOST_OS" != "win" ]; then
        echo "❌ Cannot generate Visual Studio project on non-Windows host."
        exit 1
    fi

    echo "=== Generating Visual Studio Solution ==="
    cmake .. -G "Visual Studio 17 2022" -A x64

    echo "Opening Visual Studio solution..."
    explorer.exe ll_externals.sln 2>/dev/null || \
        cmd.exe /C start "" "ll_externals.sln"
    exit 0
fi

echo "❌ Unknown platform: $PLATFORM"
exit 1
