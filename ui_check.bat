@ECHO off
REM ui_check.bat -- rebuilds UI\ from tmp\ui (stock UI) or tmp\eui (EUI), depending on
REM which EUI sits next to this folder in Assets\DLC. Run it from the installed DLC folder
REM ("KEK Mod v<version>"); the installer runs it too. Keep ui_check.sh in sync.
REM
REM Two things read this file, so it keeps to a narrow form:
REM  - cmd.exe on Windows.
REM  - civ5-mod (the Linux installer), which interprets a subset of batch in Python:
REM    copy, del, set, IF [NOT] EXIST and FINDSTR blocks, one command per line. It
REM    skips anything else with a warning, and a "(" block of any other kind (FOR,
REM    IF "a"=="b") anywhere in the file, even after EXIT, makes it abort.
REM So every file gets a plain "copy" line that civ5-mod runs, followed by a
REM "call :check" line that only Windows runs: it verifies the copy, retries a few
REM times (antivirus can hold a freshly extracted file) and counts failures.
REM Windows-only extras stay on single lines; the subroutines come after EXIT.
REM
REM copy /y and del /q never prompt. The old "ECHO F | xcopy" / "ECHO Y | del" answered
REM prompts that are translated on non-English Windows.
REM
REM The exit code is the number of files that failed (0 = OK), and the result is
REM written to ui_check_result.txt in this folder. The summary stays on screen for a few
REM seconds for players who double-click it; the installer passes /nopause.
set "nopause="
if /i "%~1"=="/nopause" set "nopause=1"
pushd "%~dp0"
REM patchfolder = the name of the folder this script lives in (the deployed
REM DLC folder, e.g. "KEK Mod v1.5-beta2") -- no version hardcoding.
for %%I in ("%cd%") do set "patchfolder=%%~nxI"
cd ..
set "D=%cd%"
set "P=%D%\%patchfolder%"
set "T=%P%\tmp"
set "U=%P%\UI"
set "R=%P%\ui_check_result.txt"
set "expected=0"
set "copied=0"
set "fails=0"
set "warns=0"
REM euifolder = whichever EUI variant folder is actually present ("UI_bc1"
REM or "UI_bc1_xits"), so every check below behaves the same regardless of
REM which one the player has (or the installer's EUI section installed).
REM Defaults to "UI_bc1" so a from-scratch check with neither present still
REM falls through every check below to the vanilla-UI branch.
set euifolder=UI_bc1
IF NOT EXIST "%D%\UI_bc1" (
  IF EXIST "%D%\UI_bc1_xits" set euifolder=UI_bc1_xits
)
set "E=%D%\%euifolder%"
REM Windows only: an EUI installed under another folder name (e.g. "UI_bc1 (2)" from a
REM second download) still loads in game, so find it by its package file. The current
REM folder is Assets\DLC here; "for /d" uses a bare * because Wine's cmd matches
REM nothing with a quoted wildcard.
IF NOT EXIST "%E%\EUI_0.Civ5Pkg" for /d %%F in (*) do if exist "%%~fF\EUI_0.Civ5Pkg" set "E=%%~fF"
call :begin
REM -------------------------------------------------
del /f /q "%U%\" >nul 2>&1
call :clean
REM -------------------------------------------------
copy /y "%T%\ui\CultureOverview.lua" "%U%\CultureOverview.lua" >nul 2>&1
call :check ui CultureOverview.lua
copy /y "%T%\ui\CultureOverview.xml" "%U%\CultureOverview.xml" >nul 2>&1
call :check ui CultureOverview.xml
copy /y "%T%\ui\EnemyUnitPanel.lua" "%U%\EnemyUnitPanel.lua" >nul 2>&1
call :check ui EnemyUnitPanel.lua
copy /y "%T%\ui\InGame.lua" "%U%\InGame.lua" >nul 2>&1
call :check ui InGame.lua
copy /y "%T%\ui\JoiningRoom.lua" "%U%\JoiningRoom.lua" >nul 2>&1
call :check ui JoiningRoom.lua
copy /y "%T%\ui\VictoryProgress.xml" "%U%\VictoryProgress.xml" >nul 2>&1
call :check ui VictoryProgress.xml
copy /y "%T%\ui\VictoryProgress.lua" "%U%\VictoryProgress.lua" >nul 2>&1
call :check ui VictoryProgress.lua
copy /y "%T%\ui\MPList.lua" "%U%\MPList.lua" >nul 2>&1
call :check ui MPList.lua
copy /y "%T%\ui\MiniMapPanel.lua" "%U%\MiniMapPanel.lua" >nul 2>&1
call :check ui MiniMapPanel.lua
copy /y "%T%\ui\MiniMapPanel.xml" "%U%\MiniMapPanel.xml" >nul 2>&1
call :check ui MiniMapPanel.xml
copy /y "%T%\ui\ChooseIdeologyPopup.lua" "%U%\ChooseIdeologyPopup.lua" >nul 2>&1
call :check ui ChooseIdeologyPopup.lua
copy /y "%T%\ui\CCVotePopup.lua" "%U%\CCVotePopup.lua" >nul 2>&1
call :check ui CCVotePopup.lua
copy /y "%T%\ui\CCVotePopup.xml" "%U%\CCVotePopup.xml" >nul 2>&1
call :check ui CCVotePopup.xml
copy /y "%T%\ui\EndGameMenu.lua" "%U%\EndGameMenu.lua" >nul 2>&1
call :check ui EndGameMenu.lua
copy /y "%T%\ui\ProposalChartPopup.lua" "%U%\ProposalChartPopup.lua" >nul 2>&1
call :check ui ProposalChartPopup.lua
copy /y "%T%\ui\ProposalChartPopup.xml" "%U%\ProposalChartPopup.xml" >nul 2>&1
call :check ui ProposalChartPopup.xml
copy /y "%T%\ui\AdvancedSetup.lua" "%U%\AdvancedSetup.lua" >nul 2>&1
call :check ui AdvancedSetup.lua
copy /y "%T%\ui\StagingRoom.lua" "%U%\StagingRoom.lua" >nul 2>&1
call :check ui StagingRoom.lua
copy /y "%T%\ui\StagingRoom.xml" "%U%\StagingRoom.xml" >nul 2>&1
call :check ui StagingRoom.xml
copy /y "%T%\ui\MPGameDefaults.lua" "%U%\MPGameDefaults.lua" >nul 2>&1
call :check ui MPGameDefaults.lua
copy /y "%T%\ui\MainMenu.lua" "%U%\MainMenu.lua" >nul 2>&1
call :check ui MainMenu.lua
copy /y "%T%\ui\MainMenu.xml" "%U%\MainMenu.xml" >nul 2>&1
call :check ui MainMenu.xml
copy /y "%T%\ui\ActionInfoPanel.lua" "%U%\ActionInfoPanel.lua" >nul 2>&1
call :check ui ActionInfoPanel.lua
copy /y "%T%\ui\MPGameOptions.lua" "%U%\MPGameOptions.lua" >nul 2>&1
call :check ui MPGameOptions.lua
copy /y "%T%\ui\MPGameSetupScreen.xml" "%U%\MPGameSetupScreen.xml" >nul 2>&1
call :check ui MPGameSetupScreen.xml
copy /y "%T%\ui\MPGameSetupScreen.lua" "%U%\MPGameSetupScreen.lua" >nul 2>&1
call :check ui MPGameSetupScreen.lua
copy /y "%T%\ui\MPTurnPanel.lua" "%U%\MPTurnPanel.lua" >nul 2>&1
call :check ui MPTurnPanel.lua
copy /y "%T%\ui\MPTurnPanel.xml" "%U%\MPTurnPanel.xml" >nul 2>&1
call :check ui MPTurnPanel.xml
copy /y "%T%\ui\CivilopediaScreen.lua" "%U%\CivilopediaScreen.lua" >nul 2>&1
call :check ui CivilopediaScreen.lua
copy /y "%T%\ui\GameMenu.lua" "%U%\GameMenu.lua" >nul 2>&1
call :check ui GameMenu.lua
copy /y "%T%\ui\Demographics.lua" "%U%\Demographics.lua" >nul 2>&1
call :check ui Demographics.lua
copy /y "%T%\ui\Bombardment.lua" "%U%\Bombardment.lua" >nul 2>&1
call :check ui Bombardment.lua
copy /y "%T%\ui\ChoosePantheonPopup.lua" "%U%\ChoosePantheonPopup.lua" >nul 2>&1
call :check ui ChoosePantheonPopup.lua
copy /y "%T%\ui\ChooseReligionPopup.lua" "%U%\ChooseReligionPopup.lua" >nul 2>&1
call :check ui ChooseReligionPopup.lua
copy /y "%T%\ui\ReplayViewer.lua" "%U%\ReplayViewer.lua" >nul 2>&1
call :check ui ReplayViewer.lua
copy /y "%T%\ui\ReplayViewer.xml" "%U%\ReplayViewer.xml" >nul 2>&1
call :check ui ReplayViewer.xml
copy /y "%T%\ui\ReligionOverview.lua" "%U%\ReligionOverview.lua" >nul 2>&1
call :check ui ReligionOverview.lua
copy /y "%T%\ui\ReligionOverview.xml" "%U%\ReligionOverview.xml" >nul 2>&1
call :check ui ReligionOverview.xml
copy /y "%T%\ui\EspionageOverview.lua" "%U%\EspionageOverview.lua" >nul 2>&1
call :check ui EspionageOverview.lua
copy /y "%T%\ui\UnitList.lua" "%U%\UnitList.lua" >nul 2>&1
call :check ui UnitList.lua
copy /y "%T%\ui\UnitList.xml" "%U%\UnitList.xml" >nul 2>&1
call :check ui UnitList.xml
copy /y "%T%\ui\Highlights.xml" "%U%\Highlights.xml" >nul 2>&1
call :check ui Highlights.xml
copy /y "%T%\ui\NetworkKickedPopup.lua" "%U%\NetworkKickedPopup.lua" >nul 2>&1
call :check ui NetworkKickedPopup.lua
copy /y "%T%\ui\KekSyncCheck.lua" "%U%\KekSyncCheck.lua" >nul 2>&1
call :check ui KekSyncCheck.lua

REM -------------------------------------------------
set text="-- destroy: check fix for need to update plot & cargo & airbase"
FINDSTR /L /C:%text% "%E%\UnitFlagManager\UnitFlagManager.lua" >nul 2>&1 && (
  copy /y "%T%\eui\UnitFlagManager.xml" "%U%\UnitFlagManager.xml" >nul 2>&1
  call :check eui UnitFlagManager.xml
  copy /y "%T%\eui\UnitFlagManager.lua" "%U%\UnitFlagManager.lua" >nul 2>&1
  call :check eui UnitFlagManager.lua
) || (
  copy /y "%T%\ui\UnitFlagManager.xml" "%U%\UnitFlagManager.xml" >nul 2>&1
  call :check ui UnitFlagManager.xml
  copy /y "%T%\ui\UnitFlagManager.lua" "%U%\UnitFlagManager.lua" >nul 2>&1
  call :check ui UnitFlagManager.lua
)
REM -------------------------------------------------
IF EXIST "%E%\Improvements\SocialPolicyPopup.lua" (
  copy /y "%T%\eui\SocialPolicyPopup.lua" "%U%\SocialPolicyPopup.lua" >nul 2>&1
  call :check eui SocialPolicyPopup.lua
) ELSE (
  copy /y "%T%\ui\SocialPolicyPopup.lua" "%U%\SocialPolicyPopup.lua" >nul 2>&1
  call :check ui SocialPolicyPopup.lua
)
REM -------------------------------------------------
IF NOT EXIST "%E%\NotificationPanel\DiploList.lua" (
  copy /y "%T%\ui\DiploList.lua" "%U%\DiploList.lua" >nul 2>&1
  call :check ui DiploList.lua
)
REM -------------------------------------------------
IF EXIST "%E%\FrontEnd\FrontEnd.lua" (
  copy /y "%T%\eui\FrontEnd.lua" "%U%\FrontEnd.lua" >nul 2>&1
  call :check eui FrontEnd.lua
) ELSE (
  copy /y "%T%\ui\FrontEnd.lua" "%U%\FrontEnd.lua" >nul 2>&1
  call :check ui FrontEnd.lua
)
REM -------------------------------------------------
IF EXIST "%E%\LeaderHead\TradeLogic.lua" (
  copy /y "%T%\eui\TradeLogic.lua" "%U%\TradeLogic.lua" >nul 2>&1
  call :check eui TradeLogic.lua
) ELSE (
  copy /y "%T%\ui\TradeLogic.lua" "%U%\TradeLogic.lua" >nul 2>&1
  call :check ui TradeLogic.lua
)
REM -------------------------------------------------
IF EXIST "%E%\ToolTips\InfoTooltipInclude.lua" (
  copy /y "%T%\eui\EUI_tooltip_library.lua" "%U%\EUI_tooltip_library.lua" >nul 2>&1
  call :check eui EUI_tooltip_library.lua
  copy /y "%T%\eui\EUI_unit_include.lua" "%U%\EUI_unit_include.lua" >nul 2>&1
  call :check eui EUI_unit_include.lua
) ELSE (
  copy /y "%T%\ui\InfoTooltipInclude.lua" "%U%\InfoTooltipInclude.lua" >nul 2>&1
  call :check ui InfoTooltipInclude.lua
)
REM -------------------------------------------------
IF NOT EXIST "%E%\PlotHelp\PlotHelpManager.lua" (
  copy /y "%T%\ui\PlotHelpManager.lua" "%U%\PlotHelpManager.lua" >nul 2>&1
  call :check ui PlotHelpManager.lua
  copy /y "%T%\ui\PlotMouseoverInclude.lua" "%U%\PlotMouseoverInclude.lua" >nul 2>&1
  call :check ui PlotMouseoverInclude.lua
)
REM -------------------------------------------------
IF NOT EXIST "%E%\PlotHelp\PlotHelpManager.xml" (
  copy /y "%T%\ui\PlotHelpManager.xml" "%U%\PlotHelpManager.xml" >nul 2>&1
  call :check ui PlotHelpManager.xml
)
REM -------------------------------------------------
IF EXIST "%E%\ToolTips\TechButtonInclude.lua" (
  copy /y "%T%\eui\TechButtonInclude.lua" "%U%\TechButtonInclude.lua" >nul 2>&1
  call :check eui TechButtonInclude.lua
) ELSE (
  copy /y "%T%\ui\TechButtonInclude.lua" "%U%\TechButtonInclude.lua" >nul 2>&1
  call :check ui TechButtonInclude.lua
)
REM -------------------------------------------------
IF NOT EXIST "%E%\CityView\ProductionPopup.lua" (
  copy /y "%T%\ui\ProductionPopup.lua" "%U%\ProductionPopup.lua" >nul 2>&1
  call :check ui ProductionPopup.lua
)
REM -------------------------------------------------
IF NOT EXIST "%E%\TechTree\TechPopup.lua" (
  copy /y "%T%\ui\TechPopup.lua" "%U%\TechPopup.lua" >nul 2>&1
  call :check ui TechPopup.lua
)
REM -------------------------------------------------
set text="-- modified by bc1 from Civ V 1.0.3.276 code"
FINDSTR /L /C:%text% "%E%\UnitPanel\UnitPanel.lua" >nul 2>&1 && (
  copy /y "%T%\eui\UnitPanel.lua" "%U%\UnitPanel.lua" >nul 2>&1
  call :check eui UnitPanel.lua
) || (
  copy /y "%T%\ui\UnitPanel.lua" "%U%\UnitPanel.lua" >nul 2>&1
  call :check ui UnitPanel.lua
)
REM -------------------------------------------------
set text="-- modified by bc1 from 1.0.3.144 brave new world code"
FINDSTR /L /C:%text% "%E%\CityStatePopup\CityStateDiploPopup.lua" >nul 2>&1 && (
  copy /y "%T%\eui\CityStateDiploPopup.lua" "%U%\CityStateDiploPopup.lua" >nul 2>&1
  call :check eui CityStateDiploPopup.lua
  copy /y "%T%\eui\CityStateDiploPopup.xml" "%U%\CityStateDiploPopup.xml" >nul 2>&1
  call :check eui CityStateDiploPopup.xml
) || (
  copy /y "%T%\ui\CityStateDiploPopup.lua" "%U%\CityStateDiploPopup.lua" >nul 2>&1
  call :check ui CityStateDiploPopup.lua
  copy /y "%T%\ui\CityStateDiploPopup.xml" "%U%\CityStateDiploPopup.xml" >nul 2>&1
  call :check ui CityStateDiploPopup.xml
)
REM -------------------------------------------------
set text="-- coded by bc1 from 1.0.3.276 brave new world code"
FINDSTR /L /C:%text% "%E%\CityView\CityView.lua" >nul 2>&1 && (
  copy /y "%T%\eui\CityView.lua" "%U%\CityView.lua" >nul 2>&1
  call :check eui CityView.lua
) || (
  copy /y "%T%\ui\CityView.lua" "%U%\CityView.lua" >nul 2>&1
  call :check ui CityView.lua
)
REM -------------------------------------------------
REM EUI XITS (whatever its folder is called) is told apart from EUI by its Great
REM General/Admiral bars. It gets the *_xits files: KEK's top panel and city banners
REM with the XITS extras.
set text="-- coded by bc1 from Civ V 1.0.3.276 code"
FINDSTR /L /C:"GetNavalCombatExperience" "%E%\TopPanel\TopPanel.lua" >nul 2>&1 && (
  copy /y "%T%\eui\TopPanel_xits.lua" "%U%\TopPanel.lua" >nul 2>&1
  call :check eui TopPanel_xits.lua TopPanel.lua
) || (
  FINDSTR /L /C:%text% "%E%\TopPanel\TopPanel.lua" >nul 2>&1 && (
    copy /y "%T%\eui\TopPanel.lua" "%U%\TopPanel.lua" >nul 2>&1
    call :check eui TopPanel.lua
  ) || (
    copy /y "%T%\ui\TopPanel.lua" "%U%\TopPanel.lua" >nul 2>&1
    call :check ui TopPanel.lua
  )
)
REM -------------------------------------------------
set text="Game.SelectionListGameNetMessage( GameMessageTypes.GAMEMESSAGE_DO_COMMAND, action.CommandType, action.CommandData, -1, 0, bAlt );"
FINDSTR /L /C:%text% "%E%\Improvements\ConfirmCommandPopup.lua" >nul 2>&1 && (
  copy /y "%T%\eui\ConfirmCommandPopup.lua" "%U%\ConfirmCommandPopup.lua" >nul 2>&1
  call :check eui ConfirmCommandPopup.lua
) || (
  copy /y "%T%\ui\ConfirmCommandPopup.lua" "%U%\ConfirmCommandPopup.lua" >nul 2>&1
  call :check ui ConfirmCommandPopup.lua
)
REM -------------------------------------------------
set text="-- coded by bc1 from Civ V 1.0.3.276 code"
FINDSTR /L /C:%text% "%E%\TechTree\TechTree.lua" >nul 2>&1 && (
  copy /y "%T%\eui\TechTree.lua" "%U%\TechTree.lua" >nul 2>&1
  call :check eui TechTree.lua
) || (
  copy /y "%T%\ui\TechTree.lua" "%U%\TechTree.lua" >nul 2>&1
  call :check ui TechTree.lua
)
REM -------------------------------------------------
set text="-- modified by bc1 from 1.0.3.144 brave new world & civ BE code"
FINDSTR /L /C:%text% "%E%\Core\CityStateStatusHelper.lua" >nul 2>&1 && (
  copy /y "%T%\eui\CityStateStatusHelper.lua" "%U%\CityStateStatusHelper.lua" >nul 2>&1
  call :check eui CityStateStatusHelper.lua
) || (
  copy /y "%T%\ui\CityStateStatusHelper.lua" "%U%\CityStateStatusHelper.lua" >nul 2>&1
  call :check ui CityStateStatusHelper.lua
)
REM -------------------------------------------------
IF EXIST "%E%\NotificationPanel\NotificationPanel.lua" (
  copy /y "%T%\eui\NotificationPanel.lua" "%U%\NotificationPanel.lua" >nul 2>&1
  call :check eui NotificationPanel.lua
) ELSE (
  copy /y "%T%\ui\NotificationPanel.lua" "%U%\NotificationPanel.lua" >nul 2>&1
  call :check ui NotificationPanel.lua
)
REM -------------------------------------------------
IF EXIST "%E%\NotificationPanel\NotificationPanel.xml" (
  copy /y "%T%\eui\NotificationPanel.xml" "%U%\NotificationPanel.xml" >nul 2>&1
  call :check eui NotificationPanel.xml
) ELSE (
  copy /y "%T%\ui\NotificationPanel.xml" "%U%\NotificationPanel.xml" >nul 2>&1
  call :check ui NotificationPanel.xml
)
REM -------------------------------------------------
IF EXIST "%E%\Options\OptionsMenu.lua" (
  copy /y "%T%\eui\OptionsMenu.lua.ignore" "%U%\OptionsMenu.lua" >nul 2>&1
  call :check eui OptionsMenu.lua.ignore OptionsMenu.lua
) ELSE (
  copy /y "%T%\ui\OptionsMenu.lua.ignore" "%U%\OptionsMenu.lua" >nul 2>&1
  call :check ui OptionsMenu.lua.ignore OptionsMenu.lua
)
REM -------------------------------------------------
IF EXIST "%E%\Options\OptionsMenu.xml" (
  copy /y "%T%\eui\OptionsMenu.xml.ignore" "%U%\OptionsMenu.xml" >nul 2>&1
  call :check eui OptionsMenu.xml.ignore OptionsMenu.xml
) ELSE (
  copy /y "%T%\ui\OptionsMenu.xml.ignore" "%U%\OptionsMenu.xml" >nul 2>&1
  call :check ui OptionsMenu.xml.ignore OptionsMenu.xml
)
REM -------------------------------------------------
REM CityBannerManager_2 is for EUIs with the clickable production button
REM (CityBannerProductionButton) in place of CityBannerProductionBox.
set text="CityBannerProductionBox = function( city )"
IF EXIST "%E%\CityBanners\CityBannerManager.lua" (
  FINDSTR /L /C:"GetNavalCombatExperience" "%E%\TopPanel\TopPanel.lua" >nul 2>&1 && (
    copy /y "%T%\eui\CityBannerManager_xits.lua" "%U%\CityBannerManager.lua" >nul 2>&1
    call :check eui CityBannerManager_xits.lua CityBannerManager.lua
    copy /y "%T%\eui\CityBannerManager_1.xml" "%U%\CityBannerManager.xml" >nul 2>&1
    call :check eui CityBannerManager_1.xml CityBannerManager.xml
  ) || (
    FINDSTR /L /C:%text% "%E%\CityBanners\CityBannerManager.lua" >nul 2>&1 && (
      copy /y "%T%\eui\CityBannerManager_1.lua" "%U%\CityBannerManager.lua" >nul 2>&1
      call :check eui CityBannerManager_1.lua CityBannerManager.lua
      copy /y "%T%\eui\CityBannerManager_1.xml" "%U%\CityBannerManager.xml" >nul 2>&1
      call :check eui CityBannerManager_1.xml CityBannerManager.xml
    ) || (
      copy /y "%T%\eui\CityBannerManager_2.lua" "%U%\CityBannerManager.lua" >nul 2>&1
      call :check eui CityBannerManager_2.lua CityBannerManager.lua
      copy /y "%T%\eui\CityBannerManager_2.xml" "%U%\CityBannerManager.xml" >nul 2>&1
      call :check eui CityBannerManager_2.xml CityBannerManager.xml
    )
  )
) ELSE (
  copy /y "%T%\ui\CityBannerManager.lua" "%U%\CityBannerManager.lua" >nul 2>&1
  call :check ui CityBannerManager.lua
  copy /y "%T%\ui\CityBannerManager.xml" "%U%\CityBannerManager.xml" >nul 2>&1
  call :check ui CityBannerManager.xml
)
REM -------------------------------------------------
IF EXIST "%E%\NotificationPanel\DiploCorner.xml" (
  copy /y "%T%\eui\DiploCorner.lua" "%U%\DiploCorner.lua" >nul 2>&1
  call :check eui DiploCorner.lua
  copy /y "%T%\eui\DiploCorner.xml" "%U%\DiploCorner.xml" >nul 2>&1
  call :check eui DiploCorner.xml
) ELSE (
  copy /y "%T%\ui\DiploCorner.lua" "%U%\DiploCorner.lua" >nul 2>&1
  call :check ui DiploCorner.lua
  copy /y "%T%\ui\DiploCorner.xml" "%U%\DiploCorner.xml" >nul 2>&1
  call :check ui DiploCorner.xml
)
REM -------------------------------------------------
IF EXIST "%E%\Improvements\WorldView.lua" (
  copy /y "%T%\eui\WorldView.lua" "%U%\WorldView.lua" >nul 2>&1
  call :check eui WorldView.lua
) ELSE (
  copy /y "%T%\ui\WorldView.lua" "%U%\WorldView.lua" >nul 2>&1
  call :check ui WorldView.lua
)
REM -------------------------------------------------
IF EXIST "%E%\Improvements\YieldIconManager.lua" (
  copy /y "%T%\eui\YieldIconManager.lua" "%U%\YieldIconManager.lua" >nul 2>&1
  call :check eui YieldIconManager.lua
) ELSE (
  copy /y "%T%\ui\YieldIconManager.lua" "%U%\YieldIconManager.lua" >nul 2>&1
  call :check ui YieldIconManager.lua
)
REM -------------------------------------------------
IF EXIST "%E%\TopPanel\TopPanel.xml" (
  FINDSTR /L /C:"GetNavalCombatExperience" "%E%\TopPanel\TopPanel.lua" >nul 2>&1 && (
    copy /y "%T%\eui\TopPanel_xits.xml" "%U%\TopPanel.xml" >nul 2>&1
    call :check eui TopPanel_xits.xml TopPanel.xml
  ) || (
    copy /y "%T%\eui\TopPanel.xml" "%U%\TopPanel.xml" >nul 2>&1
    call :check eui TopPanel.xml
  )
)
REM -------------------------------------------------
IF EXIST "%E%\CityView\CityView.xml" (
  copy /y "%T%\eui\CityView.xml" "%U%\CityView.xml" >nul 2>&1
  call :check eui CityView.xml
) ELSE (
  copy /y "%T%\ui\CityView.xml" "%U%\CityView.xml" >nul 2>&1
  call :check ui CityView.xml
)
REM -------------------------------------------------
IF EXIST "%E%\CityView\CityView_small.xml" (
  copy /y "%T%\eui\CityView_small.xml" "%U%\CityView_small.xml" >nul 2>&1
  call :check eui CityView_small.xml
) ELSE (
  copy /y "%T%\ui\CityView_small.xml" "%U%\CityView_small.xml" >nul 2>&1
  call :check ui CityView_small.xml
)
REM -------------------------------------------------
REM UI\KekUiCheck.lua tells the lobby screens how this run went (no file: it never
REM finished). civ5-mod writes this line as is; on Windows :summary rewrites it
REM with the real counts.
echo KekUiCheck = { fails = 0, warns = 0 }> "%U%\KekUiCheck.lua"
call :summary
EXIT /b %fails%

REM =================================================
REM Windows-only subroutines. civ5-mod stops at the EXIT above. No "(" blocks below.
REM =================================================

:begin
for %%I in ("%E%") do set "euiname=%%~nxI"
IF NOT EXIST "%E%\EUI_0.Civ5Pkg" set "euiname=none (stock UI)"
> "%R%" echo KEK Mod ui_check, %date% %time%
>> "%R%" echo folder: %P%
>> "%R%" echo EUI: %euiname%
echo KEK Mod ui_check: %patchfolder%
echo EUI: %euiname%
set "euicount=0"
for /d %%F in (*) do if exist "%%~fF\EUI_0.Civ5Pkg" set /a euicount+=1
if %euicount% GTR 1 call :warn "%euicount% EUI folders in Assets\DLC. The game loads all of them, keep only one."
for /d %%F in (*) do if exist "%%~fF\UI_bc1\EUI_0.Civ5Pkg" call :warn "EUI is one folder too deep, move it up to Assets\DLC: %%~fF\UI_bc1"
for /d %%F in (*) do if exist "%%~fF\UI_bc1_xits\EUI_0.Civ5Pkg" call :warn "EUI is one folder too deep, move it up to Assets\DLC: %%~fF\UI_bc1_xits"
goto :eof

:clean
REM del can fail on a file another program holds open; retry, then warn.
set "tries=0"
:clean_try
if not exist "%U%" mkdir "%U%"
REM dir /a-d fails (errorlevel 1) once no files are left.
dir /b /a-d "%U%" >nul 2>&1 || goto :eof
if %tries% GEQ 5 goto :clean_fail
set /a tries+=1
ping -n 2 127.0.0.1 >nul
del /f /q "%U%\*" >nul 2>&1
goto :clean_try
:clean_fail
call :warn "Old files in UI\ could not be deleted. Close Civilization V and run ui_check.bat again."
goto :eof

:check
REM %1 = ui or eui, %2 = file in tmp\%1, %3 = name in UI\ (default: %2)
set "src=%T%\%~1\%~2"
set "dst=%U%\%~2"
set "what=%~1\%~2"
if not "%~3"=="" set "dst=%U%\%~3"
if not "%~3"=="" set "what=%~1\%~2 as %~3"
set /a expected+=1
set "tries=0"
:check_try
REM fc /b: 0 = same, 1 = different, 2 = couldn't compare (a file is missing or held
REM open, or Wine's fc, which has no /b). On 2, fall back to comparing sizes.
fc /b "%src%" "%dst%" >nul 2>&1
if errorlevel 2 goto :check_size
if errorlevel 1 goto :check_retry
goto :check_ok
:check_size
set "srcsize=none"
set "dstsize=missing"
for %%A in ("%src%") do set "srcsize=%%~zA"
if exist "%dst%" for %%A in ("%dst%") do set "dstsize=%%~zA"
if "%srcsize%"=="%dstsize%" goto :check_ok
:check_retry
if %tries% GEQ 5 goto :check_fail
set /a tries+=1
ping -n 2 127.0.0.1 >nul
copy /y "%src%" "%dst%" >nul 2>&1
goto :check_try
:check_ok
set /a copied+=1
echo   %what%
>> "%R%" echo %what%
goto :eof
:check_fail
set /a fails+=1
echo   FAILED: %what%
>> "%R%" echo FAILED: %what%
goto :eof

:warn
set /a warns+=1
echo WARNING: %~1
>> "%R%" echo WARNING: %~1
goto :eof

:summary
> "%U%\KekUiCheck.lua" echo KekUiCheck = { fails = %fails%, warns = %warns%, files = %copied%, expected = %expected% }
echo.
if %fails% GTR 0 goto :summary_fail
>> "%R%" echo result: OK, %copied% files, %warns% warnings
if %warns% GTR 0 echo %warns% warning(s) above.
echo ui_check OK: %copied% files in UI\ (EUI: %euiname%)
if not defined nopause ping -n 6 127.0.0.1 >nul
goto :eof
:summary_fail
>> "%R%" echo result: FAILED, %fails% of %expected% files, %warns% warnings
echo ui_check FAILED: %fails% of %expected% files could not be copied into UI\
echo Close Civilization V and run ui_check.bat again. If it still fails, send
echo ui_check_result.txt from the KEK Mod folder to the KEK Mod team.
if not defined nopause ping -n 31 127.0.0.1 >nul
goto :eof
