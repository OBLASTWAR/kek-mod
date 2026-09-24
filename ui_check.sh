#!/usr/bin/env bash
# ui_check.sh -- Linux (Proton) port of ui_check.bat. Keep the two in sync:
# every check here mirrors one block of the .bat, in the same order.
#
# Run from the deployed DLC folder ("KEK Mod v<version>"), or pass its path:
#   ./ui_check.sh ["<...>/Assets/DLC/KEK Mod v2.1"]
# Rebuilds UI/ from tmp/ui (vanilla) or tmp/eui (EUI) depending on which EUI
# variant, if any, sits next to it in Assets/DLC.
set -u
P="$(cd "${1:-$(dirname "$0")}" && pwd)" || exit 1
[[ -d "$P/tmp/ui" ]] || { echo "ui_check.sh: $P is not a KEK Mod DLC folder (no tmp/ui)" >&2; exit 1; }
DLC="$(dirname "$P")"
E="$DLC/UI_bc1"
[[ ! -d "$E" && -d "$DLC/UI_bc1_xits" ]] && E="$DLC/UI_bc1_xits"
T="$P/tmp"; U="$P/UI"

cp_() { cp -f "$T/$1" "$U/$2" && echo "  $1 -> $2"; }
ui()  { cp_ "ui/$1" "$1"; }
eui() { cp_ "eui/$1" "$1"; }
has() { grep -qF -- "$1" "$2" 2>/dev/null; }

mkdir -p "$U"
find "$U" -maxdepth 1 -type f -delete

for f in CultureOverview.lua CultureOverview.xml EnemyUnitPanel.lua InGame.lua JoiningRoom.lua \
  VictoryProgress.xml VictoryProgress.lua MPList.lua MiniMapPanel.lua MiniMapPanel.xml \
  ChooseIdeologyPopup.lua CCVotePopup.lua CCVotePopup.xml EndGameMenu.lua ProposalChartPopup.lua \
  ProposalChartPopup.xml AdvancedSetup.lua StagingRoom.lua StagingRoom.xml MPGameDefaults.lua \
  MPGameOptions.lua MPGameSetupScreen.xml MPTurnPanel.lua MPTurnPanel.xml CivilopediaScreen.lua \
  GameMenu.lua Demographics.lua Bombardment.lua ChoosePantheonPopup.lua ChooseReligionPopup.lua \
  ReplayViewer.lua ReplayViewer.xml ReligionOverview.lua ReligionOverview.xml EspionageOverview.lua \
  UnitList.lua UnitList.xml Highlights.xml NetworkKickedPopup.lua; do ui "$f"; done

if has "-- destroy: check fix for need to update plot & cargo & airbase" "$E/UnitFlagManager/UnitFlagManager.lua"
then eui UnitFlagManager.xml; eui UnitFlagManager.lua; else ui UnitFlagManager.xml; ui UnitFlagManager.lua; fi

[[ -f "$E/Improvements/SocialPolicyPopup.lua" ]] && eui SocialPolicyPopup.lua || ui SocialPolicyPopup.lua
[[ -f "$E/NotificationPanel/DiploList.lua" ]] || ui DiploList.lua
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
has "-- coded by bc1 from Civ V 1.0.3.276 code" "$E/TopPanel/TopPanel.lua" && eui TopPanel.lua || ui TopPanel.lua
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
  if has "CityBannerProductionBox = function( city )" "$E/CityBanners/CityBannerManager.lua"; then
    cp_ eui/CityBannerManager_1.lua CityBannerManager.lua; cp_ eui/CityBannerManager_1.xml CityBannerManager.xml
  else
    cp_ eui/CityBannerManager_2.lua CityBannerManager.lua; cp_ eui/CityBannerManager_2.xml CityBannerManager.xml
  fi
else ui CityBannerManager.lua; ui CityBannerManager.xml; fi

if [[ -f "$E/NotificationPanel/DiploCorner.xml" ]]; then eui DiploCorner.lua; eui DiploCorner.xml; else ui DiploCorner.lua; ui DiploCorner.xml; fi
[[ -f "$E/Improvements/WorldView.lua" ]] && eui WorldView.lua || ui WorldView.lua
[[ -f "$E/TopPanel/TopPanel.xml" ]] && eui TopPanel.xml
[[ -f "$E/CityView/CityView.xml" ]] && eui CityView.xml || ui CityView.xml
[[ -f "$E/CityView/CityView_small.xml" ]] && eui CityView_small.xml || ui CityView_small.xml
[[ -d "$E" ]] && echo "ui_check done (EUI: $(basename "$E"))" || echo "ui_check done (no EUI: vanilla UI)"
