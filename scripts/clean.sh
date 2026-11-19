#!/bin/bash

# =========================
# scripts/clean.sh
# Remove build artifacts and temporary release files
# =========================

echo "Cleaning build artifacts and temporary files..."
rm -rf build-mac build-win package
rm -f ll_externals.zip ll_externals.maxpack mac-externals.tar win-externals.tar
echo "Cleanup complete."