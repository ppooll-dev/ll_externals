#!/bin/bash
set -e

VERSION=$1

if [ -z "$VERSION" ]; then
    echo "Usage: ./bump_version.sh <version>"
    exit 1
fi

jq --arg v "$VERSION" '.version = $v' package-info.json > tmp.$$.json && mv tmp.$$.json package-info.json
echo "Updated package-info.json to version $VERSION"