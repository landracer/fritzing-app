#!/usr/bin/env bash
#
# build-with-qt.sh — build Fritzing against a specific Qt without editing
# phoenix.pro by hand.
#
# Why this exists
# ---------------
# phoenix.pro hard-caps the Qt version:
#
#     QT_LEAST=6.5.3
#     QT_MOST=6.5.10
#     !versionAtMost(QT_VERSION, $${QT_MOST}):error("Use at most Qt version ...")
#
# So qmake refuses to even generate a Makefile on a newer Qt (e.g. the 6.8.3
# LTS the project is pinned to, or the 6.11 a contributor happens to have).
# This script lets you build/run the current tree against whatever Qt you
# point it at *without committing a cap change*: it raises QT_MOST only for
# the duration of the build and always restores phoenix.pro on exit (even on
# Ctrl-C or failure), so `git status` stays clean.
#
# It does NOT touch the committed cap, change any source, or relax the lower
# bound — it is purely a local convenience for "does this branch build and run
# on *my* Qt?".
#
# Usage
# -----
#   tools/build-with-qt.sh [-q <qt-bin-dir>] [-d <build-dir>] [-j <jobs>] [-c]
#
#   -q  Directory containing qmake6/qmake (e.g. ~/Qt/6.8.3/gcc_64/bin).
#       Default: whatever qmake6 (then qmake) is first on PATH.
#   -d  Out-of-tree build directory. Default: build-qtcheck (kept out of git;
#       the ~290 MB binary never lands next to your sources).
#   -j  Parallel jobs. Default: nproc.
#   -c  Configure only (run qmake, skip make). Handy to confirm the project
#       file accepts your Qt before committing to a full compile.
#
# Examples
#   # Build everything on the pinned 6.8.3 LTS:
#   tools/build-with-qt.sh -q ~/Qt/6.8.3/gcc_64/bin
#
#   # Just check qmake accepts a newer Qt, no compile:
#   tools/build-with-qt.sh -q /usr/lib64/qt6/bin -c
#
set -euo pipefail

QT_BIN=""
BUILD_DIR="build-qtcheck"
JOBS="$(nproc 2>/dev/null || echo 4)"
CONFIGURE_ONLY=0

while getopts "q:d:j:ch" opt; do
  case "$opt" in
    q) QT_BIN="$OPTARG" ;;
    d) BUILD_DIR="$OPTARG" ;;
    j) JOBS="$OPTARG" ;;
    c) CONFIGURE_ONLY=1 ;;
    h) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "Try: $0 -h" >&2; exit 2 ;;
  esac
done

# Resolve the repo root from this script's location so it works from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PRO="$REPO_ROOT/phoenix.pro"

[[ -f "$PRO" ]] || { echo "error: $PRO not found — run from inside the fritzing-app checkout" >&2; exit 1; }

# Put the chosen Qt first on PATH (if given) and pick a qmake.
if [[ -n "$QT_BIN" ]]; then
  export PATH="$QT_BIN:$PATH"
fi
QMAKE="$(command -v qmake6 || command -v qmake || true)"
[[ -n "$QMAKE" ]] || { echo "error: no qmake6/qmake on PATH (pass -q <qt-bin-dir>)" >&2; exit 1; }

QT_VER="$("$QMAKE" -query QT_VERSION)"
echo ">> qmake : $QMAKE"
echo ">> Qt    : $QT_VER"

# Read the committed cap so we can report and restore it precisely.
ORIG_CAP="$(sed -n 's/^QT_MOST=\(.*\)$/\1/p' "$PRO" | head -n1)"
[[ -n "$ORIG_CAP" ]] || { echo "error: could not find 'QT_MOST=' in phoenix.pro" >&2; exit 1; }
echo ">> cap   : QT_MOST=$ORIG_CAP (committed)"

# Restore phoenix.pro no matter how we leave (success, failure, Ctrl-C).
BUMPED=0
restore_pro() {
  if [[ "$BUMPED" == "1" ]]; then
    sed -i "s/^QT_MOST=.*/QT_MOST=$ORIG_CAP/" "$PRO"
    echo ">> restored phoenix.pro to QT_MOST=$ORIG_CAP"
  fi
}
trap restore_pro EXIT INT TERM

# Bump the cap only if the detected Qt is actually newer than the cap.
# (Sort the two versions; if the cap sorts first, Qt is newer and we bump.)
newest="$(printf '%s\n%s\n' "$ORIG_CAP" "$QT_VER" | sort -V | tail -n1)"
if [[ "$QT_VER" != "$ORIG_CAP" && "$newest" == "$QT_VER" ]]; then
  echo ">> Qt $QT_VER is above the cap; temporarily raising QT_MOST for this build"
  sed -i "s/^QT_MOST=.*/QT_MOST=$QT_VER/" "$PRO"
  BUMPED=1
fi

mkdir -p "$REPO_ROOT/$BUILD_DIR"
cd "$REPO_ROOT/$BUILD_DIR"

echo ">> qmake (CONFIG+=debug) in $BUILD_DIR ..."
"$QMAKE" "$PRO" CONFIG+=debug

if [[ "$CONFIGURE_ONLY" == "1" ]]; then
  echo ">> configure-only: Makefile generated successfully on Qt $QT_VER."
  exit 0
fi

echo ">> make -j$JOBS ..."
make -j"$JOBS"

echo ">> done. Binary is in $REPO_ROOT/$BUILD_DIR (not staged for git)."
