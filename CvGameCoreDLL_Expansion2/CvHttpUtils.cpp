//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvHttpUtils.cpp
//! \brief   Outbound HTTP for the GDR bot: save uploads, crash reports and
//!          desync reports.
//!
//! On each end-of-turn auto-save (hook in CvGame.cpp), at game end and after
//! a passed IRR, EVERY client uploads its own copy of the fresh .Civ5Save
//! -> KEKMOD_SAVE_PATH (octet-stream), via a spool folder and its own queue
//! (see "Save upload (every client)" below). GDR reads the turn's state from
//! the save; there is no separate turn JSON any more.
//!
//! Game state is only read on the GAME thread; the network I/O runs on a
//! background thread via CreateThread so the game loop is never blocked.
//!
//! Results are written to kekmod_http.log in the standard Civ V Logs folder.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include "CvGameCoreDLLPCH.h"   // Must be first (precompiled header)
#include "CvHttpUtils.h"
#include "CvGame.h"
#include "CvPreGame.h"

#ifdef _WIN32

// WinHTTP is a Windows-inbox library -- no extra install needed.
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")   // RegOpenKeyExA/RegQueryValueExA (GetDocumentsPath)

#include <algorithm>
#include <deque>
#include <string>

// ---------------------------------------------------------------------------
// Configuration (hardcoded by decision 2026-07-08; re-ship DLL to change).
//
// The DEV/PROD environment is picked at COMPILE TIME (decision 2026-07-13):
// package.sh builds this file twice, once plain and once with
// KEKMOD_BUILD_DEV defined (via the KekModExtraDefines MSBuild property --
// see CvGameCoreDLL_Expansion2.vs2013.vcxproj), producing two distinct DLLs
// for the dev and prod zips. There is deliberately no runtime toggle (no
// marker file, no env var, no registry key): anything a player can flip on
// their own disk is something a player can flip to silently stop their
// client reporting turns at all, with no error surfaced to anyone. Baking
// the host into the binary means doing that requires patching the DLL
// itself, not touching a text file.
// ---------------------------------------------------------------------------
#define KEKMOD_HTTP_HOST_PROD   L"saves.ww3.cx"                // Cloudflare edge
#define KEKMOD_HTTP_PORT_PROD   443
// Dev box moved 2026-08-14: CT 113 @ .61 (Debian 11) -> CT 114 @ .129 (Ubuntu 24.04).
// Because the host is baked in, DEV DLLs built before that date still point at .61
// and must be rebuilt to talk to the new box.
#define KEKMOD_HTTP_HOST_DEV    L"192.168.2.129"               // GDR dev box, LAN
#define KEKMOD_HTTP_PORT_DEV    8080

// The VS2008-era SDK's winhttp.h predates these; values match the Win8+ SDK.
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1  0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2  0x00000800
#endif
#define KEKMOD_SAVE_PATH        L"/api/saves"
#define KEKMOD_CRASHES_PATH     L"/api/crashes"
#define KEKMOD_DESYNCS_PATH     L"/api/desyncs"
// Shared secret checked by GDR's KekApiKeyFilter (kek.api-key in its
// application.yml -- keep the two in sync). It ships inside a public DLL,
// so it's a spam/scanner barrier, not real secrecy -- kept out of source
// control anyway to stay off GitHub's diff/secret scanners. "" = header
// omitted. See KekSecrets.h.example for setup.
#include "KekSecrets.h"
#define KEKMOD_MOD_VERSION      "2.1"
#define KEKMOD_MOD_VERSION_W    L"2.1"
#define KEKMOD_HTTP_LOG         "kekmod_http.log"

// Civ V save directory relative to %USERPROFILE%\Documents
#define KEKMOD_SAVES_SUBPATH    "\\My Games\\Sid Meier's Civilization 5\\Saves\\multi\\auto"


// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void WriteLog(const char* pszFmt, ...)
{
    FILogFileMgr* pMgr = FILogFileMgr::PeekInstance();
    if (!pMgr)
        return;

    FILogFile* pLog = pMgr->GetLog(KEKMOD_HTTP_LOG, FILogFile::kDontTimeStamp);
    if (!pLog)
        return;

    char szBuf[2048];
    va_list args;
    va_start(args, pszFmt);
    vsnprintf_s(szBuf, sizeof(szBuf), _TRUNCATE, pszFmt, args);
    va_end(args);

    pLog->Msg("%s", szBuf);
}


// The version DEFINE must stay in this file -- package.sh/stage.sh/publish.sh
// sed it out of CvHttpUtils.cpp by name. Other code (CvCrashReporter.cpp)
// consumes it through this accessor.
const char* CvHttp_GetModVersion()
{
    return KEKMOD_MOD_VERSION;
}

// Compile-time only -- see the config block above. No file/registry check,
// so there is nothing on disk for a player to tamper with.
static bool IsDevEnvironment()
{
    static volatile LONG s_lLogged = 0;
    if (InterlockedCompareExchange(&s_lLogged, 1, 0) == 0)
    {
#ifdef KEKMOD_BUILD_DEV
        WriteLog("[kekmod_http] environment: DEV (compiled in) -- LAN dev box, plain HTTP");
#else
        WriteLog("[kekmod_http] environment: PROD (compiled in) -- saves.ww3.cx via Cloudflare, HTTPS");
#endif
    }
#ifdef KEKMOD_BUILD_DEV
    return true;
#else
    return false;
#endif
}

// Preprocessor selection, not a runtime ternary: a ternary keeps BOTH string
// literals in the compiled binary, so the prod DLL would carry the dev LAN
// host in its string table. This way each flavor contains only its own host.
#ifdef KEKMOD_BUILD_DEV
static const wchar_t* HttpHost()  { return KEKMOD_HTTP_HOST_DEV; }
static INTERNET_PORT  HttpPort()  { return KEKMOD_HTTP_PORT_DEV; }
static DWORD          HttpFlags() { return 0; }
#else
static const wchar_t* HttpHost()  { return KEKMOD_HTTP_HOST_PROD; }
static INTERNET_PORT  HttpPort()  { return KEKMOD_HTTP_PORT_PROD; }
static DWORD          HttpFlags() { return WINHTTP_FLAG_SECURE; }
#endif


// Resolves the user's real "My Documents" folder via the same registry value
// Explorer itself keeps current (Shell Folders\Personal) -- unlike
// %USERPROFILE%\Documents, this follows OneDrive Known Folder Move or a
// manual Documents relocation. Falls back to the %USERPROFILE%\Documents
// guess only if the registry read fails outright (e.g. locked-down HKCU).
// CvGame::CopyModDataToMPMP (Community Patch heritage) resolves the MODS
// folder the same way -- see plan notes for why this is the trusted method.
static bool GetDocumentsPath(char* pszOut, size_t nLen)
{
    HKEY hKey = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                       "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
                       0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char szValue[MAX_PATH] = {0};
        DWORD dwType = REG_SZ;
        DWORD dwSize = sizeof(szValue);
        LONG lResult = RegQueryValueExA(hKey, "Personal", NULL, &dwType,
                                        (LPBYTE)szValue, &dwSize);
        RegCloseKey(hKey);
        if (lResult == ERROR_SUCCESS && szValue[0] && dwType == REG_EXPAND_SZ)
        {
            char szExpanded[MAX_PATH] = {0};
            if (ExpandEnvironmentStringsA(szValue, szExpanded, sizeof(szExpanded)))
                strncpy_s(szValue, sizeof(szValue), szExpanded, _TRUNCATE);
        }
        if (lResult == ERROR_SUCCESS && szValue[0] &&
            (dwType == REG_SZ || dwType == REG_EXPAND_SZ))
        {
            _snprintf_s(pszOut, nLen, _TRUNCATE, "%s", szValue);
            return true;
        }
    }

    char szProfile[MAX_PATH] = {0};
    if (!GetEnvironmentVariableA("USERPROFILE", szProfile, sizeof(szProfile)))
        return false;
    _snprintf_s(pszOut, nLen, _TRUNCATE, "%s\\Documents", szProfile);
    return true;
}

// Returns true and fills pszPathOut + pWriteTimeOut for the most recently
// written .Civ5Save in the configured saves directory.
static bool FindMostRecentSave(char* pszPathOut, size_t nPathLen,
                               FILETIME* pWriteTimeOut)
{
    char szDocuments[MAX_PATH] = {0};
    if (!GetDocumentsPath(szDocuments, sizeof(szDocuments)))
        return false;

    char szDir[MAX_PATH]     = {0};
    char szPattern[MAX_PATH] = {0};
    _snprintf_s(szDir,     sizeof(szDir),     _TRUNCATE, "%s%s", szDocuments, KEKMOD_SAVES_SUBPATH);
    _snprintf_s(szPattern, sizeof(szPattern), _TRUNCATE, "%s\\*.Civ5Save",  szDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(szPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    WIN32_FIND_DATAA best = fd;
    while (FindNextFileA(hFind, &fd))
    {
        if (CompareFileTime(&fd.ftLastWriteTime, &best.ftLastWriteTime) > 0)
            best = fd;
    }
    FindClose(hFind);

    _snprintf_s(pszPathOut, nPathLen, _TRUNCATE, "%s\\%s", szDir, best.cFileName);
    *pWriteTimeOut = best.ftLastWriteTime;
    return true;
}


// Stable per-game id: the map GUID as 32 lowercase hex chars.
static void FormatMapGuid(char* pszOut, size_t nLen)
{
    GUID guid = GC.getMap().GetGUID();
    _snprintf_s(pszOut, nLen, _TRUNCATE,
        "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        (unsigned)guid.Data1, (unsigned)guid.Data2, (unsigned)guid.Data3,
        (unsigned)guid.Data4[0], (unsigned)guid.Data4[1], (unsigned)guid.Data4[2],
        (unsigned)guid.Data4[3], (unsigned)guid.Data4[4], (unsigned)guid.Data4[5],
        (unsigned)guid.Data4[6], (unsigned)guid.Data4[7]);
}

// Shared per-game id, 16 lowercase hex chars: the map random seed, then a hash
// of both random seeds, the map size and the map script. The host picks the
// seeds and every client gets them (they drive the synced RNGs), so every
// client of a game computes the same key. The map GUID above is NOT shared:
// each client's engine makes its own, so two clients of one game upload under
// different X-Game-Id values. The hash half keeps the key from repeating the
// seed when the host gave both seeds the same value.
//
// The save holds the map seed but not the sync seed, and a game re-hosted from
// a save gets a new sync seed, so the save can't rebuild the key on its own:
// CvGame::recordKekReplayData logs it as REPLAYEVENT_KEK_GameKey.
unsigned CvHttp_GameKeyHash()
{
    const unsigned uMapSeed  = (unsigned)CvPreGame::mapRandomSeed();
    const unsigned uSyncSeed = (unsigned)CvPreGame::syncRandomSeed();

    unsigned uHash = 2166136261u;   // FNV-1a
    const unsigned auInts[4] = { uMapSeed, uSyncSeed,
                                 (unsigned)GC.getMap().getGridWidth(),
                                 (unsigned)GC.getMap().getGridHeight() };
    for (int i = 0; i < 4; i++)
        for (int iByte = 0; iByte < 4; iByte++)
            uHash = (uHash ^ ((auInts[i] >> (8 * iByte)) & 0xFF)) * 16777619u;
    const std::string strScript = CvPreGame::mapScriptName();
    for (size_t i = 0; i < strScript.size(); i++)
        uHash = (uHash ^ (unsigned char)strScript[i]) * 16777619u;
    return uHash;
}

static void FormatGameKey(char* pszOut, size_t nLen)
{
    _snprintf_s(pszOut, nLen, _TRUNCATE, "%08x%08x",
                (unsigned)CvPreGame::mapRandomSeed(), CvHttp_GameKeyHash());
}


// ---------------------------------------------------------------------------
// Payload helpers (GAME THREAD ONLY)
// ---------------------------------------------------------------------------

// Minimal JSON string escaping: quote, backslash, control chars.
// Non-ASCII (UTF-8) bytes pass through untouched -- valid JSON.
static void JsonEscape(std::string& out, const char* psz)
{
    for (const char* p = psz; p && *p; ++p)
    {
        unsigned char c = (unsigned char)*p;
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20)
        {
            char buf[8];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "\\u%04x", (int)c);
            out += buf;
        }
        else out += (char)c;
    }
}

// "persona@76561198000000000" -> steam id part, or "" if not that format.
static std::string SteamIdFromNickname(const CvString& strNick)
{
    const char* psz = strNick.c_str();
    const char* pAt = strrchr(psz, '@');
    if (!pAt)
        return std::string();
    const char* pId = pAt + 1;
    size_t nLen = strlen(pId);
    if (nLen != 17 || strncmp(pId, "7656119", 7) != 0)
        return std::string();
    for (const char* p = pId; *p; ++p)
        if (*p < '0' || *p > '9')
            return std::string();
    return std::string(pId);
}


// ---------------------------------------------------------------------------
// WinHTTP plumbing (BACKGROUND THREADS)
// ---------------------------------------------------------------------------

// Backoff schedule for a save drain whose front entry is a game-end or
// proposal save: no later turn's save heals those.
static const DWORD s_adwGameEndBackoffMs[] = { 10000, 30000, 60000 };

// One POST over a shared session/connection pair.  pwszHeaders may be NULL.
static bool HttpPost(HINTERNET hConnect, const wchar_t* pwszPath,
                     const wchar_t* pwszHeaders,
                     const void* pBody, DWORD dwLen, DWORD* pdwStatusOut)
{
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", pwszPath, NULL,
        WINHTTP_NO_REFERER, NULL, HttpFlags());
    if (!hRequest)
        return false;

    bool bOk = false;
    if (WinHttpSendRequest(hRequest,
                           pwszHeaders ? pwszHeaders : WINHTTP_NO_ADDITIONAL_HEADERS,
                           pwszHeaders ? (DWORD)-1 : 0,
                           (LPVOID)pBody, dwLen, dwLen, 0))
    {
        if (WinHttpReceiveResponse(hRequest, NULL))
        {
            DWORD dwSize = sizeof(DWORD);
            WinHttpQueryHeaders(hRequest,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                pdwStatusOut, &dwSize, WINHTTP_NO_HEADER_INDEX);
            bOk = true;
        }
    }
    WinHttpCloseHandle(hRequest);
    return bOk;
}

// Opens the WinHTTP session + connection pair; logs on connect failure.
static void OpenConnection(HINTERNET* phSession, HINTERNET* phConnect)
{
    *phConnect = NULL;
    *phSession = WinHttpOpen(
        L"CivV-KekMod/" KEKMOD_MOD_VERSION_W,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!*phSession)
        return;

    DWORD dwTimeout = 10000;
    WinHttpSetOption(*phSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
    dwTimeout = 30000;   // generous for a multi-MB upload
    WinHttpSetOption(*phSession, WINHTTP_OPTION_SEND_TIMEOUT,    &dwTimeout, sizeof(dwTimeout));
    WinHttpSetOption(*phSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

    if (HttpFlags() & WINHTTP_FLAG_SECURE)
    {
        // Old WinHTTP defaults to TLS 1.0, which Cloudflare refuses; pin to
        // TLS 1.1/1.2 (best this API can ask for pre-Win10-1903 -- the OS
        // negotiates 1.2 with Cloudflare in practice).
        DWORD dwProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
                          | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (!WinHttpSetOption(*phSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                              &dwProtocols, sizeof(dwProtocols)))
            WriteLog("[kekmod_http] could not pin TLS 1.1/1.2 (error=%u);"
                     " continuing with OS defaults", GetLastError());
    }

    *phConnect = WinHttpConnect(*phSession, HttpHost(), HttpPort(), 0);
    if (!*phConnect)
        WriteLog("[kekmod_http] connect to %S:%d failed (error=%u)",
                 HttpHost(), (int)HttpPort(), GetLastError());
}

enum SendResult
{
    SEND_OK,      // delivered (2xx) -- pop the entry
    SEND_RETRY,   // network failure / 5xx -- keep queued, stop the drain
    SEND_DROP     // permanently rejected (4xx) -- pop and move on
};

// Crash report upload (Phase 3, plan/CRASH_REPORTER_PLAN.md). Unlike the
// turn-upload path above, this is one user-triggered action per report, not
// a persistent per-turn queue -- no PendingUpload entry, no backoff/retry
// loop; a failure is just reported back so the caller can leave the files
// queued for the next launch's popup. BACKGROUND THREAD ONLY.
bool CvHttp_PostCrashDump(const char* pszDumpPath, const char* pszKind,
                          const char* pszMetaJson, DWORD* pdwStatusOut)
{
    *pdwStatusOut = 0;

    char* pFileBuf = NULL;
    DWORD dwRead   = 0;
    HANDLE hFile = CreateFileA(
        pszDumpPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        WriteLog("[kekmod_http] crash dump '%s' not readable (error=%u)",
                 pszDumpPath, GetLastError());
        return false;
    }

    LARGE_INTEGER liSize;
    ZeroMemory(&liSize, sizeof(liSize));
    GetFileSizeEx(hFile, &liSize);
    DWORD dwFileSize = (DWORD)liSize.QuadPart;
    pFileBuf = new char[dwFileSize];
    if (!ReadFile(hFile, pFileBuf, dwFileSize, &dwRead, NULL))
        dwRead = 0;
    CloseHandle(hFile);

    if (dwRead == 0)
    {
        delete[] pFileBuf;
        WriteLog("[kekmod_http] crash dump '%s' read 0 bytes", pszDumpPath);
        return false;
    }

    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    OpenConnection(&hSession, &hConnect);
    if (!hConnect)
    {
        delete[] pFileBuf;
        return false;
    }

    wchar_t wszHeaders[2048];
    swprintf_s(wszHeaders, _countof(wszHeaders),
               L"Content-Type: application/octet-stream\r\n"
               L"X-Mod-Version: %hs\r\n"
               L"X-Report-Kind: %hs\r\n"
               L"X-Crash-Meta: %hs\r\n"
               L"%hs%hs%hs",
               KEKMOD_MOD_VERSION, pszKind, pszMetaJson,
               KEKMOD_API_KEY[0] ? "X-Api-Key: " : "",
               KEKMOD_API_KEY[0] ? KEKMOD_API_KEY : "",
               KEKMOD_API_KEY[0] ? "\r\n" : "");

    bool bSent = HttpPost(hConnect, KEKMOD_CRASHES_PATH, wszHeaders,
                          pFileBuf, dwRead, pdwStatusOut);
    delete[] pFileBuf;
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!bSent)
    {
        WriteLog("[kekmod_http] crash dump POST failed (WinHTTP error=%u)", GetLastError());
        return false;
    }
    if (*pdwStatusOut < 200 || *pdwStatusOut >= 300)
    {
        WriteLog("[kekmod_http] crash dump POST '%s' -> status=%u (not accepted)",
                 pszDumpPath, *pdwStatusOut);
        return false;
    }
    WriteLog("[kekmod_http] crash dump POST '%s' (%u bytes) -> status=%u",
             pszDumpPath, dwRead, *pdwStatusOut);
    return true;
}

// ---------------------------------------------------------------------------
// Save upload (every client)
//
// There is no single-uploader rule: EVERY client
// uploads its own copy of each turn's autosave. GDR keeps every copy (one
// per player per turn), so a turn is only lost if every client fails, and a
// desync can be looked at from each player's side.
//
// Capture happens on the game thread right after the end-of-turn AutoSave:
// the newest autosave is COPIED into this client's spool folder
// (KEKMOD_SPOOL_SUBPATH\<map guid>\<turn>_<name>), which Civ never rotates,
// so a stalled queue still has its saves when it drains. A spool file is
// deleted once GDR has it. On a game change the other games' spool folders
// are deleted; spool files of the CURRENT game left over from an earlier
// session (crash, quit with a backlog) are re-queued, and GDR drops any it
// already has as byte-identical duplicates.
//
// When a client has no save for a turn, it says so: an empty-body POST with
// X-Save-Status (no_file / unchanged / stale / wrong_turn / copy_failed /
// spool_missing)
// and X-Save-Detail, which GDR records. Before giving up on a turn whose
// autosave wasn't on disk yet at hook time, the worker polls briefly for it
// ("late capture"), in case the engine wrote it after the hook ran.
//
// One background worker per client drains the save queue FIFO. 2xx pops the
// entry; a permanent rejection (4xx other than 401/403 auth and 429 rate
// limiting) drops it; a network failure, 5xx, 401, 403 or 429 stops the drain
// with the rest still queued, and the next capture re-kicks the worker. A
// game-end or proposal save at the front retries with backoff first.
// ---------------------------------------------------------------------------

// Spool folder, relative to the Documents folder.
#define KEKMOD_SPOOL_SUBPATH          "\\My Games\\Sid Meier's Civilization 5\\kekmod\\save_spool"
// Spooled turns kept per game when the queue can't drain; oldest go first.
#define KEKMOD_SPOOL_MAX_TURNS        60
// An autosave older than this at hook time is not this turn's save.
#define KEKMOD_SAVE_MAX_AGE_SECONDS   600
// Late capture: only for entries this recent, polling for this long.
#define KEKMOD_LATE_CAPTURE_WINDOW_MS 60000
#define KEKMOD_LATE_CAPTURE_POLL_MS   15000

struct PendingSave
{
    unsigned int uSeq;         // unique per entry (pop guard)
    int          iTurn;
    int          iSlot;        // local player's slot
    bool         bGameEnd;     // final save -> backoff retries
    bool         bProposal;    // save after a resolved proposal -> backoff retries
    int          iHost;        // 1 = this client hosts, 0 = it doesn't, -1 = unknown
                               //   (re-queued from an earlier session's spool)
    std::string  strGameId;    // map GUID hex
    std::string  strGameKey;   // shared by every client of the game
    std::string  strSteamId;   // local player
    std::string  strSpoolPath; // "" = no save (see strStatus)
    std::string  strSaveName;  // original autosave name, for X-Save-Filename
    std::string  strStatus;    // "" = save captured; else why there's none
    std::string  strDetail;
    FILETIME     hookTime;     // when the hook ran (UTC)
    std::string  strSeenName;  // newest autosave at hook time (late capture
    FILETIME     seenWriteTime;//   must find a different, newer one)

    PendingSave() : uSeq(0), iTurn(0), iSlot(-1), bGameEnd(false), bProposal(false), iHost(-1)
    {
        hookTime.dwLowDateTime = hookTime.dwHighDateTime = 0;
        seenWriteTime.dwLowDateTime = seenWriteTime.dwHighDateTime = 0;
    }
};

static std::deque<PendingSave> s_pendingSaves;          // guarded by s_saveQueueLock
static std::string             s_pendingSavesGameId;    // guarded by s_saveQueueLock
static CRITICAL_SECTION        s_saveQueueLock;
static bool                    s_bSaveQueueLockInit = false;  // init on game thread
static volatile LONG           s_lSaveWorkerBusy    = 0;
static unsigned int            s_uNextSaveSeq       = 1;      // game thread only
// The last autosave captured, by the hook or by a late capture: the next
// turn must find a different file. Guarded by s_saveQueueLock.
static std::string             s_strLastCapturedName;
static FILETIME                s_lastCapturedWriteTime = {0, 0};

static unsigned __int64 FileTimeToU64(const FILETIME& ft)
{
    ULARGE_INTEGER li;
    li.LowPart  = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

// Seconds from ftThen to now (UTC); negative if ftThen is in the future.
static __int64 SecondsSince(const FILETIME& ftThen)
{
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    return ((__int64)FileTimeToU64(ftNow) - (__int64)FileTimeToU64(ftThen)) / 10000000;
}

static const char* BaseName(const char* pszPath)
{
    const char* pszBase = strrchr(pszPath, '\\');
    return pszBase ? pszBase + 1 : pszPath;
}

// <Documents>\...\kekmod\save_spool, created if missing. Empty on failure.
static std::string GetSpoolRoot()
{
    char szDocuments[MAX_PATH] = {0};
    if (!GetDocumentsPath(szDocuments, sizeof(szDocuments)))
        return std::string();

    // Create each level; the Civ folder itself always exists by now.
    std::string strKekmod = std::string(szDocuments) +
        "\\My Games\\Sid Meier's Civilization 5\\kekmod";
    std::string strRoot = std::string(szDocuments) + KEKMOD_SPOOL_SUBPATH;
    CreateDirectoryA(strKekmod.c_str(), NULL);
    CreateDirectoryA(strRoot.c_str(), NULL);
    DWORD dwAttr = GetFileAttributesA(strRoot.c_str());
    if (dwAttr == INVALID_FILE_ATTRIBUTES || !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
        return std::string();
    return strRoot;
}

static void DeleteFolderFiles(const std::string& strDir)
{
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((strDir + "\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            DeleteFileA((strDir + "\\" + fd.cFileName).c_str());
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

// Spool file name for a turn: "0124_AutoSave_Post_0124 AD-1280.Civ5Save",
// "0124_end_..." for the game-end save, or "0124_vote_..." for the save after
// a resolved proposal (the engine gives it the same name as the turn's own).
static std::string SpoolFileName(const PendingSave& entry, const char* pszSaveName)
{
    char szPrefix[32];
    _snprintf_s(szPrefix, sizeof(szPrefix), _TRUNCATE, "%04d_%s",
                entry.iTurn, entry.bGameEnd ? "end_" : entry.bProposal ? "vote_" : "");
    return std::string(szPrefix) + pszSaveName;
}

// Upload order of spooled saves: by turn, then proposal, turn, game end.
static int SpoolOrder(const PendingSave& entry)
{
    return entry.iTurn * 3 + (entry.bProposal ? 0 : entry.bGameEnd ? 2 : 1);
}

// Copies pszSavePath into the spool as this turn's save. Fills the entry's
// spool path and save name on success; status/detail on failure.
static bool SpoolSave(PendingSave& entry, const char* pszSavePath)
{
    std::string strRoot = GetSpoolRoot();
    if (strRoot.empty())
    {
        entry.strStatus = "copy_failed";
        entry.strDetail = "spool folder unavailable";
        return false;
    }
    std::string strDir = strRoot + "\\" + entry.strGameId;
    CreateDirectoryA(strDir.c_str(), NULL);

    const char* pszName = BaseName(pszSavePath);
    std::string strTarget = strDir + "\\" + SpoolFileName(entry, pszName);
    if (!CopyFileA(pszSavePath, strTarget.c_str(), FALSE))
    {
        char szDetail[64];
        _snprintf_s(szDetail, sizeof(szDetail), _TRUNCATE, "CopyFile error=%u", GetLastError());
        entry.strStatus = "copy_failed";
        entry.strDetail = szDetail;
        return false;
    }
    entry.strSpoolPath = strTarget;
    entry.strSaveName  = pszName;
    entry.strStatus.clear();
    entry.strDetail.clear();
    return true;
}

// The engine names its post-turn autosaves "AutoSave_Post_<turn>...". A save
// whose name carries a different turn is some other turn's file.
static bool SaveNameContradictsTurn(const char* pszName, int iTurn)
{
    const char* pszPost = strstr(pszName, "_Post_");
    if (!pszPost)
        return false;
    pszPost += 6;
    if (pszPost[0] < '0' || pszPost[0] > '9')
        return false;
    return atoi(pszPost) != iTurn;
}

static bool IsLastCaptured(const char* pszName, const FILETIME& writeTime)
{
    EnterCriticalSection(&s_saveQueueLock);
    bool bSame = s_strLastCapturedName == pszName &&
                 CompareFileTime(&writeTime, &s_lastCapturedWriteTime) == 0;
    LeaveCriticalSection(&s_saveQueueLock);
    return bSame;
}

static void SetLastCaptured(const char* pszName, const FILETIME& writeTime)
{
    EnterCriticalSection(&s_saveQueueLock);
    s_strLastCapturedName   = pszName;
    s_lastCapturedWriteTime = writeTime;
    LeaveCriticalSection(&s_saveQueueLock);
}

static void EnsureSaveQueueLock()
{
    if (!s_bSaveQueueLockInit)
    {
        InitializeCriticalSection(&s_saveQueueLock);
        s_bSaveQueueLockInit = true;
    }
}

// On a game change: drop the old game's queue, delete other games' spool
// folders, and re-queue this game's spool files left from an earlier
// session. GAME THREAD ONLY.
static void SwitchSaveQueueToGame(const std::string& strGameId, const std::string& strSteamId,
                                  int iSlot)
{
    EnsureSaveQueueLock();

    char szGameKey[24];
    FormatGameKey(szGameKey, sizeof(szGameKey));

    size_t nStale = 0;
    EnterCriticalSection(&s_saveQueueLock);
    bool bChanged = (s_pendingSavesGameId != strGameId);
    if (bChanged)
    {
        nStale = s_pendingSaves.size();
        s_pendingSaves.clear();
        s_pendingSavesGameId = strGameId;
    }
    LeaveCriticalSection(&s_saveQueueLock);
    if (!bChanged)
        return;
    if (nStale > 0)
        WriteLog("[kekmod_http] game changed -- discarded %u queued save(s) from the"
                 " previous game", (unsigned)nStale);

    std::string strRoot = GetSpoolRoot();
    if (strRoot.empty())
        return;

    std::deque<PendingSave> leftovers;
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((strRoot + "\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            std::string strDir = strRoot + "\\" + fd.cFileName;
            if (strGameId != fd.cFileName)
            {
                DeleteFolderFiles(strDir);
                RemoveDirectoryA(strDir.c_str());
                continue;
            }

            // This game's spool from an earlier session: queue it again.
            WIN32_FIND_DATAA fs;
            HANDLE hSaves = FindFirstFileA((strDir + "\\*.Civ5Save").c_str(), &fs);
            if (hSaves == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                // "<turn>_[end_|vote_]<name>"
                const char* pszUnderscore = strchr(fs.cFileName, '_');
                if (!pszUnderscore || pszUnderscore == fs.cFileName)
                    continue;
                PendingSave entry;
                entry.iTurn        = atoi(fs.cFileName);
                entry.bGameEnd     = strncmp(pszUnderscore + 1, "end_", 4) == 0;
                entry.bProposal    = strncmp(pszUnderscore + 1, "vote_", 5) == 0;
                entry.iSlot        = iSlot;
                entry.strGameId    = strGameId;
                entry.strGameKey   = szGameKey;
                entry.strSteamId   = strSteamId;
                entry.strSpoolPath = strDir + "\\" + fs.cFileName;
                entry.strSaveName  = pszUnderscore + 1 + (entry.bGameEnd ? 4 : entry.bProposal ? 5 : 0);
                GetSystemTimeAsFileTime(&entry.hookTime);
                leftovers.push_back(entry);
            } while (FindNextFileA(hSaves, &fs));
            FindClose(hSaves);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    if (leftovers.empty())
        return;
    // FindFirstFile order is not guaranteed: oldest turn first, and within a
    // turn in the order they were taken (proposal, turn, game end). GDR takes
    // a later copy of a turn that differs as the newer state.
    for (size_t i = 1; i < leftovers.size(); ++i)
        for (size_t j = i; j > 0 && SpoolOrder(leftovers[j - 1]) > SpoolOrder(leftovers[j]); --j)
            std::swap(leftovers[j - 1], leftovers[j]);
    EnterCriticalSection(&s_saveQueueLock);
    for (size_t i = 0; i < leftovers.size(); ++i)
    {
        leftovers[i].uSeq = s_uNextSaveSeq++;
        s_pendingSaves.push_back(leftovers[i]);
    }
    LeaveCriticalSection(&s_saveQueueLock);
    WriteLog("[kekmod_http] re-queued %u spooled save(s) of this game from an earlier"
             " session", (unsigned)leftovers.size());
}

// The engine may finish writing the autosave after the hook ran: poll for a
// save written since the hook that isn't the one seen then. BACKGROUND
// THREAD ONLY.
static bool TryLateCapture(PendingSave& entry)
{
    if (SecondsSince(entry.hookTime) * 1000 > KEKMOD_LATE_CAPTURE_WINDOW_MS)
        return false;

    // Allow a little clock slack between the hook time and the file time.
    unsigned __int64 uNotBefore = FileTimeToU64(entry.hookTime) - 2ULL * 10000000ULL;
    for (DWORD dwWaited = 0; dwWaited <= KEKMOD_LATE_CAPTURE_POLL_MS; dwWaited += 500)
    {
        char     szPath[MAX_PATH] = {0};
        FILETIME writeTime        = {0, 0};
        if (FindMostRecentSave(szPath, sizeof(szPath), &writeTime) &&
            FileTimeToU64(writeTime) >= uNotBefore &&
            !SaveNameContradictsTurn(BaseName(szPath), entry.iTurn) &&
            !IsLastCaptured(BaseName(szPath), writeTime) &&
            (entry.strSeenName != BaseName(szPath) ||
             CompareFileTime(&writeTime, &entry.seenWriteTime) != 0))
        {
            std::string strOldStatus = entry.strStatus;
            if (SpoolSave(entry, szPath))
            {
                SetLastCaptured(BaseName(szPath), writeTime);
                WriteLog("[kekmod_http] turn %d save captured late (%s at hook time,"
                         " found after %ums): '%s'", entry.iTurn, strOldStatus.c_str(),
                         (unsigned)dwWaited, entry.strSaveName.c_str());
                return true;
            }
            return false;   // copy failed: status/detail say why
        }
        Sleep(500);
    }
    entry.strDetail += " (none after late wait)";
    return false;
}

// Header values must be single-line printable ASCII.
static std::string HeaderSafe(const std::string& str, size_t nMax)
{
    std::string out;
    for (size_t i = 0; i < str.size() && out.size() < nMax; ++i)
    {
        unsigned char c = (unsigned char)str[i];
        out += (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    return out;
}

// POSTs one save, or the no-save report. BACKGROUND THREAD ONLY.
static SendResult SendPendingSave(HINTERNET hConnect, PendingSave& entry)
{
    char* pFileBuf = NULL;
    DWORD dwRead   = 0;
    if (entry.strStatus.empty())
    {
        HANDLE hFile = CreateFileA(entry.strSpoolPath.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER liSize;
            ZeroMemory(&liSize, sizeof(liSize));
            GetFileSizeEx(hFile, &liSize);
            DWORD dwFileSize = (DWORD)liSize.QuadPart;
            pFileBuf = new char[dwFileSize > 0 ? dwFileSize : 1];
            if (!ReadFile(hFile, pFileBuf, dwFileSize, &dwRead, NULL) || dwRead == 0)
                dwRead = 0;
            CloseHandle(hFile);
        }
        if (dwRead == 0)
        {
            char szDetail[64];
            _snprintf_s(szDetail, sizeof(szDetail), _TRUNCATE, "error=%u", GetLastError());
            entry.strStatus = "spool_missing";
            entry.strDetail = szDetail;
            delete[] pFileBuf;
            pFileBuf = NULL;
        }
    }

    std::string strHeaders;
    char szCommon[1024];
    _snprintf_s(szCommon, sizeof(szCommon), _TRUNCATE,
                "Content-Type: application/octet-stream\r\n"
                "X-Mod-Version: %s\r\n"
                "X-Turn-Number: %d\r\n"
                "X-Game-Id: %s\r\n"
                "X-Game-Key: %s\r\n"
                "X-Uploader-Steam-Id: %s\r\n"
                "X-Player-Slot: %d\r\n"
                "%s%s%s",
                KEKMOD_MOD_VERSION, entry.iTurn,
                entry.strGameId.c_str(), entry.strGameKey.c_str(), entry.strSteamId.c_str(), entry.iSlot,
                KEKMOD_API_KEY[0] ? "X-Api-Key: " : "",
                KEKMOD_API_KEY[0] ? KEKMOD_API_KEY : "",
                KEKMOD_API_KEY[0] ? "\r\n" : "");
    strHeaders = szCommon;
    strHeaders += entry.bGameEnd ? "X-Save-Kind: game_end\r\n"
                : entry.bProposal ? "X-Save-Kind: proposal\r\n"
                : "X-Save-Kind: turn\r\n";
    // When copies of a turn disagree (a desync), GDR keeps the host's: a resync
    // gives every client the host's game. A save left from an earlier session
    // doesn't know, and says nothing.
    if (entry.iHost >= 0)
        strHeaders += entry.iHost ? "X-Is-Host: 1\r\n" : "X-Is-Host: 0\r\n";
    if (pFileBuf)
        strHeaders += "X-Save-Filename: " + HeaderSafe(entry.strSaveName, 400) + "\r\n";
    else
    {
        strHeaders += "X-Save-Status: " + HeaderSafe(entry.strStatus, 32) + "\r\n";
        if (!entry.strDetail.empty())
            strHeaders += "X-Save-Detail: " + HeaderSafe(entry.strDetail, 400) + "\r\n";
    }

    std::wstring wstrHeaders(strHeaders.begin(), strHeaders.end());   // ASCII only
    DWORD dwStatus = 0;
    bool bSent = HttpPost(hConnect, KEKMOD_SAVE_PATH, wstrHeaders.c_str(),
                          pFileBuf, dwRead, &dwStatus);
    delete[] pFileBuf;

    const char* pszWhat = dwRead ? "save" : "no-save report";
    if (!bSent || dwStatus >= 500)
    {
        WriteLog("[kekmod_http] %s POST turn %d failed (%s=%u); kept queued", pszWhat,
                 entry.iTurn, bSent ? "status" : "WinHTTP error",
                 bSent ? dwStatus : GetLastError());
        return SEND_RETRY;
    }
    if (dwStatus == 401 || dwStatus == 403 || dwStatus == 429)
    {
        // Auth (config) or rate limiting: the payload is fine, keep it.
        WriteLog("[kekmod_http] %s POST turn %d refused (status=%u); kept queued",
                 pszWhat, entry.iTurn, dwStatus);
        return SEND_RETRY;
    }
    if (dwStatus >= 400)
    {
        WriteLog("[kekmod_http] %s POST turn %d rejected (status=%u); dropped",
                 pszWhat, entry.iTurn, dwStatus);
        return SEND_DROP;
    }
    if (dwRead)
        WriteLog("[kekmod_http] save POST '%s' turn %d (%u bytes, game key %s) -> status=%u",
                 entry.strSaveName.c_str(), entry.iTurn, dwRead, entry.strGameKey.c_str(), dwStatus);
    else
        WriteLog("[kekmod_http] no-save report turn %d (%s: %s) -> status=%u",
                 entry.iTurn, entry.strStatus.c_str(), entry.strDetail.c_str(), dwStatus);
    return SEND_OK;
}

static DWORD WINAPI SaveQueueWorkerProc(LPVOID)
{
    for (;;)
    {
        HINTERNET hSession = NULL;
        HINTERNET hConnect = NULL;
        OpenConnection(&hSession, &hConnect);

        int  iBackoffStep  = 0;
        bool bDrainStopped = false;
        for (;;)
        {
            PendingSave entry;
            bool bHaveEntry = false;
            EnterCriticalSection(&s_saveQueueLock);
            if (!s_pendingSaves.empty())
            {
                entry      = s_pendingSaves.front();
                bHaveEntry = true;
            }
            LeaveCriticalSection(&s_saveQueueLock);
            if (!bHaveEntry)
                break;

            if (!entry.strStatus.empty() && entry.strSpoolPath.empty() &&
                entry.strStatus != "copy_failed")
            {
                TryLateCapture(entry);
                // Keep the outcome if this entry has to wait for a retry.
                EnterCriticalSection(&s_saveQueueLock);
                if (!s_pendingSaves.empty() && s_pendingSaves.front().uSeq == entry.uSeq)
                    s_pendingSaves.front() = entry;
                LeaveCriticalSection(&s_saveQueueLock);
            }

            SendResult eResult = hConnect ? SendPendingSave(hConnect, entry) : SEND_RETRY;

            if (eResult == SEND_RETRY)
            {
                if ((entry.bGameEnd || entry.bProposal) && iBackoffStep < (int)_countof(s_adwGameEndBackoffMs))
                {
                    Sleep(s_adwGameEndBackoffMs[iBackoffStep]);
                    iBackoffStep++;
                    if (hConnect) { WinHttpCloseHandle(hConnect); hConnect = NULL; }
                    if (hSession) { WinHttpCloseHandle(hSession); hSession = NULL; }
                    OpenConnection(&hSession, &hConnect);
                    continue;
                }
                bDrainStopped = true;
                break;
            }

            // Delivered or dropped: either way the spool copy is done with.
            if (!entry.strSpoolPath.empty())
                DeleteFileA(entry.strSpoolPath.c_str());
            EnterCriticalSection(&s_saveQueueLock);
            if (!s_pendingSaves.empty() && s_pendingSaves.front().uSeq == entry.uSeq)
                s_pendingSaves.pop_front();
            LeaveCriticalSection(&s_saveQueueLock);
            iBackoffStep = 0;
        }

        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);

        InterlockedExchange(&s_lSaveWorkerBusy, 0);
        if (bDrainStopped)
            return 0;   // the next turn's capture re-kicks the worker

        bool bMore = false;
        EnterCriticalSection(&s_saveQueueLock);
        bMore = !s_pendingSaves.empty();
        LeaveCriticalSection(&s_saveQueueLock);
        if (!bMore || InterlockedCompareExchange(&s_lSaveWorkerBusy, 1, 0) != 0)
            return 0;
    }
}

static void KickSaveWorker()
{
    if (InterlockedCompareExchange(&s_lSaveWorkerBusy, 1, 0) == 0)
    {
        HANDLE hThread = CreateThread(NULL, 0, SaveQueueWorkerProc, NULL, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
        else
        {
            InterlockedExchange(&s_lSaveWorkerBusy, 0);
            WriteLog("[kekmod_http] CreateThread (saves) failed: error=%u -- saves stay"
                     " queued for the next turn", GetLastError());
        }
    }
}

// Captures this turn's autosave into the spool (or records why there is
// none) and queues it for upload. Runs on EVERY client, right after the
// AutoSave call. GAME THREAD ONLY. A proposal save that isn't there is
// dropped rather than reported: the turn's own save still comes.
static void CaptureTurnSave(int iTurn, const char* pszGameId, PlayerTypes eActive,
                            bool bGameEnd, bool bProposal = false)
{
    PendingSave entry;
    entry.iTurn      = iTurn;
    entry.iSlot      = (int)eActive;
    entry.bGameEnd   = bGameEnd;
    entry.bProposal  = bProposal;
    entry.iHost      = gDLL->IsHost() ? 1 : 0;
    entry.strGameId  = pszGameId;
    {
        char szGameKey[24];
        FormatGameKey(szGameKey, sizeof(szGameKey));
        entry.strGameKey = szGameKey;
    }
    entry.strSteamId = SteamIdFromNickname(CvPreGame::nickname(eActive));
    GetSystemTimeAsFileTime(&entry.hookTime);

    SwitchSaveQueueToGame(entry.strGameId, entry.strSteamId, entry.iSlot);

    // The newest autosave is this turn's only if it's new since the last
    // capture, was written just now and isn't named for another turn;
    // anything else is reported instead.
    char     szPath[MAX_PATH] = {0};
    FILETIME writeTime        = {0, 0};
    if (!FindMostRecentSave(szPath, sizeof(szPath), &writeTime))
    {
        entry.strStatus = "no_file";
        entry.strDetail = std::string("no .Civ5Save in <Documents>") + KEKMOD_SAVES_SUBPATH;
    }
    else
    {
        entry.strSeenName   = BaseName(szPath);
        entry.seenWriteTime = writeTime;
        __int64 iAge = SecondsSince(writeTime);
        char szDetail[MAX_PATH + 64];
        _snprintf_s(szDetail, sizeof(szDetail), _TRUNCATE, "newest='%s' age=%ds",
                    entry.strSeenName.c_str(), (int)iAge);
        if (IsLastCaptured(entry.strSeenName.c_str(), writeTime))
        {
            entry.strStatus = "unchanged";
            entry.strDetail = szDetail;
        }
        else if (iAge > KEKMOD_SAVE_MAX_AGE_SECONDS)
        {
            entry.strStatus = "stale";
            entry.strDetail = szDetail;
        }
        else if (SaveNameContradictsTurn(entry.strSeenName.c_str(), iTurn))
        {
            entry.strStatus = "wrong_turn";
            entry.strDetail = szDetail;
        }
        else if (SpoolSave(entry, szPath))
        {
            SetLastCaptured(entry.strSeenName.c_str(), writeTime);
        }
    }
    if (!entry.strStatus.empty() && bProposal)
    {
        WriteLog("[kekmod_http] turn %d: no proposal save to upload (%s: %s)",
                 iTurn, entry.strStatus.c_str(), entry.strDetail.c_str());
        return;
    }
    if (!entry.strStatus.empty())
        WriteLog("[kekmod_http] turn %d: no fresh save to upload yet (%s: %s)",
                 iTurn, entry.strStatus.c_str(), entry.strDetail.c_str());

    size_t nDropped = 0;
    EnterCriticalSection(&s_saveQueueLock);
    entry.uSeq = s_uNextSaveSeq++;
    s_pendingSaves.push_back(entry);
    // A queue that can't drain keeps only the newest turns. Never drop the
    // front: the worker may be sending it right now.
    while (s_pendingSaves.size() > KEKMOD_SPOOL_MAX_TURNS + 1)
    {
        if (!s_pendingSaves[1].strSpoolPath.empty())
            DeleteFileA(s_pendingSaves[1].strSpoolPath.c_str());
        s_pendingSaves.erase(s_pendingSaves.begin() + 1);
        nDropped++;
    }
    LeaveCriticalSection(&s_saveQueueLock);
    if (nDropped > 0)
        WriteLog("[kekmod_http] save queue full -- dropped %u oldest spooled save(s)",
                 (unsigned)nDropped);

    KickSaveWorker();
}


// ---------------------------------------------------------------------------
// Public API -- called from the CvGame.cpp end-of-turn auto-save hook
// (GAME THREAD).
// ---------------------------------------------------------------------------

static int CountAliveHumanPlayers()
{
    int iCount = 0;
    for (int iJ = 0; iJ < MAX_PLAYERS; iJ++)
    {
        CvPlayer& kItPlayer = GET_PLAYER((PlayerTypes)iJ);
        if (kItPlayer.isAlive() && kItPlayer.isHuman())
            iCount++;
    }
    return iCount;
}

// Telemetry is for real networked multiplayer sessions only. Bail out for
// single player / hotseat / PBEM entirely -- isGameMultiPlayer() is true
// for hotseat and PBEM too, so isNetworkMultiPlayer() is the one that
// actually excludes them -- and for a networked MP game that has
// degenerated to one remaining human (everyone else quit to AI); both are
// solo sessions and neither should phone home.
static bool ShouldSendTelemetry()
{
    if (!GC.getGame().isNetworkMultiPlayer())
        return false;
    return CountAliveHumanPlayers() > 1;
}

// Map GUID of the last game this session sent a regular turn for.
static std::string s_strLiveTelemetryGameId;

// A game's ending (victory, resolved vote) often comes just after the table
// drops to one human: the losers quit to AI before the win lands, or the
// winning capture kills the last other human. The session was still a real
// multiplayer game, so its final save goes out as long as one of its turns
// did -- otherwise GDR never gets the final save, which holds the winner.
static bool ShouldSendFinalTelemetry()
{
    if (ShouldSendTelemetry())
        return true;
    if (!GC.getGame().isNetworkMultiPlayer() || s_strLiveTelemetryGameId.empty())
        return false;
    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));
    return s_strLiveTelemetryGameId == szGuid;
}

void CvHttp_OnTurnAutoSave()
{
    if (!ShouldSendTelemetry())
        return;

    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;

    // Dedup: the hook can fire more than once per auto-save; capture exactly
    // one save per (game, turn).
    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));
    s_strLiveTelemetryGameId = szGuid;
    int iTurn = GC.getGame().getGameTurn();
    static std::string s_strLastGameId;
    static int         s_iLastTurn = -1;
    if (s_iLastTurn == iTurn && s_strLastGameId == szGuid)
        return;
    s_strLastGameId = szGuid;
    s_iLastTurn     = iTurn;

    // Every client uploads its own copy of this turn's autosave.
    CaptureTurnSave(iTurn, szGuid, eActive, false);
}

// Set when a proposal resolves; CvHttp_OnUpdate takes the save.
static bool s_bProposalSavePending = false;

void CvHttp_OnGameEnd()
{
    s_bProposalSavePending = false;   // the game-end save below holds the result
    if (!ShouldSendFinalTelemetry())
        return;

    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;

    // setWinner runs on every client. No end-of-turn autosave follows a game
    // end: save the final state now and upload it from every client, like any
    // other turn (with backoff -- no later turn heals a game-end save).
    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));
#ifdef KEK_SAVE_DATA
    GC.getGame().recordKekReplayData();
#endif
    gDLL->AutoSave(false, true);
    CaptureTurnSave(GC.getGame().getGameTurn(), szGuid, eActive, true);
}

void CvHttp_OnProposalResolved(bool bSave)
{
    // A passed IRR may be the last thing that happens in a session that never
    // reaches another end-of-turn autosave, so it gets a save of its own. It
    // waits for the next update: this runs before the proposal is marked
    // complete, and may run inside turn processing.
    if (bSave && ShouldSendFinalTelemetry())
        s_bProposalSavePending = true;
}

void CvHttp_OnUpdate()
{
    if (!s_bProposalSavePending)
        return;
    s_bProposalSavePending = false;
    if (!ShouldSendFinalTelemetry())
        return;
    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;

    // Every client saves and uploads, like the turn's own autosave. It is
    // filed under the current turn; that turn's end-of-turn save follows.
    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));
#ifdef KEK_SAVE_DATA
    GC.getGame().recordKekReplayData();
#endif
    gDLL->AutoSave(false, true);
    CaptureTurnSave(GC.getGame().getGameTurn(), szGuid, eActive, false, true);
}

// ---------------------------------------------------------------------------
// Desync reports (KEK_SYNC_FINGERPRINT, see CvSyncFingerprint.cpp)
//
// Like the save upload, there is no single-uploader rule: the point is to get
// BOTH sides' snapshot of the same slice, so every client that detects a
// mismatch posts its own report. One report per out-of-sync episode per
// remote player, fire-and-forget with a short retry; GDR pairs the reports
// by gameId + slice.
// ---------------------------------------------------------------------------

struct PendingDesyncReport
{
    int         iTurn;
    std::string strJson;
    std::string strGameId;
    std::string strSteamId;
};

static const DWORD s_adwDesyncRetryMs[] = { 5000, 15000, 45000 };

static DWORD WINAPI DesyncReportWorkerProc(LPVOID pParam)
{
    PendingDesyncReport* pReport = (PendingDesyncReport*)pParam;

    char szHeaders[1024];
    _snprintf_s(szHeaders, sizeof(szHeaders), _TRUNCATE,
                "Content-Type: application/json\r\n"
                "X-Mod-Version: %s\r\n"
                "X-Turn-Number: %d\r\n"
                "X-Game-Id: %s\r\n"
                "X-Uploader-Steam-Id: %s\r\n"
                "%s%s%s",
                KEKMOD_MOD_VERSION, pReport->iTurn,
                pReport->strGameId.c_str(), pReport->strSteamId.c_str(),
                KEKMOD_API_KEY[0] ? "X-Api-Key: " : "",
                KEKMOD_API_KEY[0] ? KEKMOD_API_KEY : "",
                KEKMOD_API_KEY[0] ? "\r\n" : "");
    wchar_t wszHeaders[1024];
    swprintf_s(wszHeaders, _countof(wszHeaders), L"%hs", szHeaders);

    for (int iAttempt = 0; ; iAttempt++)
    {
        HINTERNET hSession = NULL;
        HINTERNET hConnect = NULL;
        OpenConnection(&hSession, &hConnect);
        DWORD dwStatus = 0;
        bool bSent = hConnect && HttpPost(hConnect, KEKMOD_DESYNCS_PATH, wszHeaders,
                                          pReport->strJson.c_str(),
                                          (DWORD)pReport->strJson.size(), &dwStatus);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);

        if (bSent && dwStatus >= 200 && dwStatus < 300)
        {
            WriteLog("[kekmod_http] desync report turn %d (%u bytes) -> status=%u",
                     pReport->iTurn, (unsigned)pReport->strJson.size(), dwStatus);
            break;
        }
        // A 4xx other than auth / rate limiting will not get better on retry.
        bool bPermanent = bSent && dwStatus >= 400 && dwStatus < 500 &&
                          dwStatus != 401 && dwStatus != 403 && dwStatus != 429;
        if (bPermanent || iAttempt >= (int)_countof(s_adwDesyncRetryMs))
        {
            WriteLog("[kekmod_http] desync report turn %d not delivered (%s=%u); giving up",
                     pReport->iTurn, bSent ? "status" : "WinHTTP error",
                     bSent ? dwStatus : GetLastError());
            break;
        }
        Sleep(s_adwDesyncRetryMs[iAttempt]);
    }

    delete pReport;
    return 0;
}

void CvHttp_PostDesyncReport(int iTurn, int iSlice, PlayerTypes eLocal, PlayerTypes eRemote,
                             const std::string& strReportJson)
{
    if (!ShouldSendTelemetry() || eLocal == NO_PLAYER || strReportJson.size() < 2)
        return;

    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));

    PendingDesyncReport* pReport = new PendingDesyncReport;
    pReport->iTurn      = iTurn;
    pReport->strGameId  = szGuid;
    pReport->strSteamId = SteamIdFromNickname(CvPreGame::nickname(eLocal));

    // Identity fields only this file can produce, spliced in front of the
    // report body ("{...}" from CvSyncFingerprint.cpp).
    std::string& out = pReport->strJson;
    out = "{\"gameId\":\"";
    JsonEscape(out, szGuid);
    out += "\",\"modVersion\":\"" KEKMOD_MOD_VERSION "\",\"reporterSteamId\":\"";
    out += pReport->strSteamId;
    out += "\",\"remoteSteamId\":\"";
    out += SteamIdFromNickname(CvPreGame::nickname(eRemote));
    out += "\",";
    out.append(strReportJson, 1, std::string::npos);

    WriteLog("[kekmod_http] desync with slot %d detected at turn %d slice %d -- sending report",
             (int)eRemote, iTurn, iSlice);

    HANDLE hThread = CreateThread(NULL, 0, DesyncReportWorkerProc, pReport, 0, NULL);
    if (hThread)
        CloseHandle(hThread);
    else
    {
        WriteLog("[kekmod_http] CreateThread for desync report failed: error=%u", GetLastError());
        delete pReport;
    }
}

#else // !_WIN32

// ---------------------------------------------------------------------------
// Non-Windows stub -- WinHTTP is not available; calls silently do nothing.
// ---------------------------------------------------------------------------
void CvHttp_OnTurnAutoSave() {}
void CvHttp_OnGameEnd() {}
void CvHttp_OnProposalResolved(bool) {}
void CvHttp_OnUpdate() {}
const char* CvHttp_GetModVersion() { return ""; }
unsigned CvHttp_GameKeyHash() { return 0; }
void CvHttp_PostDesyncReport(int, int, PlayerTypes, PlayerTypes, const std::string&) {}

#endif // _WIN32
