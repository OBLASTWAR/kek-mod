@echo off
setlocal

:: ============================================================
:: kek-mod DLL build script
:: Requires: VS2008 SP1 + VS2010 SP1 + VS2019/2022 (with C++ workload)
:: Usage: build.bat [Release|Debug]
::        Defaults to Release (Mod config)
:: ============================================================

set SLN=%~dp0CvGameCoreDLL_Expansion2.vs2013.sln
set CONFIG=Mod
set PLATFORM=Win32

:: Find MSBuild -- try VS2022, VS2019, then fallback
set MSBUILD=
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles(x86)%\MSBuild\14.0\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles(x86)%\MSBuild\14.0\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%ProgramFiles(x86)%\MSBuild\12.0\Bin\MSBuild.exe" (
    set MSBUILD="%ProgramFiles(x86)%\MSBuild\12.0\Bin\MSBuild.exe"
    goto :found_msbuild
)
if exist "%SystemRoot%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" (
    set MSBUILD="%SystemRoot%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe"
    goto :found_msbuild
)

echo ERROR: Could not find MSBuild.exe.
echo Install Visual Studio 2019 or 2022 with "Desktop development with C++" workload,
echo or ensure MSBuild 14.0 / .NET Framework 4.0 MSBuild is available.
exit /b 1

:found_msbuild
echo Using MSBuild: %MSBUILD%

:: Check that VS2008 compiler exists (required for v90 toolset)
if not exist "%VS90COMNTOOLS%..\IDE\devenv.exe" (
    if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\VC\bin\cl.exe" (
        echo ERROR: Visual C++ 2008 ^(v90 toolset^) not found.
        echo Please install Visual Studio 2008 Express with SP1 from:
        echo   https://web.archive.org/web/20250618154620/https://download.microsoft.com/download/E/8/E/E8EEB394-7F42-4963-A2D8-29559B738298/VS2008ExpressWithSP1ENUX1504728.iso
        echo Then install Visual Studio 2010 Express + SP1 for MSBuild v4 toolset integration.
        exit /b 1
    )
)

:: VCTargetsPath for the v4.0 C++ targets (installed by VS2010)
set VCTARGETS=%ProgramFiles(x86)%\MSBuild\Microsoft.Cpp\v4.0

echo Building %CONFIG%^|%PLATFORM%...
echo Solution: %SLN%
echo.

%MSBUILD% "%SLN%" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:VCTargetsPath="%VCTARGETS%" /m /nologo /verbosity:minimal

if %ERRORLEVEL% neq 0 (
    echo.
    echo BUILD FAILED ^(exit code %ERRORLEVEL%^)
    exit /b %ERRORLEVEL%
)

echo.
echo BUILD SUCCEEDED
call "%~dp0verify_build.bat" quiet
exit /b 0
