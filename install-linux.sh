#!/usr/bin/env bash
# install-linux.sh -- install KEK Mod (+ EUI) into Civ V running under Proton.
#
#   ./install-linux.sh                 latest KEK Mod release + EUI
#   ./install-linux.sh --tag v2.0      a specific release tag
#   ./install-linux.sh --beta          newest release, prereleases included
#   ./install-linux.sh --no-eui        skip EUI (vanilla UI)
#   ./install-linux.sh --eui-zip FILE  EUI zip to install (UI_bc1 from CivFanatics)
#   ./install-linux.sh --civ-dir DIR   Civ V folder, if auto-detect misses it
#
# Linux equivalent of KekModInstaller.exe, which can't be used under Proton:
# it finds Steam through the Windows registry, and inside a Proton prefix that
# points at a stub Steam folder rather than the real game.
#
# KEK Mod replaces CvGameCore_Expansion2.dll, a Windows DLL, so the native
# Linux build of Civ V can't load it. In Steam: Civ V -> Properties ->
# Compatibility -> force a Proton version, then run this script.
#
# Steps: find the game -> install EUI into Assets/DLC/UI_bc1 (unless an EUI
# variant is already there) -> replace any "KEK Mod v*" folder with the chosen
# release -> run ui_check.sh (the Linux port of ui_check.bat) -> add Fish Map
# Script to Assets/Maps if missing -> clear the game's cache inside the
# Proton prefix.
#
# EUI has no stable download URL (see installer/EuiExtra.cs), so it is never
# fetched: an existing Assets/DLC/UI_bc1 is kept, else the zip comes from
# --eui-zip or this checkout's installer/UI_bc1.zip.
#
# Needs: bash, curl, python3.
set -euo pipefail

REPO="OBLASTWAR/kek-mod"
CIV_APPID=8930
CIV_NAME="Sid Meier's Civilization V"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TAG=""
WANT_BETA=0
WANT_EUI=1
CIV_DIR=""
EUI_ZIP="$SCRIPT_DIR/installer/UI_bc1.zip"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)     TAG="${2:?--tag needs a value}"; shift 2 ;;
        --beta)    WANT_BETA=1; shift ;;
        --no-eui)  WANT_EUI=0; shift ;;
        --civ-dir) CIV_DIR="${2:?--civ-dir needs a value}"; shift 2 ;;
        --eui-zip) EUI_ZIP="${2:?--eui-zip needs a value}"; shift 2 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
done

die() { echo "ERROR: $*" >&2; exit 1; }
for cmd in curl python3; do
    command -v "$cmd" >/dev/null || die "$cmd is required"
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── 1. Find the game ──────────────────────────────────────────────────────────
# Every Steam library listed in libraryfolders.vdf, across the usual Steam
# roots (native, ~/.steam symlink, Flatpak). compatdata lives in the same
# library as the game, so remember which library matched.
steam_libraries() {
    local root vdf
    for root in "$HOME/.local/share/Steam" "$HOME/.steam/steam" \
                "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        [[ -d "$root/steamapps" ]] || continue
        echo "$root"
        vdf="$root/steamapps/libraryfolders.vdf"
        [[ -f "$vdf" ]] && sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$vdf"
    done
}

if [[ -z "$CIV_DIR" ]]; then
    while IFS= read -r lib; do
        if [[ -d "$lib/steamapps/common/$CIV_NAME" ]]; then
            CIV_DIR="$lib/steamapps/common/$CIV_NAME"
            break
        fi
    done < <(steam_libraries | awk '!seen[$0]++')
fi
[[ -n "$CIV_DIR" && -d "$CIV_DIR" ]] || die "Civ V not found; pass --civ-dir \"<.../steamapps/common/$CIV_NAME>\""
CIV_DIR="$(cd "$CIV_DIR" && pwd)"

# The Windows build (what Proton runs) has CivilizationV_DX11.exe and
# Assets/DLC; the native Linux build has neither.
if [[ ! -f "$CIV_DIR/CivilizationV_DX11.exe" || ! -d "$CIV_DIR/Assets/DLC" ]]; then
    die "$CIV_DIR is the native Linux build. KEK Mod needs the Windows build:
  Steam -> Civ V -> Properties -> Compatibility -> force a Proton version,
  let Steam finish updating, launch the game once, then re-run this script."
fi
DLC="$CIV_DIR/Assets/DLC"
PREFIX_DOCS="$(dirname "$(dirname "$CIV_DIR")")/compatdata/$CIV_APPID/pfx/drive_c/users/steamuser/Documents/My Games/Sid Meier's Civilization 5"
echo "[GAME] $CIV_DIR"

# ── Zip extraction ────────────────────────────────────────────────────────────
# Release zips built with Windows PowerShell's Compress-Archive store paths
# with backslashes ("KEK Mod v2.0\Art\x.dds"). Many Linux unzip tools turn
# those into single files with backslashes in their names instead of folders,
# which leaves a mod folder the game silently ignores. Normalize here.
extract_zip() {
    python3 - "$1" "$2" <<'PY'
import os, sys, zipfile
zip_path, dest_root = sys.argv[1], os.path.realpath(sys.argv[2])
with zipfile.ZipFile(zip_path) as zf:
    for info in zf.infolist():
        rel = info.filename.replace('\\', '/')
        dest = os.path.realpath(os.path.join(dest_root, rel))
        if not dest.startswith(dest_root + os.sep):
            sys.exit("refusing to extract outside target: " + info.filename)
        if rel.endswith('/'):
            os.makedirs(dest, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with zf.open(info) as src, open(dest, 'wb') as out:
            out.write(src.read())
PY
}

# Top-level folder names in a zip (after separator normalization).
zip_top_dirs() {
    python3 - "$1" <<'PY'
import sys, zipfile
names = zipfile.ZipFile(sys.argv[1]).namelist()
print("\n".join(sorted({n.replace('\\', '/').split('/')[0] for n in names})))
PY
}

# ── 2. EUI ────────────────────────────────────────────────────────────────────
if [[ "$WANT_EUI" == 1 ]]; then
    if [[ -d "$DLC/UI_bc1" || -d "$DLC/UI_bc1_xits" ]]; then
        echo "[EUI] already installed, leaving it alone"
    else
        [[ -f "$EUI_ZIP" ]] || die "EUI zip not found ($EUI_ZIP).
  Download EUI (UI_bc1) from CivFanatics and pass --eui-zip <file>,
  or use --no-eui for the vanilla UI."
        [[ "$(zip_top_dirs "$EUI_ZIP")" == UI_bc1* ]] \
            || die "$EUI_ZIP doesn't look like EUI (expected a top-level UI_bc1 folder)"
        extract_zip "$EUI_ZIP" "$DLC"
        echo "[EUI] installed -> $DLC/$(zip_top_dirs "$EUI_ZIP")"
    fi
fi

# ── 3. Pick the release ───────────────────────────────────────────────────────
# Default is GitHub's own "Latest" release, same as KekModInstaller; --beta
# takes the newest release of any kind.
if [[ -n "$TAG" ]]; then
    API="https://api.github.com/repos/$REPO/releases/tags/$TAG"
elif [[ "$WANT_BETA" == 1 ]]; then
    API="https://api.github.com/repos/$REPO/releases?per_page=1"
else
    API="https://api.github.com/repos/$REPO/releases/latest"
fi
curl -fsSL -H "Accept: application/vnd.github+json" -o "$WORK/release.json" "$API" \
    || die "GitHub API request failed: $API"

# Prints "<tag>\t<zip url>"; a release is expected to carry exactly one zip.
read -r TAG ZIP_URL < <(python3 - "$WORK/release.json" <<'PY'
import json, sys
rel = json.load(open(sys.argv[1]))
if isinstance(rel, list):
    rel = rel[0]
zips = [a for a in rel.get("assets", []) if a["name"].lower().endswith(".zip")]
if len(zips) != 1:
    sys.exit("expected exactly one .zip asset on %s, found %d" % (rel.get("tag_name"), len(zips)))
print(rel["tag_name"], zips[0]["browser_download_url"], sep="\t")
PY
) || die "could not read release info"
echo "[KEK] release $TAG"

# ── 4. Install it ─────────────────────────────────────────────────────────────
curl -fsSL -o "$WORK/kekmod.zip" "$ZIP_URL" || die "download failed: $ZIP_URL"
KEK_FOLDER="$(zip_top_dirs "$WORK/kekmod.zip")"
[[ "$KEK_FOLDER" == "KEK Mod v"* && "$KEK_FOLDER" != *$'\n'* ]] \
    || die "unexpected zip layout (top level: $KEK_FOLDER)"

# Only one KEK Mod can be active; remove every previous version first.
for old in "$DLC"/"KEK Mod v"*; do
    [[ -d "$old" ]] || continue
    echo "[KEK] removing $(basename "$old")"
    rm -rf "$old"
done
extract_zip "$WORK/kekmod.zip" "$DLC"
echo "[KEK] installed -> $DLC/$KEK_FOLDER"

# ── 5. ui_check ───────────────────────────────────────────────────────────────
# Prefer the ui_check.sh shipped inside the release (matches its tmp/ files),
# then this checkout's copy, then the one at the release's tag, then main.
UI_CHECK="$DLC/$KEK_FOLDER/ui_check.sh"
if [[ ! -f "$UI_CHECK" ]]; then
    if [[ -f "$SCRIPT_DIR/ui_check.sh" ]]; then
        cp "$SCRIPT_DIR/ui_check.sh" "$UI_CHECK"
    else
        curl -fsSL -o "$UI_CHECK" "https://raw.githubusercontent.com/$REPO/$TAG/ui_check.sh" \
            || curl -fsSL -o "$UI_CHECK" "https://raw.githubusercontent.com/$REPO/main/ui_check.sh" \
            || die "could not get ui_check.sh"
    fi
fi
echo "[UI] running ui_check.sh"
bash "$UI_CHECK" "$DLC/$KEK_FOLDER" >"$WORK/ui_check.log" 2>&1 \
    || { cat "$WORK/ui_check.log" >&2; die "ui_check.sh failed"; }
tail -n 1 "$WORK/ui_check.log"

# ── 6. Fish Map Script ───────────────────────────────────────────────────────
# Same as installer/MapScriptExtra.cs: a best-effort bonus from its own repo,
# newest release, skipped if the folder already exists. Failures only warn.
FISH_DIR="$CIV_DIR/Assets/Maps/Fish Map Script"
install_fish_map() {
    local url
    curl -fsSL -H "Accept: application/vnd.github+json" -o "$WORK/fish.json" \
        "https://api.github.com/repos/OBLASTWAR/pangea-stratbal/releases?per_page=1" || return 1
    url="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[0]["assets"][0]["browser_download_url"])' \
        "$WORK/fish.json")" || return 1
    curl -fsSL -o "$WORK/fish.zip" "$url" || return 1
    extract_zip "$WORK/fish.zip" "$CIV_DIR/Assets/Maps" || return 1
    [[ -d "$FISH_DIR" ]]
}
if [[ -d "$FISH_DIR" ]]; then
    echo "[MAP] Fish Map Script already installed, skipping"
elif install_fish_map; then
    echo "[MAP] Fish Map Script installed"
else
    echo "[MAP] WARNING: couldn't install Fish Map Script (continuing)" >&2
fi

# ── 7. Cache ──────────────────────────────────────────────────────────────────
if [[ -d "$PREFIX_DOCS/cache" ]]; then
    rm -rf "${PREFIX_DOCS:?}/cache/"*
    echo "[CACHE] cleared"
else
    echo "[CACHE] no Proton prefix yet (game never launched under Proton) -- nothing to clear"
fi

echo ""
echo "Done: $KEK_FOLDER$( [[ "$WANT_EUI" == 1 ]] && echo " + EUI" )$( [[ -d "$FISH_DIR" ]] && echo " + Fish Map Script" ). Launch Civ V from Steam (DX9 or DX11)."
