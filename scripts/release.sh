#!/bin/bash

# =========================
# scripts/release.sh
# Bump version, build, commit, and tag
# =========================

VERSION="$1"

if [ -z "$VERSION" ]; then
    echo "Usage: ./scripts/release.sh 1.0.0"
    exit 1
fi

# Update package-info.json
echo "Updating package-info.json version to $VERSION..."
sed -i '' "s/\"version\": \".*\"/\"version\": \"$VERSION\"/" package-info.json

# Build
echo "Building release package..."
./scripts/build_sign_package.sh --platform=all

# Git commit and tag
echo "Committing and tagging release..."
git add package-info.json
git commit -m "Release v$VERSION"
git tag "v$VERSION"

echo "Release v$VERSION created and tagged locally."
echo "Reminder: git push && git push --tags to publish."
