#!/usr/bin/env bash
# deploy.sh — build and deploy kek-mod DLL to Civ V DLC directory
set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────────
CIV5_DIR="/c/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V"
DLC_FOLDER="kek-mod"   # folder name inside Assets/DLC/
# ─────────────────────────────────────────────────────────────────────────────

MOD_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILT_DLL="$MOD_ROOT/CvGameCoreDLL_Expansion2/BuildOutput/VS2013_ModWin32/CvGameCoreDLL_Expansion2Win32Mod.dll"
BUILT_PDB="$MOD_ROOT/CvGameCoreDLL_Expansion2/BuildOutput/VS2013_ModWin32/CvGameCoreDLL_Expansion2Win32Mod.pdb"
DEPLOY_DIR="$CIV5_DIR/Assets/DLC/$DLC_FOLDER"

echo "=== kek-mod build + deploy ==="

# ── 1. Build ──────────────────────────────────────────────────────────────────
echo "[BUILD] Running build.bat..."
cmd //c "$MOD_ROOT/CvGameCoreDLL_Expansion2/build.bat"
echo ""

# ── 2. Verify built DLL exists ────────────────────────────────────────────────
if [[ ! -f "$BUILT_DLL" ]]; then
    echo "ERROR: DLL not found. Run build.bat first."
    echo "  Expected: $BUILT_DLL"
    exit 1
fi
echo "[OK] DLL found: $(du -h "$BUILT_DLL" | cut -f1)"

# ── 3. Copy DLL into mod root (rename to what Civ V expects) ─────────────────
cp "$BUILT_DLL" "$MOD_ROOT/CvGameCore_Expansion2.dll"
cp "$BUILT_PDB" "$MOD_ROOT/CvGameCore_Expansion2.pdb"
echo "[OK] DLL staged to mod root"

# ── 4. Ensure deploy dir exists ───────────────────────────────────────────────
if [[ ! -d "$CIV5_DIR" ]]; then
    echo "ERROR: Civ V directory not found: $CIV5_DIR"
    echo "  Edit CIV5_DIR at the top of this script."
    exit 1
fi
mkdir -p "$DEPLOY_DIR"

# ── 5. Copy mod files to DLC folder (exclude source/build dirs) ──────────────
# Clear old contents first so removed files don't linger
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"

for item in Art Fonts Mods Override UI tmp MPModsPack.Civ5Pkg \
            ui_check.bat CvGameCore_Expansion2.dll CvGameCore_Expansion2.pdb; do
    if [[ -e "$MOD_ROOT/$item" ]]; then
        cp -r "$MOD_ROOT/$item" "$DEPLOY_DIR/"
        echo "  copied: $item"
    fi
done

echo ""
echo "=== Deploy complete ==="
echo "  -> $DEPLOY_DIR"
