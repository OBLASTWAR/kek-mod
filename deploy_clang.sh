#!/usr/bin/env bash
# deploy_clang.sh — build kek-mod DLL with clang-cl and deploy to Civ V
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────────
PYTHON="C:/Users/mike/AppData/Local/Programs/Python/Python39/python.exe"
CIV5_DIR="/c/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V"
DLC_FOLDER="kek-mod"
# ──────────────────────────────────────────────────────────────────────────────

MOD_ROOT="$(cd "$(dirname "$0")" && pwd)"
CONFIG="release"
NO_BUILD=0

usage() {
    echo "Usage: $0 [--no-build] [--config release|debug]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)  NO_BUILD=1 ;;
        --config)    CONFIG="$2"; shift ;;
        *)           usage ;;
    esac
    shift
done

[[ "$CONFIG" == "release" || "$CONFIG" == "debug" ]] || usage

CONFIG_CAP="${CONFIG^}"   # Release / Debug
BUILT_DLL="$MOD_ROOT/clang-output/$CONFIG_CAP/CvGameCore_Expansion2.dll"
DEPLOY_DIR="$CIV5_DIR/Assets/DLC/$DLC_FOLDER"

echo "=== kek-mod clang build + deploy ($CONFIG) ==="

# ── 1. Build ──────────────────────────────────────────────────────────────────
if [[ $NO_BUILD -eq 0 ]]; then
    "$PYTHON" "$MOD_ROOT/build_clang.py" --config "$CONFIG"
    echo ""
fi

# ── 2. Verify DLL exists ──────────────────────────────────────────────────────
if [[ ! -f "$BUILT_DLL" ]]; then
    echo "ERROR: DLL not found: $BUILT_DLL"
    echo "       Run without --no-build to build first."
    exit 1
fi
echo "[OK] DLL: $(du -h "$BUILT_DLL" | cut -f1)  ($BUILT_DLL)"

# ── 3. Stage DLL at mod root (name Civ V expects) ────────────────────────────
cp "$BUILT_DLL" "$MOD_ROOT/CvGameCore_Expansion2.dll"
echo "[OK] DLL staged to mod root"

# ── 4. Validate Civ V dir ─────────────────────────────────────────────────────
if [[ ! -d "$CIV5_DIR" ]]; then
    echo "ERROR: Civ V directory not found: $CIV5_DIR"
    echo "       Edit CIV5_DIR at the top of this script."
    exit 1
fi

# ── 5. Deploy to DLC folder ───────────────────────────────────────────────────
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"

for item in Art Fonts Mods Override UI tmp MPModsPack.Civ5Pkg \
            ui_check.bat CvGameCore_Expansion2.dll; do
    if [[ -e "$MOD_ROOT/$item" ]]; then
        cp -r "$MOD_ROOT/$item" "$DEPLOY_DIR/"
        echo "  copied: $item"
    fi
done

echo ""
echo "=== Deploy complete -> $DEPLOY_DIR ==="
