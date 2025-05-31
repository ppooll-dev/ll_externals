#!/bin/bash

# =========================
# scripts/release.sh
# Bump version, commit, and tag (no build!)
# =========================

set -e

VERSION=$(jq -r .version package-info.json)

if [ -z "$VERSION" ] || [ "$VERSION" = "null" ]; then
    echo "Error: Could not find version in package-info.json"
    exit 1
fi

git add package-info.json
git commit -m "Release v$VERSION"
git tag "v$VERSION"

echo "Committed and tagged version v$VERSION"