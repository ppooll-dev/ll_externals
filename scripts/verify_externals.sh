#!/bin/bash

# ================================
# verify_externals.sh
# Verifies all .mxo externals in the built package.
#   - codesign strict validation         (fails the run)
#   - no quarantine attribute            (fails the run)
#   - arm64 + x86_64 slices present      (warning only)
#   - minimum macOS version per slice    (shown for information)
#
# Gatekeeper (spctl) is deliberately not checked: ad-hoc signatures always
# fail it, which is harmless because .maxpack installs remove quarantine.
#
# Usage: ./scripts/verify_externals.sh [externals-dir]
# Exits non-zero if any external fails verification.
# ================================

set -uo pipefail

ROOT_DIR="${1:-$(pwd)/package/externals}"

if [ ! -d "$ROOT_DIR" ]; then
    echo "❌ $ROOT_DIR not found. Run ./scripts/build_package.sh first."
    exit 1
fi

echo "🔍 Verifying all .mxo bundles under: $ROOT_DIR"
echo

total=0
failures=0
warnings=0

while IFS= read -r -d '' mxo; do
    total=$((total + 1))
    name="${mxo#"$ROOT_DIR"/}"
    ok=1

    # ---- Signature
    if out=$(codesign --verify --deep --strict "$mxo" 2>&1); then
        signer=$(codesign -dvv "$mxo" 2>&1 | grep -m1 -E '^(Authority=|Signature=adhoc)' || true)
        sig_desc="${signer:-signed}"
    else
        ok=0
        sig_desc="INVALID"
    fi

    # ---- Quarantine (on any file inside the bundle)
    if xattr -r -l "$mxo" 2>/dev/null | grep -q "com.apple.quarantine"; then
        ok=0
        quarantine="QUARANTINED"
    else
        quarantine="no quarantine"
    fi

    # ---- Architectures of the main executable
    archs="?"
    minos="?"
    exe_name=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$mxo/Contents/Info.plist" 2>/dev/null || true)
    if [ -n "$exe_name" ] && [ -f "$mxo/Contents/MacOS/$exe_name" ]; then
        exe="$mxo/Contents/MacOS/$exe_name"
        archs=$(lipo -archs "$exe" 2>/dev/null || echo "?")
        minos=""
        for a in $archs; do
            v=$(otool -arch "$a" -l "$exe" 2>/dev/null | awk '/^ *minos / || /^ *version [0-9]/ {print $2; exit}')
            minos+="${minos:+ }$a:${v:-?}"
        done
    fi
    arch_note=""
    if [[ " $archs " != *" arm64 "* ]] || [[ " $archs " != *" x86_64 "* ]]; then
        warnings=$((warnings + 1))
        arch_note="  ⚠️  not universal"
    fi

    if [ $ok -eq 1 ]; then
        echo "✔ $name  [$sig_desc | $quarantine | $minos]$arch_note"
    else
        failures=$((failures + 1))
        echo "❌ $name  [$sig_desc | $quarantine | $minos]$arch_note"
        [ -n "${out:-}" ] && echo "$out" | sed 's/^/      /'
    fi
    out=""
done < <(find "$ROOT_DIR" -name "*.mxo" -type d -prune -print0)

echo
echo "------------------------------------------------------------"
echo "Checked $total externals: $failures failed, $warnings warnings."
echo "------------------------------------------------------------"

if [ "$total" -eq 0 ]; then
    echo "❌ No .mxo bundles found."
    exit 1
fi
[ "$failures" -eq 0 ]