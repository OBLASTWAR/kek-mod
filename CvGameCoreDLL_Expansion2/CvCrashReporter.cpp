//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvCrashReporter.cpp
//! \brief   Process-wide crash filter + minidump writer for kek-mod.
//!
//! Ported from the Community Patch DLL (CvGlobals.cpp, MINIDUMP_MOD by
//! terkhen/ls612, memory census + Wine detection by the VP team), with
//! kek-mod changes per plan/CRASH_REPORTER_PLAN.md Phase 1:
//!   - dumps go to Documents\My Games\Sid Meier's Civilization 5\kekmod\
//!     crashlogs\ (always writable; survives game reinstalls) instead of
//!     the game install dir
//!   - kek_<timestamp>_<modversion>_<kind>.dmp naming, version via
//!     CvHttp_GetModVersion()
//!   - a JSON metadata sidecar next to every dump (the Phase 3 uploader
//!     reads it; until then it makes hand-reported dumps self-describing)
//!   - game context (turn, MP, players, map GUID) captured SEH-guarded --
//!     the heap may be corrupt mid-crash, so a fault while reading game
//!     state must not lose the dump
//!
//! The crash path is allocation-free: static buffers only, no CRT heap.
//! MessageBoxA and dbghelp are resolved at runtime so this file adds no
//! static link dependencies.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include "CvGameCoreDLLPCH.h"   // Must be first (precompiled header)
#include "CvCrashReporter.h"
#include "CvHttpUtils.h"        // CvHttp_GetModVersion()
#include "CvPreGame.h"

#if defined(_WIN32) && defined(NQM_MINIDUMPS)

#include <dbghelp.h>

// The VS2008-era SDK's dbghelp.h predates some MINIDUMP_TYPE members; the
// values match the Win7+ SDK. These are enum members when present, never
// macros, so the #define harmlessly shadows an existing member with the
// identical value.
#ifndef MiniDumpWithHandleData
#define MiniDumpWithHandleData             ((MINIDUMP_TYPE)0x00000004)
#endif
#ifndef MiniDumpWithUnloadedModules
#define MiniDumpWithUnloadedModules        ((MINIDUMP_TYPE)0x00000020)
#endif
#ifndef MiniDumpWithProcessThreadData
#define MiniDumpWithProcessThreadData      ((MINIDUMP_TYPE)0x00000100)
#endif
#ifndef MiniDumpWithThreadInfo
#define MiniDumpWithThreadInfo             ((MINIDUMP_TYPE)0x00001000)
#endif
#ifndef MiniDumpIgnoreInaccessibleMemory
#define MiniDumpIgnoreInaccessibleMemory   ((MINIDUMP_TYPE)0x00020000)
#endif

#ifndef GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS       0x00000004
#endif

// Crash artifacts live under Documents like the auto-saves CvHttpUtils
// reads (same %USERPROFILE%\Documents convention); the Civ5 "My Games"
// folder is guaranteed to exist on any machine the game has run on.
#define KEK_KEKMOD_SUBPATH    "\\Documents\\My Games\\Sid Meier's Civilization 5\\kekmod"
#define KEK_CRASHLOGS_NAME    "crashlogs"

// ---------------------------------------------------------------------------
// dbghelp, loaded on demand (crash time), never statically linked
// ---------------------------------------------------------------------------

typedef BOOL (WINAPI* PFN_MiniDumpWriteDump)(
    HANDLE hProcess,
    DWORD ProcessId,
    HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

static HMODULE               g_hDbgHelp            = NULL;
static PFN_MiniDumpWriteDump g_pfnMiniDumpWriteDump = NULL;
static char                  g_szDbgHelpPath[MAX_PATH] = {0};

// System32 first (newer dbghelp on Win10/11), then default search order
// (which would find a copy in the game directory).
static bool LoadBestDbgHelp()
{
    if (g_hDbgHelp)
        return true;

    char szPath[MAX_PATH];
    if (GetSystemDirectoryA(szPath, MAX_PATH) > 0)
    {
        strcat_s(szPath, MAX_PATH, "\\dbghelp.dll");
        g_hDbgHelp = LoadLibraryA(szPath);
    }
    if (!g_hDbgHelp)
        g_hDbgHelp = LoadLibraryA("dbghelp.dll");
    if (!g_hDbgHelp)
    {
        OutputDebugString("kek crash: failed to load dbghelp.dll\n");
        return false;
    }

    g_pfnMiniDumpWriteDump =
        (PFN_MiniDumpWriteDump)GetProcAddress(g_hDbgHelp, "MiniDumpWriteDump");
    if (!g_pfnMiniDumpWriteDump)
    {
        OutputDebugString("kek crash: no MiniDumpWriteDump in dbghelp.dll\n");
        FreeLibrary(g_hDbgHelp);
        g_hDbgHelp = NULL;
        return false;
    }

    GetModuleFileNameA(g_hDbgHelp, g_szDbgHelpPath, MAX_PATH);
    return true;
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

static char g_szCrashlogsDir[MAX_PATH] = {0};   // filled at install time

// Build (install time) or rebuild+recreate (crash time -- the player may
// have deleted the folder mid-session) the crashlogs directory.
static bool EnsureCrashlogsDir()
{
    if (g_szCrashlogsDir[0] == '\0')
    {
        char szProfile[MAX_PATH] = {0};
        if (!GetEnvironmentVariableA("USERPROFILE", szProfile, sizeof(szProfile)))
            return false;
        _snprintf_s(g_szCrashlogsDir, sizeof(g_szCrashlogsDir), _TRUNCATE,
                    "%s%s\\%s", szProfile, KEK_KEKMOD_SUBPATH, KEK_CRASHLOGS_NAME);
    }

    // Two levels: ...\kekmod, then ...\kekmod\crashlogs. The parents exist
    // on any machine that has launched Civ5. ERROR_ALREADY_EXISTS is fine.
    char szKekDir[MAX_PATH] = {0};
    char szProfile[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("USERPROFILE", szProfile, sizeof(szProfile)))
    {
        _snprintf_s(szKekDir, sizeof(szKekDir), _TRUNCATE,
                    "%s%s", szProfile, KEK_KEKMOD_SUBPATH);
        CreateDirectoryA(szKekDir, NULL);
    }
    CreateDirectoryA(g_szCrashlogsDir, NULL);

    DWORD dwAttr = GetFileAttributesA(g_szCrashlogsDir);
    return dwAttr != INVALID_FILE_ATTRIBUTES && (dwAttr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// ---------------------------------------------------------------------------
// Game context, captured SEH-guarded
// ---------------------------------------------------------------------------

struct KekCrashContext
{
    int  iTurn;             // -1 = unknown / no game
    int  iNetworkMP;        // -1 unknown, else 0/1
    int  iNumHumans;        // -1 unknown
    char szGameId[40];      // map GUID as lowercase hex, "" unknown
    char szMapScript[128];  // "" unknown
    char szSteamId[24];     // local player's steam id, "" unknown
};

// Copy that keeps the JSON below well-formed no matter what is in the
// source string: printable ASCII only, quote/backslash dropped.
static void SanitizedCopy(char* pszOut, size_t nOut, const char* pszIn)
{
    size_t iW = 0;
    for (const char* p = pszIn; p && *p; ++p)
    {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20)
            continue;
        // Escape rather than drop -- map script paths ("Assets\Maps\X.lua")
        // are backslash-heavy; dropping them silently mangled the path.
        if (c == '"' || c == '\\')
        {
            if (iW + 2 >= nOut)
                break;
            pszOut[iW++] = '\\';
            pszOut[iW++] = (char)c;
        }
        else
        {
            if (iW + 1 >= nOut)
                break;
            pszOut[iW++] = (char)c;
        }
    }
    pszOut[iW] = '\0';
}

// "persona@76561198xxxxxxxxx" -> steam id part (same convention
// CvHttpUtils.cpp parses), "" if not that shape.
static void CopyLocalSteamId(char* pszOut, size_t nOut)
{
    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;
    const CvString& strNick = CvPreGame::nickname(eActive);
    const char* pAt = strrchr(strNick.c_str(), '@');
    if (!pAt)
        return;
    const char* pId = pAt + 1;
    if (strlen(pId) != 17 || strncmp(pId, "7656119", 7) != 0)
        return;
    for (const char* p = pId; *p; ++p)
        if (*p < '0' || *p > '9')
            return;
    SanitizedCopy(pszOut, nOut, pId);
}

// May fault anywhere -- only ever called inside the __try below.
static void ReadContextUnsafe(KekCrashContext* p)
{
    CvGame& kGame = GC.getGame();
    p->iTurn      = kGame.getGameTurn();
    p->iNetworkMP = kGame.isNetworkMultiPlayer() ? 1 : 0;
    p->iNumHumans = kGame.countHumanPlayersAlive();

    GUID guid = GC.getMap().GetGUID();
    _snprintf_s(p->szGameId, sizeof(p->szGameId), _TRUNCATE,
        "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        (unsigned)guid.Data1, (unsigned)guid.Data2, (unsigned)guid.Data3,
        (unsigned)guid.Data4[0], (unsigned)guid.Data4[1], (unsigned)guid.Data4[2],
        (unsigned)guid.Data4[3], (unsigned)guid.Data4[4], (unsigned)guid.Data4[5],
        (unsigned)guid.Data4[6], (unsigned)guid.Data4[7]);

    SanitizedCopy(p->szMapScript, sizeof(p->szMapScript),
                  CvPreGame::mapScriptName().c_str());
    CopyLocalSteamId(p->szSteamId, sizeof(p->szSteamId));
}

// No C++ locals here (C2712): __try and object unwinding don't mix.
static void CaptureGameContext(KekCrashContext* p)
{
    memset(p, 0, sizeof(*p));
    p->iTurn = -1;
    p->iNetworkMP = -1;
    p->iNumHumans = -1;
    __try
    {
        ReadContextUnsafe(p);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Partial context is still context; whatever was filled in stays.
    }
}

// ---------------------------------------------------------------------------
// Description helpers (ported from CP)
// ---------------------------------------------------------------------------

static const char* GetExceptionDescription(DWORD exceptionCode)
{
    switch (exceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:         return "Access Violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "Array Bounds Exceeded";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "Datatype Misalignment";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "Float Divide by Zero";
    case EXCEPTION_FLT_OVERFLOW:             return "Float Overflow";
    case EXCEPTION_FLT_UNDERFLOW:            return "Float Underflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "Illegal Instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "Integer Divide by Zero";
    case EXCEPTION_INT_OVERFLOW:             return "Integer Overflow";
    case EXCEPTION_PRIV_INSTRUCTION:         return "Privileged Instruction";
    case EXCEPTION_STACK_OVERFLOW:           return "Stack Overflow";
    default:                                 return "Unknown Exception";
    }
}

typedef const char* (CDECL* WINE_GET_VERSION)(void);
typedef void (CDECL* WINE_GET_HOST_VERSION)(const char** sysname, const char** release);

// "Windows", or the Wine/Proton version + host when running under Wine
// (kek MP under Proton is a real player population). Native Windows spoofs
// GetVersionEx for old exes, so no build number is reported there.
static void GetOsDescription(char* out, size_t len)
{
    HMODULE hNt = GetModuleHandleA("ntdll.dll");
    if (!hNt)
    {
        _snprintf_s(out, len, _TRUNCATE, "?? (no ntdll)");
        return;
    }
    WINE_GET_VERSION fWineGetVersion =
        (WINE_GET_VERSION)GetProcAddress(hNt, "wine_get_version");
    if (!fWineGetVersion)
    {
        _snprintf_s(out, len, _TRUNCATE, "Windows");
        return;
    }
    WINE_GET_HOST_VERSION fWineGetHostInfo =
        (WINE_GET_HOST_VERSION)GetProcAddress(hNt, "wine_get_host_version");
    const char* host_sysname = NULL;
    const char* host_release = NULL;
    if (fWineGetHostInfo)
        fWineGetHostInfo(&host_sysname, &host_release);
    if (host_sysname && host_release)
        _snprintf_s(out, len, _TRUNCATE, "%s(Wine) - Host: %s %s",
                    fWineGetVersion(), host_sysname, host_release);
    else
        _snprintf_s(out, len, _TRUNCATE, "%s(Wine) - Host: ?", fWineGetVersion());
}

static const char* GetOnlyFilename(const char* in)
{
    const char* ret = strrchr(in, '\\');
    return ret == NULL ? in : (ret + 1);
}

// ---------------------------------------------------------------------------
// 32-bit address-space census (ported from CP)
//
// Address exhaustion is the #1 Civ V crash class; these numbers let the
// server auto-classify those reports instead of us chasing them as bugs.
// ---------------------------------------------------------------------------

struct KekMemStats
{
    size_t committedKB,    reservedKB,    freeKB;
    size_t committedLowKB, reservedLowKB, freeLowKB;    // below 2 GB
    size_t largestFreeKB,  largestFreeLowKB;
};

static void CollectMemStats(KekMemStats* pStats)
{
    memset(pStats, 0, sizeof(*pStats));

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    byte* minAddress = (byte*)si.lpMinimumApplicationAddress;
    byte* maxAddress = (byte*)si.lpMaximumApplicationAddress;

    size_t committed = 0, reserved = 0, free_ = 0;
    size_t committedLow = 0, reservedLow = 0, freeLow = 0;
    size_t largestFree = 0, largestFreeLow = 0;

    byte* currentAddress = minAddress;
    while (currentAddress < maxAddress)
    {
        MEMORY_BASIC_INFORMATION mInfo = {0};
        if (VirtualQuery(currentAddress, &mInfo, sizeof(mInfo)) == 0)
            break;

        bool below2GB = currentAddress < (byte*)0x80000000;
        SIZE_T regionSize = mInfo.RegionSize;
        if (below2GB && currentAddress + regionSize >= (byte*)0x80000000)
            regionSize = (byte*)0x80000000 - currentAddress;
        size_t adjSize = below2GB ? regionSize : 0;

        if (mInfo.State == MEM_COMMIT)
        {
            committed += mInfo.RegionSize;
            committedLow += adjSize;
        }
        else if (mInfo.State == MEM_RESERVE)
        {
            reserved += mInfo.RegionSize;
            reservedLow += adjSize;
        }
        else
        {
            free_ += mInfo.RegionSize;
            freeLow += adjSize;
            if (largestFree < mInfo.RegionSize)
                largestFree = mInfo.RegionSize;
            if (largestFreeLow < adjSize)
                largestFreeLow = adjSize;
        }
        currentAddress += mInfo.RegionSize;
    }

    pStats->committedKB      = committed >> 10;
    pStats->reservedKB       = reserved >> 10;
    pStats->freeKB           = free_ >> 10;
    pStats->committedLowKB   = committedLow >> 10;
    pStats->reservedLowKB    = reservedLow >> 10;
    pStats->freeLowKB        = freeLow >> 10;
    pStats->largestFreeKB    = largestFree >> 10;
    pStats->largestFreeLowKB = largestFreeLow >> 10;
}

// ---------------------------------------------------------------------------
// Dump + sidecar writing
// ---------------------------------------------------------------------------

// kind is "crash" now; Phase 2's watchdog will pass "hang" (pep == NULL).
// On success fills pszDumpPathOut (full path) and returns true.
static bool WriteMiniDumpFile(EXCEPTION_POINTERS* pep, const char* pszKind,
                              const KekCrashContext* pCtx, const char* pszOs,
                              char* pszDumpPathOut, size_t nDumpPathOut)
{
    pszDumpPathOut[0] = '\0';
    if (!LoadBestDbgHelp())
        return false;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char szTimestamp[32];
    _snprintf_s(szTimestamp, sizeof(szTimestamp), _TRUNCATE,
                "%04d%02d%02d_%02d%02d%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    _snprintf_s(pszDumpPathOut, nDumpPathOut, _TRUNCATE,
                "%s\\kek_%s_%s_%s.dmp",
                g_szCrashlogsDir, szTimestamp, CvHttp_GetModVersion(), pszKind);

    HANDLE hFile = CreateFileA(pszDumpPathOut, GENERIC_READ | GENERIC_WRITE,
                               0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == NULL || hFile == INVALID_HANDLE_VALUE)
    {
        pszDumpPathOut[0] = '\0';
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION mdei;
    mdei.ThreadId          = GetCurrentThreadId();
    mdei.ExceptionPointers = pep;
    mdei.ClientPointers    = FALSE;

#ifdef _DEBUG
    // Debug build: everything -- dumps are for the dev box, size is fine.
    MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithThreadInfo |
        MiniDumpWithProcessThreadData |
        MiniDumpIgnoreInaccessibleMemory);
#else
    // Release: all thread stacks + modules + handles, no heap contents.
    // A few hundred KB -- small enough to upload, no player heap data.
    MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(
        MiniDumpNormal |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules |
        MiniDumpWithProcessThreadData |
        MiniDumpWithHandleData |
        MiniDumpIgnoreInaccessibleMemory);
#endif

    // Self-describing dump: same facts as the sidecar, embedded as a
    // comment stream so a dump alone (posted to Discord) is enough.
    static char s_szComment[1024];
    _snprintf_s(s_szComment, sizeof(s_szComment), _TRUNCATE,
                "kek-mod %s report\n"
                "modVersion: %s\n"
                "os: %s\n"
                "turn: %d\nnetworkMP: %d\nnumHumans: %d\n"
                "gameId: %s\nmapScript: %s\n"
                "dbghelp: %s\n",
                pszKind, CvHttp_GetModVersion(), pszOs,
                pCtx->iTurn, pCtx->iNetworkMP, pCtx->iNumHumans,
                pCtx->szGameId, pCtx->szMapScript,
                g_szDbgHelpPath[0] ? g_szDbgHelpPath : "(not loaded)");

    MINIDUMP_USER_STREAM userStream;
    userStream.Type       = CommentStreamA;
    userStream.Buffer     = s_szComment;
    userStream.BufferSize = (ULONG)(strlen(s_szComment) + 1);

    MINIDUMP_USER_STREAM_INFORMATION streamInfo;
    streamInfo.UserStreamCount = 1;
    streamInfo.UserStreamArray = &userStream;

    BOOL bOk = g_pfnMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                      hFile, mdt,
                                      (pep != NULL) ? &mdei : NULL,
                                      &streamInfo, NULL);
    CloseHandle(hFile);

    if (!bOk)
    {
        DeleteFileA(pszDumpPathOut);
        pszDumpPathOut[0] = '\0';
        return false;
    }
    return true;
}

// Sidecar next to the dump: same basename, .json. This is the metadata the
// Phase 3 uploader sends in the X-Crash-Meta header (see the plan doc), so
// field names here ARE the wire contract -- extend, don't rename.
// dwStallMs is 0 for "crash" reports; for "hang" reports it's how long the
// game thread's heartbeat had gone stale, so the server can tell a 91s stall
// from a 10-minute one without re-deriving it from timestamps.
static void WriteSidecarJson(const char* pszDumpPath, const char* pszKind,
                             DWORD dwExceptionCode, const char* pszModule,
                             DWORD dwOffset, DWORD dwStallMs,
                             const KekCrashContext* pCtx,
                             const char* pszOs, const KekMemStats* pMem)
{
    if (pszDumpPath[0] == '\0')
        return;

    char szJsonPath[MAX_PATH];
    _snprintf_s(szJsonPath, sizeof(szJsonPath), _TRUNCATE, "%s", pszDumpPath);
    size_t nLen = strlen(szJsonPath);
    if (nLen < 4)
        return;
    strcpy_s(szJsonPath + nLen - 4, sizeof(szJsonPath) - (nLen - 4), ".json");

    char szModuleClean[MAX_PATH];
    SanitizedCopy(szModuleClean, sizeof(szModuleClean), GetOnlyFilename(pszModule));

    static char s_szJson[2048];
    _snprintf_s(s_szJson, sizeof(s_szJson), _TRUNCATE,
        "{"
        "\"kind\":\"%s\","
        "\"modVersion\":\"%s\","
        "\"exceptionCode\":\"0x%08X\","
        "\"module\":\"%s\","
        "\"offset\":\"0x%08X\","
        "\"stallMs\":%u,"
        "\"os\":\"%s\","
        "\"turn\":%d,"
        "\"isNetworkMP\":%d,"
        "\"numHumans\":%d,"
        "\"gameId\":\"%s\","
        "\"mapScript\":\"%s\","
        "\"uploaderSteamId\":\"%s\","
        "\"memCommittedKB\":%u,"
        "\"memCommittedLowKB\":%u,"
        "\"memLargestFreeKB\":%u,"
        "\"memLargestFreeLowKB\":%u"
        "}\n",
        pszKind, CvHttp_GetModVersion(), (unsigned)dwExceptionCode,
        szModuleClean, (unsigned)dwOffset, (unsigned)dwStallMs, pszOs,
        pCtx->iTurn, pCtx->iNetworkMP, pCtx->iNumHumans,
        pCtx->szGameId, pCtx->szMapScript, pCtx->szSteamId,
        (unsigned)pMem->committedKB, (unsigned)pMem->committedLowKB,
        (unsigned)pMem->largestFreeKB, (unsigned)pMem->largestFreeLowKB);

    HANDLE hFile = CreateFileA(szJsonPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == NULL || hFile == INVALID_HANDLE_VALUE)
        return;
    DWORD dwWritten = 0;
    WriteFile(hFile, s_szJson, (DWORD)strlen(s_szJson), &dwWritten, NULL);
    CloseHandle(hFile);
}

// ---------------------------------------------------------------------------
// The filter
// ---------------------------------------------------------------------------

// This PCH chain doesn't pull in winuser.h's MB_* constants; declared locally
// rather than widening the include (MessageBoxA itself is already resolved
// at runtime below, not linked, so this keeps the same no-new-deps approach).
#ifndef MB_OK
#define MB_OK 0x00000000L
#endif
#ifndef MB_ICONERROR
#define MB_ICONERROR 0x00000010L
#endif
#ifndef MB_SYSTEMMODAL
#define MB_SYSTEMMODAL 0x00001000L
#endif
// Civ V grabs exclusive fullscreen very early/aggressively, which can bury a
// plain MessageBoxA behind the game surface and force an (infamously slow,
// in this game) alt-tab to reach it. These two pull it to the front.
#ifndef MB_TOPMOST
#define MB_TOPMOST 0x00040000L
#endif
#ifndef MB_SETFOREGROUND
#define MB_SETFOREGROUND 0x00010000L
#endif

typedef int (WINAPI* PFN_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);
typedef int (WINAPI* PFN_ShowCursor)(BOOL);

// Civ V hides the system cursor for its own fullscreen rendering (ShowCursor
// is an internal reference count, not a simple on/off), so a native dialog
// can be unclickable-by-mouse even when it's correctly in the foreground --
// confirmed live: had to Tab+Enter through it blind. bShow=TRUE before
// showing a dialog, bShow=FALSE right after to give back exactly what was
// taken (ShowCursor's count nets to zero either way) and not leave the
// cursor visible over the game afterward.
static void SetSystemCursorVisible(HMODULE hUser32, BOOL bShow)
{
    if (!hUser32)
        return;
    PFN_ShowCursor pfnShowCursor = (PFN_ShowCursor)GetProcAddress(hUser32, "ShowCursor");
    if (pfnShowCursor)
        pfnShowCursor(bShow);
}

static void ShowCrashDialog(const char* pszText)
{
    // user32 is not in this project's link deps; resolve at runtime.
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (!hUser32)
        return;
    PFN_MessageBoxA pfnMessageBoxA =
        (PFN_MessageBoxA)GetProcAddress(hUser32, "MessageBoxA");
    if (pfnMessageBoxA)
    {
        SetSystemCursorVisible(hUser32, TRUE);
        pfnMessageBoxA(NULL, pszText, "kek-mod: game crashed",
                       MB_OK | MB_ICONERROR | MB_SYSTEMMODAL |
                       MB_TOPMOST | MB_SETFOREGROUND);
        SetSystemCursorVisible(hUser32, FALSE);
    }
}

static volatile LONG g_lInFilter = 0;

static LONG WINAPI KekCrashFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    // A crash inside our own crash handling (or a second thread crashing
    // while we work) must not recurse; hand it to the default handler.
    if (InterlockedCompareExchange(&g_lInFilter, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    EnsureCrashlogsDir();

    DWORD exceptionCode = pExceptionInfo
        ? pExceptionInfo->ExceptionRecord->ExceptionCode : 0;
    void* exceptionAddress = pExceptionInfo
        ? pExceptionInfo->ExceptionRecord->ExceptionAddress : NULL;

    // Faulting module + module-relative offset: the offset is the RVA the
    // server-side symbolication and crash signature key on.
    char  szCrashModule[MAX_PATH] = "???";
    DWORD dwOffset = (DWORD)(DWORD_PTR)exceptionAddress;
    if (exceptionAddress != NULL)
    {
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)exceptionAddress, &hModule))
        {
            if (!GetModuleFileNameA(hModule, szCrashModule, MAX_PATH))
                _snprintf_s(szCrashModule, sizeof(szCrashModule), _TRUNCATE, "???");
            dwOffset = (DWORD)((DWORD_PTR)exceptionAddress - (DWORD_PTR)hModule);
        }
    }

    static KekCrashContext s_ctx;      // statics: keep the crash-path stack tiny
    CaptureGameContext(&s_ctx);

    static char s_szOs[256];
    GetOsDescription(s_szOs, sizeof(s_szOs));

    static KekMemStats s_mem;
    CollectMemStats(&s_mem);

    static char s_szDumpPath[MAX_PATH];
    bool bDumpOk = WriteMiniDumpFile(pExceptionInfo, "crash", &s_ctx, s_szOs,
                                     s_szDumpPath, sizeof(s_szDumpPath));

    WriteSidecarJson(s_szDumpPath, "crash", exceptionCode, szCrashModule,
                     dwOffset, 0, &s_ctx, s_szOs, &s_mem);

    // Dialog. Two flavors like CP: our DLL (actionable bug report) vs
    // elsewhere in the process (often 32-bit address exhaustion).
    bool bFromDLL =
        _stricmp(GetOnlyFilename(szCrashModule), "CvGameCore_Expansion2.dll") == 0;

    static char s_szMessage[2048];
    _snprintf_s(s_szMessage, sizeof(s_szMessage), _TRUNCATE,
        "%s"
        "\n"
        "--Crash details--\n"
        "kek-mod version: %s\n"
        "Exception: 0x%08X (%s) in %s+0x%08X\n"
        "Turn: %d   MP: %d   Humans: %d\n"
        "OS: %s\n"
        "Memory (sub-2GB): %u MB committed, largest free block %u MB\n"
        "\n"
        "%s\n"
        "\n"
        "Please post BOTH files (.dmp and .json) in the kek Discord along "
        "with what was happening in game.",
        bFromDLL
            ? "The game crashed due to an error in the kek-mod DLL. A crash "
              "report was saved -- posting it lets us fix this for everyone.\n"
            : "The game crashed outside the kek-mod DLL. A crash report was "
              "saved anyway -- it may still identify the cause.\n"
              "\n"
              "Civ 5 is a 32-bit program and commonly crashes when it runs "
              "out of address space. If this happens often: disable yield "
              "icons, set Leader Screen Quality to minimum, avoid zooming "
              "far out, and use Strategic View in the late game.\n",
        CvHttp_GetModVersion(),
        (unsigned)exceptionCode, GetExceptionDescription(exceptionCode),
        GetOnlyFilename(szCrashModule), (unsigned)dwOffset,
        s_ctx.iTurn, s_ctx.iNetworkMP, s_ctx.iNumHumans,
        s_szOs,
        (unsigned)(s_mem.committedLowKB >> 10),
        (unsigned)(s_mem.largestFreeLowKB >> 10),
        bDumpOk ? s_szDumpPath : "(minidump creation FAILED -- report the details above as a screenshot)");

    ShowCrashDialog(s_szMessage);

    return EXCEPTION_EXECUTE_HANDLER;
}

// ---------------------------------------------------------------------------
// Hang watchdog (Phase 2 of plan/CRASH_REPORTER_PLAN.md)
//
// Deadlocks raise no exception, so KekCrashFilter above never sees them --
// this is what would have caught the MP turn-rollover freeze that motivated
// the whole plan. A low-priority background thread polls the age of a
// heartbeat the game thread writes every CvGame::update() tick; if it goes
// stale past the threshold, dump the live process (all thread stacks, same
// writer as the crash path) and disarm for the rest of the session. Never
// kills the process -- it may still recover.
// ---------------------------------------------------------------------------

static const DWORD KEK_HANG_THRESHOLD_MS = 90000;

static volatile DWORD g_dwLastHeartbeatTick = 0;   // written by the game thread
static volatile LONG  g_lArmed              = 0;   // set on the first heartbeat
static volatile LONG  g_lHangDumped         = 0;   // one hang dump per session
static HANDLE         g_hWatchdogShutdownEvent = NULL;
static HANDLE         g_hWatchdogThread         = NULL;

typedef HWND  (WINAPI* PFN_GetForegroundWindow)(void);
typedef DWORD (WINAPI* PFN_GetWindowThreadProcessId)(HWND, LPDWORD);
typedef BOOL  (WINAPI* PFN_IsIconic)(HWND);

static PFN_GetForegroundWindow      g_pfnGetForegroundWindow      = NULL;
static PFN_GetWindowThreadProcessId g_pfnGetWindowThreadProcessId = NULL;
static PFN_IsIconic                 g_pfnIsIconic                 = NULL;

// user32 is not in this project's link deps (see ShowCrashDialog above);
// resolved once here and cached, since the watchdog polls this every 5s.
static bool LoadUser32ForegroundFns()
{
    if (g_pfnGetForegroundWindow && g_pfnGetWindowThreadProcessId && g_pfnIsIconic)
        return true;
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (!hUser32)
        return false;
    g_pfnGetForegroundWindow =
        (PFN_GetForegroundWindow)GetProcAddress(hUser32, "GetForegroundWindow");
    g_pfnGetWindowThreadProcessId =
        (PFN_GetWindowThreadProcessId)GetProcAddress(hUser32, "GetWindowThreadProcessId");
    g_pfnIsIconic = (PFN_IsIconic)GetProcAddress(hUser32, "IsIconic");
    return g_pfnGetForegroundWindow && g_pfnGetWindowThreadProcessId && g_pfnIsIconic;
}

// True if THIS process' window is the active, non-minimized foreground
// window -- i.e. the player is actually looking at the game, not alt-tabbed
// away. Compares the foreground window's owning PID instead of hunting for
// "the" game HWND (which this DLL has no handle to). Fails open (true) if
// user32 can't be resolved -- missing a real hang report is worse than one
// extra dump from an undetectable edge case.
static bool IsOwnProcessForegroundAndVisible()
{
    if (!LoadUser32ForegroundFns())
        return true;
    HWND hFg = g_pfnGetForegroundWindow();
    if (!hFg)
        return false;
    DWORD dwPid = 0;
    g_pfnGetWindowThreadProcessId(hFg, &dwPid);
    if (dwPid != GetCurrentProcessId())
        return false;
    if (g_pfnIsIconic(hFg))
        return false;
    return true;
}

// Same dump writer as the crash path, pep == NULL (captures all thread
// stacks of the live process -- exactly what diagnosed the rollover
// deadlock by hand).
static void WriteHangDump(DWORD dwStallMs)
{
    EnsureCrashlogsDir();

    static KekCrashContext s_ctx;
    CaptureGameContext(&s_ctx);

    static char s_szOs[256];
    GetOsDescription(s_szOs, sizeof(s_szOs));

    static KekMemStats s_mem;
    CollectMemStats(&s_mem);

    static char s_szDumpPath[MAX_PATH];
    WriteMiniDumpFile(NULL, "hang", &s_ctx, s_szOs, s_szDumpPath, sizeof(s_szDumpPath));

    WriteSidecarJson(s_szDumpPath, "hang", 0, "", 0, dwStallMs,
                     &s_ctx, s_szOs, &s_mem);

    char szLog[160];
    _snprintf_s(szLog, sizeof(szLog), _TRUNCATE,
                "kek watchdog: game thread stalled %u ms, dump %s\n",
                (unsigned)dwStallMs,
                s_szDumpPath[0] ? s_szDumpPath : "FAILED");
    OutputDebugString(szLog);
}

static DWORD WINAPI KekWatchdogThreadProc(LPVOID)
{
    bool  bHaveSeenHeartbeat        = false;
    bool  bForegroundSeenDuringStall = false;
    DWORD dwLastSeenHeartbeat        = 0;

    for (;;)
    {
        // 5 s poll; also the shutdown wait, so DLL_PROCESS_DETACH never
        // blocks more than one tick on this thread.
        if (WaitForSingleObject(g_hWatchdogShutdownEvent, 5000) == WAIT_OBJECT_0)
            return 0;

        if (g_lHangDumped)
            continue;                  // already reported once this session
        if (!g_lArmed)
            continue;                  // no game yet (menus/loading)
        if (IsDebuggerPresent())
            continue;                  // paused under a debugger looks like a hang

        DWORD dwHeartbeat = g_dwLastHeartbeatTick;
        if (!bHaveSeenHeartbeat || dwHeartbeat != dwLastSeenHeartbeat)
        {
            // Heartbeat moved (or this is the first check) -- any prior
            // stall just ended; start tracking foreground state fresh.
            dwLastSeenHeartbeat        = dwHeartbeat;
            bHaveSeenHeartbeat         = true;
            bForegroundSeenDuringStall = false;
        }

        if (IsOwnProcessForegroundAndVisible())
            bForegroundSeenDuringStall = true;

        DWORD dwAge = GetTickCount() - dwHeartbeat;   // unsigned: wrap-safe
        if (dwAge <= KEK_HANG_THRESHOLD_MS)
            continue;

        // Alt-tabbed/minimized clients legitimately stop pumping; only
        // treat it as a hang if the player was actually looking at the
        // game at some point during the stall.
        if (!bForegroundSeenDuringStall)
            continue;

        InterlockedExchange(&g_lHangDumped, 1);
        WriteHangDump(dwAge);
    }
}

void KekCrashReporter_Heartbeat()
{
    g_dwLastHeartbeatTick = GetTickCount();
    if (!g_lArmed)
        InterlockedExchange(&g_lArmed, 1);
}

void KekCrashReporter_Shutdown()
{
    if (g_hWatchdogShutdownEvent)
        SetEvent(g_hWatchdogShutdownEvent);
    if (g_hWatchdogThread)
    {
        WaitForSingleObject(g_hWatchdogThread, 6000);
        CloseHandle(g_hWatchdogThread);
        g_hWatchdogThread = NULL;
    }
    if (g_hWatchdogShutdownEvent)
    {
        CloseHandle(g_hWatchdogShutdownEvent);
        g_hWatchdogShutdownEvent = NULL;
    }
}

// ---------------------------------------------------------------------------
// Dev-only crash test trigger (see CvCrashReporter.h)
// ---------------------------------------------------------------------------

#ifdef KEKMOD_BUILD_DEV

static DWORD WINAPI CrashTestThreadProc(LPVOID)
{
    volatile int* p = NULL;
    *p = 42;    // deliberate AV off the game thread
    return 0;
}

// Blocks the calling (game) thread well past KEK_HANG_THRESHOLD_MS without
// touching the heartbeat, so the watchdog trips exactly like a real deadlock
// would. Sleep, not a busy loop or lock -- simplest thing that stops the
// heartbeat from advancing.
static void RunHangTest()
{
    Sleep(KEK_HANG_THRESHOLD_MS + 30000);
}

void KekCrashReporter_CheckTestTrigger()
{
    // Poll at most every 5 seconds; the trigger file check is one
    // CreateFileA on a path that almost never exists.
    static DWORD s_dwLastCheck = 0;
    DWORD dwNow = GetTickCount();
    if (s_dwLastCheck != 0 && dwNow - s_dwLastCheck < 5000)
        return;
    s_dwLastCheck = dwNow;

    if (g_szCrashlogsDir[0] == '\0')
        return;

    char szTriggerPath[MAX_PATH];
    _snprintf_s(szTriggerPath, sizeof(szTriggerPath), _TRUNCATE,
                "%s\\crashtest.txt", g_szCrashlogsDir);

    HANDLE hFile = CreateFileA(szTriggerPath, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    char szKind[16] = {0};
    DWORD dwRead = 0;
    ReadFile(hFile, szKind, sizeof(szKind) - 1, &dwRead, NULL);
    CloseHandle(hFile);

    // Consume the trigger BEFORE crashing/hanging so the next launch is clean.
    DeleteFileA(szTriggerPath);

    if (strncmp(szKind, "thread", 6) == 0)
    {
        OutputDebugString("kek crash test: triggering deliberate crash (spawned thread)\n");
        CreateThread(NULL, 0, CrashTestThreadProc, NULL, 0, NULL);
        return;
    }
    if (strncmp(szKind, "hang", 4) == 0)
    {
        OutputDebugString("kek crash test: triggering deliberate hang (game thread)\n");
        RunHangTest();
        return;
    }
    OutputDebugString("kek crash test: triggering deliberate crash (game thread)\n");
    volatile int* p = NULL;
    *p = 42;    // deliberate AV on the game thread
}

#else // !KEKMOD_BUILD_DEV

void KekCrashReporter_CheckTestTrigger() {}

#endif

// ---------------------------------------------------------------------------
// Pending report scan + submit/decline (Phase 3 of plan/CRASH_REPORTER_PLAN.md)
//
// Scanned once at boot from KekCrashReporter_Install(), which then shows a
// native Yes/No dialog right there and acts on the answer immediately --
// NOT a themed in-game popup. That was the original design, but Game (kek's
// CvGame Lua binding) turned out to not be Lua-bound at all at the bare
// FrontEnd screen (confirmed via Lua.log: "attempt to index global 'Game'
// (a nil value)"), and RegisterScriptLibraries appears to never run for that
// screen's Lua thread -- an engine boot-order thing we can't fix without
// engine source. A native dialog sidesteps the whole Lua/UI layer, fires
// before any menu exists (Install() runs at/near process boot), and reuses
// the same MessageBoxA-via-runtime-resolution pattern KekCrashFilter's
// ShowCrashDialog already uses successfully.
//
// Submit uploads each pending pair on a background thread reusing
// CvHttpUtils' WinHTTP machinery. Declining moves the pair into
// crashlogs\declined\ -- kept on disk (still hand-reportable) but invisible
// to this scan, so a declined report never asks again.
// ---------------------------------------------------------------------------

struct KekPendingReport
{
    char szDumpPath[MAX_PATH];
    char szJsonPath[MAX_PATH];
    char szKind[16];       // "crash" | "hang" | "unknown"
    char szMetaJson[600];  // sidecar contents, trimmed -- the upload's X-Crash-Meta
    DWORD dwDumpSizeBytes;
    DWORD dwJsonSizeBytes;
};

static const int KEK_MAX_PENDING_REPORTS = 10;   // matches the local-retention cap
static KekPendingReport g_pendingReports[KEK_MAX_PENDING_REPORTS];
static int              g_nPendingReports = 0;

// Reads a whole file into pszOut (NUL-terminated), trimming trailing CR/LF --
// the sidecar is written with a trailing "\n" (WriteSidecarJson) that must
// not leak into an HTTP header value.
static void ReadFileTrimmed(const char* pszPath, char* pszOut, size_t nOut)
{
    pszOut[0] = '\0';
    HANDLE hFile = CreateFileA(pszPath, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    DWORD dwRead = 0;
    ReadFile(hFile, pszOut, (DWORD)(nOut - 1), &dwRead, NULL);
    CloseHandle(hFile);
    pszOut[dwRead] = '\0';
    while (dwRead > 0 && (pszOut[dwRead - 1] == '\n' || pszOut[dwRead - 1] == '\r'))
        pszOut[--dwRead] = '\0';
}

// Pulls "kind":"crash"/"hang" out of the sidecar without a JSON library --
// the field is always written as one of these two literals (WriteSidecarJson
// above), so a substring search is enough.
static void ExtractKind(const char* pszJson, char* pszKindOut, size_t nOut)
{
    _snprintf_s(pszKindOut, nOut, _TRUNCATE, "unknown");
    if (strstr(pszJson, "\"kind\":\"hang\""))
        _snprintf_s(pszKindOut, nOut, _TRUNCATE, "hang");
    else if (strstr(pszJson, "\"kind\":\"crash\""))
        _snprintf_s(pszKindOut, nOut, _TRUNCATE, "crash");
}

// Called once from KekCrashReporter_Install(). Lists crashlogs\*.json,
// pairs each with its same-basename .dmp, keeps up to
// KEK_MAX_PENDING_REPORTS in memory.
static void ScanPendingReports()
{
    g_nPendingReports = 0;
    if (g_szCrashlogsDir[0] == '\0')
        return;

    char szPattern[MAX_PATH];
    _snprintf_s(szPattern, sizeof(szPattern), _TRUNCATE, "%s\\*.json", g_szCrashlogsDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(szPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (g_nPendingReports >= KEK_MAX_PENDING_REPORTS)
            break;

        KekPendingReport& r = g_pendingReports[g_nPendingReports];
        _snprintf_s(r.szJsonPath, sizeof(r.szJsonPath), _TRUNCATE,
                    "%s\\%s", g_szCrashlogsDir, fd.cFileName);

        _snprintf_s(r.szDumpPath, sizeof(r.szDumpPath), _TRUNCATE, "%s", r.szJsonPath);
        size_t nLen = strlen(r.szDumpPath);
        strcpy_s(r.szDumpPath + nLen - 5, sizeof(r.szDumpPath) - (nLen - 5), ".dmp");

        WIN32_FILE_ATTRIBUTE_DATA dumpAttr;
        if (!GetFileAttributesExA(r.szDumpPath, GetFileExInfoStandard, &dumpAttr))
            continue;   // orphaned sidecar (dump missing) -- skip
        r.dwDumpSizeBytes = dumpAttr.nFileSizeLow;
        r.dwJsonSizeBytes = fd.nFileSizeLow;

        ReadFileTrimmed(r.szJsonPath, r.szMetaJson, sizeof(r.szMetaJson));
        ExtractKind(r.szMetaJson, r.szKind, sizeof(r.szKind));
        ++g_nPendingReports;
    }
    while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

static void FormatByteSize(DWORD dwBytes, char* pszOut, size_t nOut)
{
    if (dwBytes >= 1024 * 1024)
        _snprintf_s(pszOut, nOut, _TRUNCATE, "%.1f MB", dwBytes / (1024.0 * 1024.0));
    else
        _snprintf_s(pszOut, nOut, _TRUNCATE, "%.0f KB", dwBytes / 1024.0);
}

// Builds the full dialog body: a punchy headline count + total size, so the
// player can make an informed choice without needing to already trust us,
// without getting into the .dmp/.json file-level detail. Crash and hang
// reports aren't broken out here -- "crash report" is used as the general
// term for the feature (see the plan doc); the kind still goes out in the
// upload headers regardless.
static void BuildPendingReportMessage(char* pszOut, size_t nOut)
{
    DWORD dwTotalBytes = 0;
    for (int i = 0; i < g_nPendingReports; ++i)
        dwTotalBytes += g_pendingReports[i].dwDumpSizeBytes + g_pendingReports[i].dwJsonSizeBytes;

    char szSize[32];
    FormatByteSize(dwTotalBytes, szSize, sizeof(szSize));

    _snprintf_s(pszOut, nOut, _TRUNCATE,
        "KekMod found %d crash report%s! (%s)\n"
        "\n"
        "Send them to help fix bugs?",
        g_nPendingReports, g_nPendingReports == 1 ? "" : "s", szSize);
}

// Uploads every pending report in order; stops at the first failure so the
// remainder (plus the failed one) stays queued for the next launch's prompt.
// BACKGROUND THREAD -- never touches game state.
static DWORD WINAPI SubmitPendingReportsThreadProc(LPVOID)
{
    int i = 0;
    for (; i < g_nPendingReports; ++i)
    {
        KekPendingReport& r = g_pendingReports[i];
        DWORD dwStatus = 0;
        if (!CvHttp_PostCrashDump(r.szDumpPath, r.szKind, r.szMetaJson, &dwStatus))
        {
            OutputDebugString("kek crash report: upload failed, remaining reports stay queued\n");
            break;
        }
        DeleteFileA(r.szDumpPath);
        DeleteFileA(r.szJsonPath);
    }
    if (i == g_nPendingReports)
        g_nPendingReports = 0;   // all sent -- nothing left pending this session
    return 0;
}

static void SubmitPendingReports()
{
    HANDLE hThread = CreateThread(NULL, 0, SubmitPendingReportsThreadProc, NULL, 0, NULL);
    if (hThread)
        CloseHandle(hThread);
}

// "Not Now": move every pending pair into crashlogs\declined\ so it stays on
// disk (still hand-reportable) but ScanPendingReports never sees it again.
static void DeclinePendingReports()
{
    char szDeclinedDir[MAX_PATH];
    _snprintf_s(szDeclinedDir, sizeof(szDeclinedDir), _TRUNCATE,
                "%s\\declined", g_szCrashlogsDir);
    CreateDirectoryA(szDeclinedDir, NULL);

    for (int i = 0; i < g_nPendingReports; ++i)
    {
        KekPendingReport& r = g_pendingReports[i];
        char szDest[MAX_PATH];

        _snprintf_s(szDest, sizeof(szDest), _TRUNCATE, "%s\\%s",
                    szDeclinedDir, GetOnlyFilename(r.szDumpPath));
        MoveFileExA(r.szDumpPath, szDest, MOVEFILE_REPLACE_EXISTING);

        _snprintf_s(szDest, sizeof(szDest), _TRUNCATE, "%s\\%s",
                    szDeclinedDir, GetOnlyFilename(r.szJsonPath));
        MoveFileExA(r.szJsonPath, szDest, MOVEFILE_REPLACE_EXISTING);
    }
    g_nPendingReports = 0;
}

#ifndef MB_YESNO
#define MB_YESNO 0x00000004L
#endif
#ifndef MB_ICONQUESTION
#define MB_ICONQUESTION 0x00000020L
#endif
#ifndef IDYES
#define IDYES 6
#endif

// Same runtime-resolution pattern as ShowCrashDialog above (user32 isn't in
// this project's link deps). Blocking/modal -- Install() runs at/near
// process boot, well before any menu exists, so this is the one place we
// can prompt before the player has done anything at all. MB_TOPMOST/
// MB_SETFOREGROUND (defined near ShowCrashDialog above) keep it from getting
// buried behind Civ V's fullscreen surface.
static bool ShowYesNoDialog(const char* pszText, const char* pszTitle)
{
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (!hUser32)
        return false;   // fail closed -- no dialog means no consent
    PFN_MessageBoxA pfnMessageBoxA =
        (PFN_MessageBoxA)GetProcAddress(hUser32, "MessageBoxA");
    if (!pfnMessageBoxA)
        return false;
    SetSystemCursorVisible(hUser32, TRUE);
    int iResult = pfnMessageBoxA(NULL, pszText, pszTitle,
                                 MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL |
                                 MB_TOPMOST | MB_SETFOREGROUND);
    SetSystemCursorVisible(hUser32, FALSE);
    return iResult == IDYES;
}

// Called once from KekCrashReporter_Install() after ScanPendingReports().
static void ShowPendingReportPrompt()
{
    if (g_nPendingReports == 0)
        return;

    char szMessage[1024];
    BuildPendingReportMessage(szMessage, sizeof(szMessage));

    if (ShowYesNoDialog(szMessage, "kek-mod: pending crash reports"))
        SubmitPendingReports();
    else
        DeclinePendingReports();
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

void KekCrashReporter_Install()
{
    static bool s_bInstalled = false;
    if (s_bInstalled)
        return;
    s_bInstalled = true;

    EnsureCrashlogsDir();
    ScanPendingReports();
    ShowPendingReportPrompt();
    SetUnhandledExceptionFilter(KekCrashFilter);

    // Manual-reset: SetEvent from Shutdown() latches it so the thread's next
    // 5s wait wakes immediately even if Shutdown() races ahead of the wait.
    g_hWatchdogShutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_hWatchdogShutdownEvent)
    {
        g_hWatchdogThread = CreateThread(NULL, 0, KekWatchdogThreadProc, NULL,
                                         CREATE_SUSPENDED, NULL);
        if (g_hWatchdogThread)
        {
            SetThreadPriority(g_hWatchdogThread, THREAD_PRIORITY_LOWEST);
            ResumeThread(g_hWatchdogThread);
        }
    }

    OutputDebugString("kek-mod crash reporter + hang watchdog installed\n");
}

#else // !(_WIN32 && NQM_MINIDUMPS)

void KekCrashReporter_Install() {}
void KekCrashReporter_Shutdown() {}
void KekCrashReporter_Heartbeat() {}
void KekCrashReporter_CheckTestTrigger() {}

#endif
