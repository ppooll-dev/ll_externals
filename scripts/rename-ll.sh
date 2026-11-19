#!/usr/bin/env bash
# Rename the ll_* externals to ll.* in two commits:
#   1) pure renames (git mv) -> git log --follow / blame keep working
#   2) content edits: class names, class_alias, CMake sources, help/refpages,
#      plus an init/ objectmappings file so old patchers still load.
#
# Usage:  ./rename-ll.sh [source-dir] [mappings-file]
#   source-dir     folder holding the ll_* folders (default: .)
#   mappings-file  path relative to the repo root (default: init/ll-objectmappings.txt)
set -euo pipefail

NAMES=(2dslider fastforward filewatchers mcwaveform menu number slishi trackpad zip)
SRC_DIR="${1:-.}"
MAPPINGS_REL="${2:-init/ll-objectmappings.txt}"

cd "$SRC_DIR"
ROOT="$(git rev-parse --show-toplevel)"

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "Working tree has uncommitted changes; commit or stash first." >&2
  exit 1
fi

# ---------- Commit 1: renames only ----------
for n in "${NAMES[@]}"; do
  old="ll_$n"; new="ll.$n"
  if [ ! -d "$old" ]; then echo "skip: $old/ not found"; continue; fi
  git mv "$old" "$new"
  for ext in c cpp; do
    if [ -f "$new/$old.$ext" ]; then git mv "$new/$old.$ext" "$new/$new.$ext"; fi
  done
  # help files and refpages anywhere in the repo
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    git -C "$ROOT" mv "$f" "$(dirname "$f")/$new.${f##*/$old.}"
  done < <(git -C "$ROOT" ls-files | grep -E "(^|/)$old\.(maxhelp|maxref\.xml)$" || true)
done
git commit -q -m "Rename ll_* externals to ll.* (renames only, no content changes)"
echo "commit 1 done: renames"

# ---------- Commit 2: content ----------
mkdir -p "$ROOT/$(dirname "$MAPPINGS_REL")"
MAPPINGS="$ROOT/$MAPPINGS_REL"
# (no comment line: Max reads init files as messages, so keep it to mappings only)
for n in "${NAMES[@]}"; do echo "max objectfile ll_$n ll.$n;"; done > "$MAPPINGS"

for n in "${NAMES[@]}"; do
  export OLD="ll_$n" NEW="ll.$n"
  d="$NEW"
  [ -d "$d" ] || continue

  # CMake: point at the renamed source files
  perl -pi -e 's/(?<![\w.])\Q$ENV{OLD}\E\.(c|cpp)\b/$ENV{NEW}.$1/g' "$d/CMakeLists.txt"

  for src in "$d/$NEW.c" "$d/$NEW.cpp"; do
    [ -f "$src" ] || continue
    # class name
    perl -pi -e 's/class_new\(\s*"\Q$ENV{OLD}\E"/class_new("$ENV{NEW}"/g' "$src"
    # user-facing strings like "ll_number: ..." or "ll_number - v1.5.1"
    perl -pi -e 's/"\Q$ENV{OLD}\E(?=[\s:-])/"$ENV{NEW}/g' "$src"
    # alias the old name right after class_register(...)
    perl -pi -e 's/^(\s*)class_register\(\s*(\w+)\s*,\s*(\w+)\s*\);.*$/$&\n$1class_alias($3, gensym("$ENV{OLD}"));/' "$src"

    grep -q "class_new(\"$NEW\"" "$src" || echo "CHECK $src: class_new(\"$NEW\" not found"
    grep -q "class_alias(.*\"$OLD\")" "$src" || echo "CHECK $src: class_alias not inserted"
  done

  # help patchers and refpages
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    perl -pi -e 's/(?<![\w.])\Q$ENV{OLD}\E(?!\w)/$ENV{NEW}/g' "$ROOT/$f"
  done < <(git -C "$ROOT" ls-files | grep -E "(^|/)$NEW\.(maxhelp|maxref\.xml)$" || true)
done

git -C "$ROOT" add -A
git commit -q -m "Use ll.* class names; alias and map old ll_* names for old patchers"
echo "commit 2 done: content"

echo
ALT="$(IFS='|'; echo "${NAMES[*]}")"
echo "Remaining quoted \"ll_*\" strings to review (expected: only the class_alias lines):"
git -C "$ROOT" grep -n -E "\"ll_($ALT)\"" || true