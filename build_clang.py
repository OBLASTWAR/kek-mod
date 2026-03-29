import os
import subprocess
import sys
from enum import Enum
import typing
import time
import tempfile
from pathlib import Path
import argparse
from queue import Queue

class Config(Enum):
    Release = 0
    Debug = 1

CORE_DLL = 'CvGameCore_Expansion2'
PROJECT_DIR = Path().resolve()
SDK_VERSION = '7.0A'  # 7.0 has no libs on this machine; 7.0A (bundled with VS2010) does
LLVM_BIN = r'C:\Program Files\LLVM\bin'
INCLUDE_PATHS = [
    rf'C:\Program Files (x86)\Microsoft SDKs\Windows\v{SDK_VERSION}\Include',
    rf'C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\include'
]
LIB_PATHS = [
    rf'C:\Program Files (x86)\Microsoft SDKs\Windows\v{SDK_VERSION}\Lib'
]
BUILD_DIR = {
    Config.Release: 'clang-build\\Release',
    Config.Debug:   'clang-build\\Debug',
}
OUT_DIR = {
    Config.Release: 'clang-output\\Release',
    Config.Debug:   'clang-output\\Debug',
}
LIBS = [
    'CvGameCoreDLL_Expansion2\\CvWorldBuilderMap\\lib\\CvWorldBuilderMapWin32.lib',
    'CvGameCoreDLL_Expansion2\\CvGameCoreDLLUtil\\lib\\CvGameCoreDLLUtilWin32.lib',
    'CvGameCoreDLL_Expansion2\\CvLocalization\\lib\\CvLocalizationWin32.lib',
    'CvGameCoreDLL_Expansion2\\CvGameDatabase\\lib\\CvGameDatabaseWin32.lib',
    'CvGameCoreDLL_Expansion2\\FirePlace\\lib\\FireWorksWin32.lib',
    'CvGameCoreDLL_Expansion2\\FirePlace\\lib\\FLuaWin32.lib',
    'CvGameCoreDLL_Expansion2\\ThirdPartyLibs\\Lua51\\lib\\lua51_Win32.lib',
    'CvGameCoreDLL_Expansion2\\ThirdPartyLibs\\winsqlite\\winsqlite3.lib',
]
DEFAULT_LIBS = [
    'winmm.lib',
    'kernel32.lib',
    'user32.lib',
    'gdi32.lib',
    'winspool.lib',
    'comdlg32.lib',
    'advapi32.lib',
    'shell32.lib',
    'ole32.lib',
    'oleaut32.lib',
    'uuid.lib',
    'odbc32.lib',
    'odbccp32.lib',
    'msvcrt.lib',
]
DEF_FILE = 'CvGameCoreDLL_Expansion2\\CvGameCoreDLL.def'
INCLUDE_DIRS = [
    'CvGameCoreDLL_Expansion2',
    'CvGameCoreDLL_Expansion2\\CvWorldBuilderMap\\include',
    'CvGameCoreDLL_Expansion2\\CvGameCoreDLLUtil\\include',
    'CvGameCoreDLL_Expansion2\\CvLocalization\\include',
    'CvGameCoreDLL_Expansion2\\CvGameDatabase\\include',
    'CvGameCoreDLL_Expansion2\\FirePlace\\include',
    'CvGameCoreDLL_Expansion2\\FirePlace\\include\\FireWorks',
    'CvGameCoreDLL_Expansion2\\ThirdPartyLibs\\Lua51\\include',
    'CvGameCoreDLL_Expansion2\\ThirdPartyLibs',
]
SHARED_PREDEFS = [
    'FXS_IS_DLL',
    'WIN32',
    '_WINDOWS',
    '_USRDLL',
    'CVGAMECOREDLL_EXPORTS',
    'FINAL_RELEASE',
    '_CRT_SECURE_NO_WARNINGS',
    '_WINDLL',
]
RELEASE_PREDEFS = SHARED_PREDEFS + ['NDEBUG']
DEBUG_PREDEFS   = SHARED_PREDEFS + ['_DEBUG']
PREDEFS = {
    Config.Release: RELEASE_PREDEFS,
    Config.Debug:   DEBUG_PREDEFS,
}
CL_SUPPRESS = [
    'invalid-offsetof',
    'tautological-constant-out-of-range-compare',
    'comment',
    'c++11-narrowing',
]
PCH_CPP = 'CvGameCoreDLL_Expansion2\\_precompile.cpp'
PCH_H   = 'CvGameCoreDLLPCH.h'
PCH     = 'CvGameCoreDLLPCH.pch'
CPP = [
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaArea.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaArgsHandle.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaCity.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaDeal.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaEnums.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaFractal.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaGame.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaGameInfo.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaLeague.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaMap.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaPlayer.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaPlot.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaSupport.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaTeam.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaTeamTech.cpp',
    'CvGameCoreDLL_Expansion2\\Lua\\CvLuaUnit.cpp',
    'CvGameCoreDLL_Expansion2\\CvAIOperation.cpp',
    'CvGameCoreDLL_Expansion2\\CvAStar.cpp',
    'CvGameCoreDLL_Expansion2\\CvAchievementUnlocker.cpp',
    'CvGameCoreDLL_Expansion2\\CvAdvisorCounsel.cpp',
    'CvGameCoreDLL_Expansion2\\CvAdvisorRecommender.cpp',
    'CvGameCoreDLL_Expansion2\\CvArea.cpp',
    'CvGameCoreDLL_Expansion2\\CvArmyAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvBarbarians.cpp',
    'CvGameCoreDLL_Expansion2\\CvBeliefClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvBuilderTaskingAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvBuildingClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvBuildingProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvCity.cpp',
    'CvGameCoreDLL_Expansion2\\CvCityAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvCityCitizens.cpp',
    'CvGameCoreDLL_Expansion2\\CvCityConnections.cpp',
    'CvGameCoreDLL_Expansion2\\CvCityManager.cpp',
    'CvGameCoreDLL_Expansion2\\CvCitySpecializationAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvCityStrategyAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvCultureClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvDangerPlots.cpp',
    'CvGameCoreDLL_Expansion2\\CvDatabaseUtility.cpp',
    'CvGameCoreDLL_Expansion2\\CvDealAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvDealClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvDiplomacyAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvDiplomacyRequests.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllBuildInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllBuildingInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllCity.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllCivilizationInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllColorInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllCombatInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllContext.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllDatabaseUtility.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllDeal.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllDealAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllDiplomacyAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllDlcPackageInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllEraInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllFeatureInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllGame.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllGameAsynch.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllGameDeals.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllGameOptionInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllGameSpeedInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllHandicapInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllImprovementInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllInterfaceModeInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllLeaderheadInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllMap.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllMinorCivInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllMissionData.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllMissionInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllNetInitInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllNetLoadGameInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllNetMessageHandler.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllNetworkSyncronization.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPathFinderUpdate.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPlayer.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPlayerColorInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPlayerOptionInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPlot.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPolicyInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPreGame.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllPromotionInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllRandom.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllResourceInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllScriptSystemUtility.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllTeam.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllTechInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllTerrainInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllUnit.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllUnitCombatClassInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllUnitInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllVictoryInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllWorldBuilderMapLoader.cpp',
    'CvGameCoreDLL_Expansion2\\CvDllWorldInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvEconomicAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvEmphasisClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvEspionageClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvFlavorManager.cpp',
    'CvGameCoreDLL_Expansion2\\CvFractal.cpp',
    'CvGameCoreDLL_Expansion2\\CvGame.cpp',
    'CvGameCoreDLL_Expansion2\\CvGameCoreDLL.cpp',
    'CvGameCoreDLL_Expansion2\\CvGameCoreEnumSerialization.cpp',
    'CvGameCoreDLL_Expansion2\\CvGameCoreUtils.cpp',
    'CvGameCoreDLL_Expansion2\\CvGameQueries.cpp',
    'CvGameCoreDLL_Expansion2\\CvGameTextMgr.cpp',
    'CvGameCoreDLL_Expansion2\\CvGlobals.cpp',
    'CvGameCoreDLL_Expansion2\\CvGoodyHuts.cpp',
    'CvGameCoreDLL_Expansion2\\CvGrandStrategyAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvHomelandAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvImprovementClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvInfos.cpp',
    'CvGameCoreDLL_Expansion2\\CvInfosSerializationHelper.cpp',
    'CvGameCoreDLL_Expansion2\\CvInternalGameCoreUtils.cpp',
    'CvGameCoreDLL_Expansion2\\CvMap.cpp',
    'CvGameCoreDLL_Expansion2\\CvMapGenerator.cpp',
    'CvGameCoreDLL_Expansion2\\CvMilitaryAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvMinorCivAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvNetworkTest.cpp',
    'CvGameCoreDLL_Expansion2\\CvNotificationClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvNotifications.cpp',
    'CvGameCoreDLL_Expansion2\\CvPlayer.cpp',
    'CvGameCoreDLL_Expansion2\\CvPlayerAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvPlayerManager.cpp',
    'CvGameCoreDLL_Expansion2\\CvPlot.cpp',
    'CvGameCoreDLL_Expansion2\\CvPlotManager.cpp',
    'CvGameCoreDLL_Expansion2\\CvPolicyAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvPolicyClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvPopupInfoSerialization.cpp',
    'CvGameCoreDLL_Expansion2\\CvPopupReturn.cpp',
    'CvGameCoreDLL_Expansion2\\CvPreGame.cpp',
    'CvGameCoreDLL_Expansion2\\CvProcessProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvProjectClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvProjectProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvPromotionClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvRandom.cpp',
    'CvGameCoreDLL_Expansion2\\CvReligionClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvReplayInfo.cpp',
    'CvGameCoreDLL_Expansion2\\CvReplayMessage.cpp',
    'CvGameCoreDLL_Expansion2\\CvSiteEvaluationClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvStartPositioner.cpp',
    'CvGameCoreDLL_Expansion2\\CvStructs.cpp',
    'CvGameCoreDLL_Expansion2\\CvTacticalAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvTacticalAnalysisMap.cpp',
    'CvGameCoreDLL_Expansion2\\CvTargeting.cpp',
    'CvGameCoreDLL_Expansion2\\CvTeam.cpp',
    'CvGameCoreDLL_Expansion2\\CvTechAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvTechClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvTradeClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvTraitClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvTreasury.cpp',
    'CvGameCoreDLL_Expansion2\\CvTypes.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnit.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnitClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnitCombat.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnitCycler.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnitMission.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnitMovement.cpp',
    'CvGameCoreDLL_Expansion2\\CvUnitProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvVotingClasses.cpp',
    'CvGameCoreDLL_Expansion2\\CvWonderProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\\CvWorldBuilderMapLoader.cpp',
    'CvGameCoreDLL_Expansion2\\cvStopWatch.cpp',
]

class TaskResult:
    commands: typing.Union[str, list[str]]
    returncode: int

    def __init__(self, commands: typing.Union[str, list[str]]):
        self.commands = commands
        self.returncode = None

class Task:
    proc: subprocess.Popen
    result: TaskResult
    def __init__(self, commands: typing.Union[str, list[str]], env: typing.Optional[dict[str, str]]=None, shell: bool=False, log: any=None):
        self.proc = subprocess.Popen(commands, stdout=log, stderr=log, env=env, shell=shell)
        self.result = TaskResult(commands)

    def poll(self) -> typing.Optional[TaskResult]:
        if (returncode := self.proc.poll()) is not None:
            self.result.returncode = returncode
            return self.result
        return None

class TaskMan:
    pending: Queue
    def __init__(self):
        self.pending = Queue()

    def spawn(self, commands: typing.Union[str, list[str]], env: typing.Optional[dict[str, str]]=None, shell: bool=False, log: any=None):
        task = Task(commands, env=env, shell=shell, log=log)
        self.pending.put(task)

    def wait(self) -> list[TaskResult]:
        results: list[TaskResult] = []
        while not self.pending.empty():
            task = self.pending.get()
            if result := task.poll():
                results.append(result)
            else:
                self.pending.put(task)
        return results

def set_environment(sdk_version: str):
    sdk_path = rf'C:\Program Files (x86)\Microsoft SDKs\Windows\v{sdk_version}'
    vs9_path = r'C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC'
    vs10_path = r'C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC'
    vs10_ide  = r'C:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\IDE'
    os.environ['INCLUDE'] = f'{sdk_path}\\Include;{vs9_path}\\include'
    # include VS2010 libs so MSVC link.exe can find CRT / ATL libs
    os.environ['LIB']     = f'{sdk_path}\\Lib;{vs9_path}\\lib;{vs10_path}\\lib'
    # mspdb100.dll lives in Common7\IDE, not VC\bin
    os.environ['PATH']    = f'{LLVM_BIN};{sdk_path}\\Bin;{vs9_path}\\bin;{vs10_path}\\bin;{vs10_ide};' + os.environ['PATH']

def build_cl_config_args(config: Config) -> list[str]:
    args = ['-m32', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t']
    if config == Config.Release:
        args += ['/Ox', '/Ob2', '/Zo']
    else:
        args += ['/Od', '/Oy-']
    for predef in PREDEFS[config]:
        args.append(f'/D{predef}')
    for include_dir in INCLUDE_DIRS:
        args.append(f'/I"{os.path.join(PROJECT_DIR, include_dir)}"')
    for include_path in INCLUDE_PATHS:
        args.append(f'-external:I"{include_path}"')
    for suppress in CL_SUPPRESS:
        args.append(f'-Wno-{suppress}')
    return args

def build_link_config_args(config: Config) -> list[str]:
    args = ['/MACHINE:x86', '/DLL', '/DEBUG', '/DYNAMICBASE', '/NXCOMPAT',
            '/SUBSYSTEM:WINDOWS', '/MANIFEST', '/FORCE:MULTIPLE',
            f'/DEF:"{os.path.join(PROJECT_DIR, DEF_FILE)}"']
    if config == Config.Release:
        args += ['/OPT:REF', '/OPT:ICF']
    return args

def prepare_dirs(build_dir: Path, out_dir: Path):
    build_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    for cpp in CPP:
        cpp_dir = build_dir.joinpath(Path(cpp).parent)
        cpp_dir.mkdir(parents=True, exist_ok=True)

def build_clang_cpp(cl: str, cl_args: str, build_dir: Path, log: typing.IO):
    src = PROJECT_DIR / 'clang.cpp'
    if not src.exists():
        print('clang.cpp not found, skipping')
        return
    print('building clang.cpp...')
    start = time.time()
    out = build_dir / 'clang.obj'
    cp = subprocess.run(f'{cl} "{src}" /Fo"{out}" {cl_args}', capture_output=True)
    log.write(f'==== {src} ====\n'.encode())
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('failed to build clang.cpp - see build log')
        sys.exit(1)
    print(f'clang.cpp done in {time.time() - start:.1f}s')

def build_pch(cl: str, cl_args: str, pch_path: Path, build_dir: Path, log: typing.IO):
    print('building precompiled header...')
    start = time.time()
    pch_src = PROJECT_DIR / PCH_CPP
    out = build_dir.joinpath(PCH_CPP).with_suffix('.obj')
    cp = subprocess.run(f'{cl} "{pch_src}" /Fo"{out}" /Yc"{PCH_H}" /Fp"{pch_path}" {cl_args}', capture_output=True)
    log.write(f'==== {pch_src} ====\n'.encode())
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('failed to build precompiled header - see build log')
        sys.exit(1)
    print(f'precompiled header done in {time.time() - start:.1f}s')

def build_cpps(cl: str, cl_args: str, pch_path: Path, build_dir: Path, log: typing.IO):
    print('building cpps...')
    start = time.time()
    build_tasks = TaskMan()
    logs: dict[Path, typing.IO] = {}
    try:
        for cpp in CPP:
            cpp_src = PROJECT_DIR / cpp
            cpp_log = tempfile.TemporaryFile()
            logs[cpp_src] = cpp_log
            out = build_dir.joinpath(cpp).with_suffix('.obj')
            build_tasks.spawn(f'{cl} "{cpp_src}" /Fo"{out}" /Yu"{PCH_H}" /Fp"{pch_path}" {cl_args}', log=cpp_log)
        build_results = build_tasks.wait()
        for cpp_src, cpp_log in logs.items():
            cpp_log.seek(0)
            log.write(f'==== {cpp_src} ====\n'.encode())
            log.write(cpp_log.read())
        log.flush()
        failed = sum(1 for r in build_results if r.returncode != 0)
        if failed:
            print(f'{failed} cpp(s) failed - see build log')
            sys.exit(1)
        print(f'cpps done in {time.time() - start:.1f}s')
    finally:
        del logs

def link_dll(link: str, link_args: list[str], build_dir: Path, out_dir: Path, log: typing.IO):
    print('linking dll...')
    start = time.time()
    rsp = build_dir / 'link.rsp'
    with open(rsp, 'w') as f:
        out_dll = out_dir / f'{CORE_DLL}.dll'
        out_pdb = out_dir / f'{CORE_DLL}.pdb'
        f.write(f'/OUT:"{out_dll}"\n/PDB:"{out_pdb}"\n')
        f.write('\n'.join(link_args) + '\n')
        for lib_path in LIB_PATHS:
            f.write(f'/LIBPATH:"{lib_path}"\n')
        for lib in LIBS:
            p = PROJECT_DIR / lib
            if not p.exists():
                log.write(f'Warning: library not found: {p}\n'.encode())
            f.write(f'"{p}"\n')
        for lib in DEFAULT_LIBS:
            f.write(f'{lib}\n')
        clang_obj = build_dir / 'clang.obj'
        if clang_obj.exists():
            f.write(f'"{clang_obj}"\n')
        pch_obj = build_dir.joinpath(PCH_CPP).with_suffix('.obj')
        f.write(f'"{pch_obj}"\n')
        for cpp in CPP:
            f.write(f'"{build_dir.joinpath(cpp).with_suffix(".obj")}"\n')
    command = f'{link} @"{rsp}"'
    cp = subprocess.run(command, capture_output=True)
    log.write(f'==== {CORE_DLL}.dll ====\n'.encode())
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('linking failed - see build log')
        sys.exit(1)
    print(f'linking done in {time.time() - start:.1f}s')

# ── Main ──────────────────────────────────────────────────────────────────────
set_environment(SDK_VERSION)

arg_parser = argparse.ArgumentParser(description='Build kek-mod DLL using clang-cl.')
arg_parser.add_argument('--config', default='release', choices=['release', 'debug'])
args = arg_parser.parse_args()
config = Config.Release if args.config == 'release' else Config.Debug

cl   = rf'{LLVM_BIN}\clang-cl.exe'
# lld-link cannot read MSVC /GL (LTCG) precompiled libs; use MSVC link.exe instead
link = r'C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\link.exe'
build_dir = PROJECT_DIR / BUILD_DIR[config]
out_dir   = PROJECT_DIR / OUT_DIR[config]
cl_args   = ' '.join(build_cl_config_args(config))
link_args = build_link_config_args(config)
pch_path  = build_dir / PCH

prepare_dirs(build_dir, out_dir)
log = open(out_dir / 'build.log', 'w+b')
try:
    build_clang_cpp(cl, cl_args, build_dir, log)
    build_pch(cl, cl_args, pch_path, build_dir, log)
    build_cpps(cl, cl_args, pch_path, build_dir, log)
    link_dll(link, link_args, build_dir, out_dir, log)
    print(f'\nBUILD SUCCEEDED -> {out_dir / (CORE_DLL + ".dll")}')
finally:
    log.close()
