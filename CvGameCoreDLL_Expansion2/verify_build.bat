@echo off
setlocal enabledelayedexpansion
:: ============================================================
:: verify_build.bat  -- check that the DLL was produced
:: Called automatically by build.bat, or run standalone.
:: Usage: verify_build.bat [quiet]
:: ============================================================

set QUIET=%1
set BUILD_DIR=%~dp0BuildOutput\VS2013_ModWin32
set DLL_NAME=CvGameCoreDLL_Expansion2Win32Mod.dll
set DLL_PATH=%BUILD_DIR%\%DLL_NAME%
set PDB_PATH=%BUILD_DIR%\CvGameCoreDLL_Expansion2Win32Mod.pdb

set PASS=0
set FAIL=0

call :check_file "%DLL_PATH%" "DLL output"
call :check_file "%PDB_PATH%" "PDB (debug symbols)"

echo.
if !FAIL! equ 0 (
    echo [VERIFY] All checks PASSED.
    if "!QUIET!"=="" (
        echo.
        echo Output: !DLL_PATH!
        echo.
        echo To deploy for testing:
        echo   Copy !DLL_NAME! to your mod's root folder
        echo   ^(e.g. Documents\My Games\Sid Meier's Civilization V\MODS\kek-mod\^)
        echo   and replace the existing DLL there.
    )
    exit /b 0
) else (
    echo [VERIFY] !FAIL! check^(s^) FAILED. Run build.bat first.
    exit /b 1
)

:check_file
set FILE=%~1
set LABEL=%~2
if exist "!FILE!" (
    for %%A in ("!FILE!") do set SIZE=%%~zA
    echo [PASS] !LABEL!: !FILE! ^(!SIZE! bytes^)
    set /a PASS+=1
) else (
    echo [FAIL] !LABEL!: NOT FOUND at !FILE!
    set /a FAIL+=1
)
goto :eof
