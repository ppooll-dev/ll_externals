#!/bin/bash

# =========================
# scripts/release.sh
# Bump version, commit, and tag (no build!)
# =========================

VERSION="$1"

if [ -z "$VERSION" ]; then
    echo "Usage: ./scripts/release.sh 0.9.0"
    exit 1
fi

# Update version in package-info.json
echo "Updating package-info.json version to $VERSION..."
sed -i '' "s/\"version\": \".*\"/\"version\": \"$VERSION\"/" package-info.json

# Commit version bump
git add package-info.json
git commit -m "Release v$VERSION"
git tag "v$VERSION"

echo "Release v$VERSION committed and tagged."
echo "Remember to push: git push && git push --tags"
