#!/usr/bin/env bash
# ui_check.sh -- Linux (Proton) port of ui_check.bat. Keep the two in sync:
# every check here mirrors one block of the .bat, in the same order.
# The Linux installer (civ5-mod) runs this file; Windows players run ui_check.bat.
#
# Run from the deployed DLC folder ("KEK Mod v<version>"), or pass its path:
#   ./ui_check.sh ["<...>/Assets/DLC/KEK Mod v2.1"]
# Rebuilds UI/ from tmp/ui (vanilla) or tmp/eui (EUI) depending on which EUI
# variant, if any, sits next to it in Assets/DLC.
#
# Every copy is verified and retried, failures are counted, and the result is
# written to ui_check_result.txt in the KEK Mod folder. The exit code is the
# number of files that failed (0 = OK); the last line printed is the summary.
set -u
P="$(cd "${1:-$(dirname "$0")}" && pwd)" || exit 1
[[ -d "$P/tmp/ui" ]] || { echo "ui_check.sh: $P is not a KEK Mod DLC folder (no tmp/ui)" >&2; exit 1; }
DLC="$(dirname "$P")"
E="$DLC/UI_bc1"
[[ ! -d "$E" && -d "$DLC/UI_bc1_xits" ]] && E="$DLC/UI_bc1_xits"
# An EUI installed under another folder name (e.g. "UI_bc1 (2)" from a second
# download) still loads in game, so find it by its package file.
if [[ ! -f "$E/EUI_0.Civ5Pkg" ]]; then
  for d in "$DLC"/*/; do [[ -f "${d}EUI_0.Civ5Pkg" ]] && E="${d%/}"; done
fi
T="$P/tmp"; U="$P/UI"; R="$P/ui_check_result.txt"
expected=0; copied=0; fails=0; warns=0
[[ -f "$E/EUI_0.Civ5Pkg" ]] && euiname="$(basename "$E")" || euiname="none (stock UI)"
{ echo "KEK Mod ui_check, $(date '+%Y-%m-%d %H:%M:%S')"; echo "folder: $P"; echo "EUI: $euiname"; } >"$R"
echo "KEK Mod ui_check: $(basename "$P")"
echo "EUI: $euiname"

warn() { warns=$((warns + 1)); echo "WARNING: $1"; echo "WARNING: $1" >>"$R"; }
n=0; for d in "$DLC"/*/; do [[ -f "${d}EUI_0.Civ5Pkg" ]] && n=$((n + 1)); done
(( n > 1 )) && warn "$n EUI folders in Assets/DLC. The game loads all of them, keep only one."
for f in "$DLC"/*/UI_bc1/EUI_0.Civ5Pkg "$DLC"/*/UI_bc1_xits/EUI_0.Civ5Pkg; do
  [[ -f "$f" ]] && warn "EUI is one folder too deep, move it up to Assets/DLC: $(dirname "$f")"
done

# cp_ SRC DST: copy tmp/SRC to UI/DST and verify it, retrying for a few seconds
# in case something holds the file open.
cp_() {
  local what="$1" i
  [[ "$(basename "$1")" != "$2" ]] && what="$1 as $2"
  expected=$((expected + 1))
  for i in 1 2 3 4 5 6; do
    if cp -f "$T/$1" "$U/$2" 2>/dev/null && cmp -s "$T/$1" "$U/$2"; then
      copied=$((copied + 1)); echo "  $what"; echo "$what" >>"$R"; return 0
    fi
    (( i < 6 )) && sleep 1
  done
  fails=$((fails + 1)); echo "  FAILED: $what"; echo "FAILED: $what" >>"$R"
}
ui()  { cp_ "ui/$1" "$1"; }
eui() { cp_ "eui/$1" "$1"; }
has() { grep -qF -- "$1" "$2" 2>/dev/null; }

mkdir -p "$U"
for i in 1 2 3 4 5 6; do
  find "$U" -maxdepth 1 -type f -delete 2>/dev/null
  [[ -z "$(find "$U" -maxdepth 1 -type f -print -quit)" ]] && break
  if (( i < 6 )); then sleep 1; else warn "Old files in UI/ could not be deleted. Close Civilization V and run ui_check again."; fi
done

for f in CultureOverview.lua CultureOverview.xml EnemyUnitPanel.lua InGame.lua JoiningRoom.lua \
  VictoryProgress.xml VictoryProgress.lua MPList.lua MiniMapPanel.lua MiniMapPanel.xml \
  ChooseIdeologyPopup.lua CCVotePopup.lua CCVotePopup.xml EndGameMenu.lua ProposalChartPopup.lua \
  ProposalChartPopup.xml AdvancedSetup.lua StagingRoom.lua StagingRoom.xml MPGameDefaults.lua \
  MPGameOptions.lua MPGameSetupScreen.xml MPGameSetupScreen.lua MPTurnPanel.lua MPTurnPanel.xml CivilopediaScreen.lua \
  GameMenu.lua Demographics.lua Bombardment.lua ChoosePantheonPopup.lua ChooseReligionPopup.lua \
  ReplayViewer.lua ReplayViewer.xml ReligionOverview.lua ReligionOverview.xml EspionageOverview.lua \
  UnitList.lua UnitList.xml Highlights.xml NetworkKickedPopup.lua KekSyncCheck.lua MainMenu.lua MainMenu.xml \
  ActionInfoPanel.lua; do ui "$f"; done

if has "-- destroy: check fix for need to update plot & cargo & airbase" "$E/UnitFlagManager/UnitFlagManager.lua"
then eui UnitFlagManager.xml; eui UnitFlagManager.lua; else ui UnitFlagManager.xml; ui UnitFlagManager.lua; fi

[[ -f "$E/Improvements/SocialPolicyPopup.lua" ]] && eui SocialPolicyPopup.lua || ui SocialPolicyPopup.lua
[[ -f "$E/NotificationPanel/DiploList.lua" ]] || ui DiploList.lua
[[ -f "$E/FrontEnd/FrontEnd.lua" ]] && eui FrontEnd.lua || ui FrontEnd.lua
[[ -f "$E/LeaderHead/TradeLogic.lua" ]] && eui TradeLogic.lua || ui TradeLogic.lua
if [[ -f "$E/ToolTips/InfoTooltipInclude.lua" ]]; then eui EUI_tooltip_library.lua; eui EUI_unit_include.lua
else ui InfoTooltipInclude.lua; fi
[[ -f "$E/PlotHelp/PlotHelpManager.lua" ]] || { ui PlotHelpManager.lua; ui PlotMouseoverInclude.lua; }
[[ -f "$E/PlotHelp/PlotHelpManager.xml" ]] || ui PlotHelpManager.xml
[[ -f "$E/ToolTips/TechButtonInclude.lua" ]] && eui TechButtonInclude.lua || ui TechButtonInclude.lua
[[ -f "$E/CityView/ProductionPopup.lua" ]] || ui ProductionPopup.lua
[[ -f "$E/TechTree/TechPopup.lua" ]] || ui TechPopup.lua

has "-- modified by bc1 from Civ V 1.0.3.276 code" "$E/UnitPanel/UnitPanel.lua" && eui UnitPanel.lua || ui UnitPanel.lua
if has "-- modified by bc1 from 1.0.3.144 brave new world code" "$E/CityStatePopup/CityStateDiploPopup.lua"
then eui CityStateDiploPopup.lua; eui CityStateDiploPopup.xml; else ui CityStateDiploPopup.lua; ui CityStateDiploPopup.xml; fi
has "-- coded by bc1 from 1.0.3.276 brave new world code" "$E/CityView/CityView.lua" && eui CityView.lua || ui CityView.lua
# EUI XITS (whatever its folder is called) is told apart from EUI by its Great General/Admiral
# bars. It gets the *_xits files: KEK's top panel and city banners with the XITS extras.
if has "GetNavalCombatExperience" "$E/TopPanel/TopPanel.lua"; then cp_ eui/TopPanel_xits.lua TopPanel.lua
elif has "-- coded by bc1 from Civ V 1.0.3.276 code" "$E/TopPanel/TopPanel.lua"; then eui TopPanel.lua
else ui TopPanel.lua; fi
has "Game.SelectionListGameNetMessage( GameMessageTypes.GAMEMESSAGE_DO_COMMAND, action.CommandType, action.CommandData, -1, 0, bAlt );" \
  "$E/Improvements/ConfirmCommandPopup.lua" && eui ConfirmCommandPopup.lua || ui ConfirmCommandPopup.lua
has "-- coded by bc1 from Civ V 1.0.3.276 code" "$E/TechTree/TechTree.lua" && eui TechTree.lua || ui TechTree.lua
has "-- modified by bc1 from 1.0.3.144 brave new world & civ BE code" "$E/Core/CityStateStatusHelper.lua" \
  && eui CityStateStatusHelper.lua || ui CityStateStatusHelper.lua
[[ -f "$E/NotificationPanel/NotificationPanel.lua" ]] && eui NotificationPanel.lua || ui NotificationPanel.lua
[[ -f "$E/NotificationPanel/NotificationPanel.xml" ]] && eui NotificationPanel.xml || ui NotificationPanel.xml
if [[ -f "$E/Options/OptionsMenu.lua" ]]; then cp_ eui/OptionsMenu.lua.ignore OptionsMenu.lua; else cp_ ui/OptionsMenu.lua.ignore OptionsMenu.lua; fi
if [[ -f "$E/Options/OptionsMenu.xml" ]]; then cp_ eui/OptionsMenu.xml.ignore OptionsMenu.xml; else cp_ ui/OptionsMenu.xml.ignore OptionsMenu.xml; fi

if [[ -f "$E/CityBanners/CityBannerManager.lua" ]]; then
  if has "GetNavalCombatExperience" "$E/TopPanel/TopPanel.lua"; then
    cp_ eui/CityBannerManager_xits.lua CityBannerManager.lua; cp_ eui/CityBannerManager_1.xml CityBannerManager.xml
  elif has "CityBannerProductionBox = function( city )" "$E/CityBanners/CityBannerManager.lua"; then
    cp_ eui/CityBannerManager_1.lua CityBannerManager.lua; cp_ eui/CityBannerManager_1.xml CityBannerManager.xml
  else
    cp_ eui/CityBannerManager_2.lua CityBannerManager.lua; cp_ eui/CityBannerManager_2.xml CityBannerManager.xml
  fi
else ui CityBannerManager.lua; ui CityBannerManager.xml; fi

if [[ -f "$E/NotificationPanel/DiploCorner.xml" ]]; then eui DiploCorner.lua; eui DiploCorner.xml; else ui DiploCorner.lua; ui DiploCorner.xml; fi
[[ -f "$E/Improvements/WorldView.lua" ]] && eui WorldView.lua || ui WorldView.lua
[[ -f "$E/Improvements/YieldIconManager.lua" ]] && eui YieldIconManager.lua || ui YieldIconManager.lua
if [[ -f "$E/TopPanel/TopPanel.xml" ]]; then
  if has "GetNavalCombatExperience" "$E/TopPanel/TopPanel.lua"; then cp_ eui/TopPanel_xits.xml TopPanel.xml; else eui TopPanel.xml; fi
fi
[[ -f "$E/CityView/CityView.xml" ]] && eui CityView.xml || ui CityView.xml
[[ -f "$E/CityView/CityView_small.xml" ]] && eui CityView_small.xml || ui CityView_small.xml
# UI/KekUiCheck.lua tells the lobby screens how this run went (no file: it never finished).
echo "KekUiCheck = { fails = $fails, warns = $warns, files = $copied, expected = $expected }" >"$U/KekUiCheck.lua"
echo
if (( fails > 0 )); then
  echo "result: FAILED, $fails of $expected files, $warns warnings" >>"$R"
  echo "ui_check FAILED: $fails of $expected files could not be copied into UI/"
else
  echo "result: OK, $copied files, $warns warnings" >>"$R"
  (( warns > 0 )) && echo "$warns warning(s) above."
  echo "ui_check OK: $copied files in UI/ (EUI: $euiname)"
fi
exit $(( fails > 255 ? 255 : fails ))
