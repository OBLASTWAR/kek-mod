//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvHttpUtils.cpp
//! \brief   Outbound HTTP for kek-mod 1.5: turn-end save upload + compact
//!          JSON telemetry for the GDR bot.
//!
//! On each end-of-turn auto-save (hook in CvGame.cpp), EVERY client builds
//! and buffers the turn payload, but exactly ONE client -- the one whose
//! local player is the first alive human -- drains the queue and POSTs:
//!   1. the fresh .Civ5Save            -> KEKMOD_SAVE_PATH  (octet-stream)
//!   2. a schema-versioned JSON digest -> KEKMOD_TURNS_PATH (application/json)
//! Standby clients keep the whole game buffered, so whichever client is
//! promoted uploader (e.g. the old uploader quit) flushes any turns the old
//! uploader never delivered; the server upserts by (gameGuid, turnNumber),
//! so the redundant re-sends are no-ops.
//!
//! The JSON is built on the GAME thread (game state must never be touched
//! off-thread); the network I/O runs on a background thread via CreateThread
//! so the game loop is never blocked.  See docs/kek-1.5-upload-plan.md in the
//! GiantDeathRobot repo for the payload contract.
//!
//! Results are written to kekmod_http.log in the standard Civ V Logs folder.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include "CvGameCoreDLLPCH.h"   // Must be first (precompiled header)
#include "CvHttpUtils.h"
#include "CvGame.h"
#include "CvGameCoreUtils.h"    // plotDistance
#include "CvPreGame.h"
#ifdef REPLAY_EVENTS
#include "CvReplayMessage.h"    // CvReplayEvent -- CvGame.h only forward-declares it
#endif

#ifdef _WIN32

// WinHTTP is a Windows-inbox library -- no extra install needed.
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

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
#define KEKMOD_HTTP_HOST_DEV    L"192.168.2.61"                // GDR dev box, LAN
#define KEKMOD_HTTP_PORT_DEV    8080

// The VS2008-era SDK's winhttp.h predates these; values match the Win8+ SDK.
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1  0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2  0x00000800
#endif
#define KEKMOD_SAVE_PATH        L"/api/saves"
#define KEKMOD_TURNS_PATH       L"/api/turns"
// Shared secret checked by GDR's KekApiKeyFilter (kek.api-key in its
// application.yml -- keep the two in sync). It ships inside a public DLL,
// so it's a spam/scanner barrier, not real secrecy -- kept out of source
// control anyway to stay off GitHub's diff/secret scanners. "" = header
// omitted. See KekSecrets.h.example for setup.
#include "KekSecrets.h"
#define KEKMOD_MOD_VERSION      "2.0-beta1"
#define KEKMOD_MOD_VERSION_W    L"2.0-beta1"
#define KEKMOD_JSON_SCHEMA      8
#define KEKMOD_HTTP_LOG         "kekmod_http.log"

// Server-side turn-JSON size cap (GDR application.yml -- keep the two in
// sync). A payload over the cap is rejected 4xx and DROPPED permanently, so
// warn well before: monster late games (units/cities/buildings arrays for
// every player) are the growth vector.
#define KEKMOD_JSON_MAX_BYTES   (1024 * 1024)
#define KEKMOD_JSON_WARN_BYTES  (768 * 1024)

// Store-and-forward: every payload is queued and drained FIFO by a single
// background worker; a failed send leaves the queue intact and the next
// turn's upload retries everything, so an API outage of N turns delivers
// all N payloads (original turn numbers) once the server is back. The
// server upserts by (gameGuid, turnNumber), so re-sends are idempotent.
// The queue is deliberately UNBOUNDED: standby clients retain the whole
// game so a promoted uploader can heal any gap. Payloads are capped at
// KEKMOD_JSON_MAX_BYTES server-side and typically a few KB, so even a
// marathon game buffered whole stays in the tens of MB.

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


// Returns true and fills pszPathOut + pWriteTimeOut for the most recently
// written .Civ5Save in the configured saves directory.
static bool FindMostRecentSave(char* pszPathOut, size_t nPathLen,
                               FILETIME* pWriteTimeOut)
{
    char szProfile[MAX_PATH] = {0};
    if (!GetEnvironmentVariableA("USERPROFILE", szProfile, sizeof(szProfile)))
        return false;

    char szDir[MAX_PATH]     = {0};
    char szPattern[MAX_PATH] = {0};
    _snprintf_s(szDir,     sizeof(szDir),     _TRUNCATE, "%s\\Documents%s", szProfile, KEKMOD_SAVES_SUBPATH);
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


// ---------------------------------------------------------------------------
// Event buffers (GAME THREAD ONLY)
//
// Game code records events (ruins popped, voting-system activity, city
// captures) as they happen; the auto-save hook drains them into the turn
// JSON. Events, not snapshots: under simultaneous turns, state can change
// several times between end-of-turn samples. The buffers are keyed to the
// map GUID so events never leak across games (the statics outlive a game
// when another save is loaded in the same session).
// ---------------------------------------------------------------------------

static std::vector<KekRuinEvent>        s_ruinEvents;
static std::vector<KekVoteEvent>        s_voteEvents;
static std::vector<KekCityCaptureEvent> s_captureEvents;
static std::string                      s_eventBuffersGameId;

#ifdef REPLAY_EVENTS
// How far into CvGame's OWN event list (m_listReplayEvents, via
// getNumReplayEvents()/getReplayEvent()) we've already drained into a turn
// JSON. Unlike the ruins/votes/captures buffers above, this list is not ours
// to clear -- the Replay Viewer needs the full history for the whole game --
// so we only track a read cursor into it, reset to 0 on a new game.
static size_t s_iNextReplayEventIndex = 0;
#endif

// Per-buffer safety caps; real games stay far below these.
#define KEKMOD_MAX_EVENTS_PER_BUFFER 512

// Drop buffered events that belong to a different game than the current one.
static void SyncEventBuffersToCurrentGame()
{
    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));
    if (s_eventBuffersGameId != szGuid)
    {
        s_ruinEvents.clear();
        s_voteEvents.clear();
        s_captureEvents.clear();
#ifdef REPLAY_EVENTS
        s_iNextReplayEventIndex = 0;
#endif
        s_eventBuffersGameId = szGuid;
    }
}

static void ClearEventBuffers()
{
    s_ruinEvents.clear();
    s_voteEvents.clear();
    s_captureEvents.clear();
}

void CvHttp_RecordRuinEvent(const KekRuinEvent& evt)
{
    SyncEventBuffersToCurrentGame();
    if (s_ruinEvents.size() >= KEKMOD_MAX_EVENTS_PER_BUFFER)
        return;
    s_ruinEvents.push_back(evt);
    s_ruinEvents.back().iTurn = GC.getGame().getGameTurn();
}

void CvHttp_RecordVoteEvent(const KekVoteEvent& evt)
{
    SyncEventBuffersToCurrentGame();
    if (s_voteEvents.size() >= KEKMOD_MAX_EVENTS_PER_BUFFER)
        return;
    s_voteEvents.push_back(evt);
    s_voteEvents.back().iTurn = GC.getGame().getGameTurn();
}

void CvHttp_RecordCityCaptureEvent(const KekCityCaptureEvent& evt)
{
    SyncEventBuffersToCurrentGame();
    if (s_captureEvents.size() >= KEKMOD_MAX_EVENTS_PER_BUFFER)
        return;
    s_captureEvents.push_back(evt);
    s_captureEvents.back().iTurn = GC.getGame().getGameTurn();
}

// MPVotingSystemProposalTypes / MPVotingSystemProposalStatus -> JSON tokens.
static const char* VoteTypeToken(int iType)
{
    switch (iType)
    {
    case 0:  return "irr";
    case 1:  return "cc";
    case 2:  return "scrap";
    default: return "unknown";
    }
}
static const char* VoteStatusToken(int iStatus)
{
    switch (iStatus)
    {
    case 1:  return "passed";
    case 2:  return "failed";
    default: return "invalid";
    }
}

#ifdef REPLAY_EVENTS
// ReplayEventTypes (CvGame.h) -> the same token as the ReplayEvents.Type
// column in Override/CIV5Units.xml, indexed by enum value. Keep in sync with
// that enum -- there is no C++-side info class for this DB table to read the
// token back from, so this is a hand-kept mirror. NUM_REPLAYEVENTS entries.
static const char* const s_apszReplayEventTypeTokens[] =
{
    "REPLAYEVENT_AdvancedStartAction",
    "REPLAYEVENT_AutoMission",
    "REPLAYEVENT_BarbarianRansom",
    "REPLAYEVENT_ChangeWar",
    "REPLAYEVENT_IgnoreWarning",
    "REPLAYEVENT_CityBuyPlot",
    "REPLAYEVENT_CityDoTask",
    "REPLAYEVENT_CityPopOrder",
    "REPLAYEVENT_CityPurchase",
    "REPLAYEVENT_CityPushOrder",
    "REPLAYEVENT_CitySwapOrder",
    "REPLAYEVENT_ChooseElection",
    "REPLAYEVENT_DestroyUnit",
    "REPLAYEVENT_DiplomacyFromUI",
    "REPLAYEVENT_DiploVote",
    "REPLAYEVENT_DoCommand",
    "REPLAYEVENT_ExtendedGame",
    "REPLAYEVENT_FoundPantheon",
    "REPLAYEVENT_FoundReligion",
    "REPLAYEVENT_EnhanceReligion",
    "REPLAYEVENT_MoveSpy",
    "REPLAYEVENT_StageCoup",
    "REPLAYEVENT_FaithPurchase",
    "REPLAYEVENT_LeagueVoteEnact",
    "REPLAYEVENT_LeagueVoteRepeal",
    "REPLAYEVENT_LeagueVoteAbstain",
    "REPLAYEVENT_LeagueProposeEnact",
    "REPLAYEVENT_LeagueProposeRepeal",
    "REPLAYEVENT_LeagueEditName",
    "REPLAYEVENT_SetSwappableGreatWork",
    "REPLAYEVENT_SwapGreatWorks",
    "REPLAYEVENT_MoveGreatWorks",
    "REPLAYEVENT_ChangeIdeology",
    "REPLAYEVENT_GiftUnit",
    "REPLAYEVENT_LaunchSpaceship",
    "REPLAYEVENT_LiberatePlayer",
    "REPLAYEVENT_MinorCivBullyGold",
    "REPLAYEVENT_MinorCivBullyUnit",
    "REPLAYEVENT_MinorCivGiftGold",
    "REPLAYEVENT_MinorCivGiftTileImprovement",
    "REPLAYEVENT_MinorCivBuyout",
    "REPLAYEVENT_MinorNoUnitSpawning",
    "REPLAYEVENT_PlayerDealFinalized",
    "REPLAYEVENT_PlayerOption",
    "REPLAYEVENT_PledgeMinorProtection",
    "REPLAYEVENT_PushMission",
    "REPLAYEVENT_GreatPersonChoice",
    "REPLAYEVENT_MayaBonusChoice",
    "REPLAYEVENT_FaithGreatPersonChoice",
    "REPLAYEVENT_GoodyChoice",
    "REPLAYEVENT_ArchaeologyChoice",
    "REPLAYEVENT_IdeologyChoice",
    "REPLAYEVENT_RenameCity",
    "REPLAYEVENT_RenameUnit",
    "REPLAYEVENT_Research",
    "REPLAYEVENT_ReturnCivilian",
    "REPLAYEVENT_SellBuilding",
    "REPLAYEVENT_SetCityAIFocus",
    "REPLAYEVENT_SetCityAvoidGrowth",
    "REPLAYEVENT_SwapUnits",
    "REPLAYEVENT_UpdateCityCitizens",
    "REPLAYEVENT_UpdatePolicies",
    "REPLAYEVENT_CityPurchaseUnit",
    "REPLAYEVENT_CityPurchaseBuilding",
    "REPLAYEVENT_FreeTech",
    "REPLAYEVENT_StealTech",
    "REPLAYEVENT_ProposalIrr",
    "REPLAYEVENT_ProposalCc",
    "REPLAYEVENT_ProposalScrap",
    "REPLAYEVENT_ProposalYes",
    "REPLAYEVENT_ProposalNo",
    "REPLAYEVENT_ResetTimer",
    "REPLAYEVENT_PauseTimer",
    "REPLAYEVENT_OpenDemoScreen",
    "REPLAYEVENT_ExtractSpy",
    "REPLAYEVENT_UpdatePolicyBranch",
    "REPLAYEVENT_UnpauseTimer",
    "REPLAYEVENT_CityUnitComplete",
    "REPLAYEVENT_CityBuildingComplete",
    "REPLAYEVENT_CityGrowth",
    "REPLAYEVENT_CityStarvation",
    "REPLAYEVENT_CityBorderGrowth",
    "REPLAYEVENT_UnitDisbanded",
    "REPLAYEVENT_UnitKilledInCombat",
    "REPLAYEVENT_UnitPromotion",
    "REPLAYEVENT_UnitUpgrade",
    "REPLAYEVENT_ImprovementFinished",
    "REPLAYEVENT_GoodyHut",
    "REPLAYEVENT_NaturalWonderDiscovered",
    "REPLAYEVENT_CircumnavigatedGlobe",
    "REPLAYEVENT_MeetTeam",
    "REPLAYEVENT_TechAcquired",
    "REPLAYEVENT_AdvanceEra",
    "REPLAYEVENT_SpyCoupResult",
    "REPLAYEVENT_SpyOperationResult",
    "REPLAYEVENT_MinorQuestComplete",
    "REPLAYEVENT_MinorAllyChanged",
    "REPLAYEVENT_MinorFriendChanged",
    "REPLAYEVENT_CongressHostChange",
    "REPLAYEVENT_CongressEnactedProposalsChange",
    "REPLAYEVENT_MPProposalResult",
    "REPLAYEVENT_PlotNewCityName",
    "REPLAYEVENT_EnterCityScreen",
    "REPLAYEVENT_AddTerrain",
    "REPLAYEVENT_BuildProgress",
    "REPLAYEVENT_FeatureChanged",
    "REPLAYEVENT_ResourceChanged",
    "REPLAYEVENT_RouteChanged",
    "REPLAYEVENT_TileOwnerChanged",
    "REPLAYEVENT_YieldChanged",
};
#define KEKMOD_NUM_REPLAY_EVENT_TOKENS \
    (sizeof(s_apszReplayEventTypeTokens) / sizeof(s_apszReplayEventTypeTokens[0]))

static const char* ReplayEventTypeToken(int eType)
{
    if (eType < 0 || (size_t)eType >= KEKMOD_NUM_REPLAY_EVENT_TOKENS)
        return "REPLAYEVENT_UNKNOWN";
    return s_apszReplayEventTypeTokens[eType];
}

// Compile-time guard: catches a forgotten token entry the next time someone
// adds a REPLAYEVENT_* without updating s_apszReplayEventTypeTokens above.
typedef char kekmod_ReplayEventTokenCountCheck[
    (NUM_REPLAYEVENTS == KEKMOD_NUM_REPLAY_EVENT_TOKENS) ? 1 : -1];
#endif


// ---------------------------------------------------------------------------
// JSON building (GAME THREAD ONLY -- reads live game state)
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

static void JsonAppendFmt(std::string& out, const char* pszFmt, ...)
{
    char szBuf[512];
    va_list args;
    va_start(args, pszFmt);
    vsnprintf_s(szBuf, sizeof(szBuf), _TRUNCATE, pszFmt, args);
    va_end(args);
    out += szBuf;
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

// Persona without the "@steamid" suffix.
static std::string PersonaFromNickname(const CvString& strNick)
{
    const char* psz = strNick.c_str();
    const char* pAt = strrchr(psz, '@');
    if (pAt && !SteamIdFromNickname(strNick).empty())
        return std::string(psz, pAt - psz);
    return std::string(psz);
}

// NULL-safe XML type token ("UNIT_WARRIOR", "BUILDING_GRANARY", ...).
static const char* TokenOrEmpty(const CvBaseInfo* pkInfo)
{
    return pkInfo ? pkInfo->GetType() : "";
}

// ---------------------------------------------------------------------------
// Regional luxuries (GAME THREAD ONLY)
//
// Civ5's start placement gives each major 1-3 copies of its region's luxury
// within 3 hexes of the STARTING plot plus a loose cluster in the region;
// each luxury type serves one region at <=8 civs. Mirror of the detection the
// server-side parser validated: candidates need >=1 copy within 3 hexes,
// score = copies<=3 * 10 + copies<=9, then assign types to starts 1:1 by
// descending score. Starting plots are serialized, so this works on reloads.
// ---------------------------------------------------------------------------

struct KekLuxCandidate
{
    int           iScore;
    int           iX, iY;
    int           iSlot;
    ResourceTypes eResource;
};

static bool LuxCandidateGreater(const KekLuxCandidate& a, const KekLuxCandidate& b)
{
    if (a.iScore != b.iScore) return a.iScore > b.iScore;
    if (a.iX != b.iX)         return a.iX > b.iX;
    if (a.iY != b.iY)         return a.iY > b.iY;
    if (a.iSlot != b.iSlot)   return a.iSlot > b.iSlot;
    return (int)a.eResource > (int)b.eResource;
}

// Appends "regionalLuxuries":{"0":"RESOURCE_X",...} (slots with a detected
// assignment only). Starting plots never change, so this is computed and
// SENT once per game per session: the first upload of a game carries it and
// later ones omit the field entirely. Statics reset on app restart, so a
// reload re-sends it once more -- harmless, the server upsert is idempotent
// -- which also heals a lost first upload.
static void AppendRegionalLuxuries(std::string& out)
{
    static std::string s_sentForGameId;
    {
        char szGuid[40];
        FormatMapGuid(szGuid, sizeof(szGuid));
        if (s_sentForGameId == szGuid)
            return;
        s_sentForGameId = szGuid;
    }

    CvMap& kMap = GC.getMap();
    const ResourceClassTypes eLuxClass =
        (ResourceClassTypes)GC.getInfoTypeForString("RESOURCECLASS_LUXURY", true);

    // Collect luxury tiles once (~60-80 on a standard map).
    std::vector<CvPlot*> luxPlots;
    for (int iI = 0; iI < kMap.numPlots(); iI++)
    {
        CvPlot* pPlot = kMap.plotByIndexUnchecked(iI);
        ResourceTypes eRes = (ResourceTypes)pPlot->getResourceType();
        if (eRes == NO_RESOURCE)
            continue;
        CvResourceInfo* pkRes = GC.getResourceInfo(eRes);
        if (pkRes && eLuxClass != NO_RESOURCECLASS &&
            pkRes->getResourceClassType() == eLuxClass)
            luxPlots.push_back(pPlot);
    }

    // Score every (major start, luxury type) pair.
    std::vector<KekLuxCandidate> cands;
    for (int iI = 0; iI < MAX_MAJOR_CIVS; iI++)
    {
        CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);
        if (!kPlayer.isEverAlive())
            continue;
        CvPlot* pStart = kPlayer.getStartingPlot();
        if (pStart == NULL)
            continue;

        int aiC3[512]; int aiC9[512];   // indexed by ResourceTypes
        memset(aiC3, 0, sizeof(aiC3));
        memset(aiC9, 0, sizeof(aiC9));
        for (size_t j = 0; j < luxPlots.size(); ++j)
        {
            int iDist = plotDistance(pStart->getX(), pStart->getY(),
                                     luxPlots[j]->getX(), luxPlots[j]->getY());
            int iRes = (int)luxPlots[j]->getResourceType();
            if (iRes < 0 || iRes >= 512 || iDist > 9)
                continue;
            if (iDist <= 3) aiC3[iRes]++;
            aiC9[iRes]++;
        }
        for (int iRes = 0; iRes < GC.getNumResourceInfos() && iRes < 512; iRes++)
        {
            if (aiC3[iRes] < 1)
                continue;
            KekLuxCandidate cand;
            cand.iScore    = aiC3[iRes] * 10 + aiC9[iRes];
            cand.iX        = pStart->getX();
            cand.iY        = pStart->getY();
            cand.iSlot     = iI;
            cand.eResource = (ResourceTypes)iRes;
            cands.push_back(cand);
        }
    }
    std::sort(cands.begin(), cands.end(), LuxCandidateGreater);

    // Unique 1:1 assignment: best remaining (start, type) pair wins.
    bool abSlotDone[MAX_MAJOR_CIVS];
    bool abTypeDone[512];
    memset(abSlotDone, 0, sizeof(abSlotDone));
    memset(abTypeDone, 0, sizeof(abTypeDone));

    out += "\"regionalLuxuries\":{";
    bool bFirst = true;
    for (size_t i = 0; i < cands.size(); ++i)
    {
        const KekLuxCandidate& cand = cands[i];
        if (abSlotDone[cand.iSlot] || abTypeDone[(int)cand.eResource])
            continue;
        abSlotDone[cand.iSlot] = true;
        abTypeDone[(int)cand.eResource] = true;

        if (!bFirst) out += ",";
        bFirst = false;
        JsonAppendFmt(out, "\"%d\":\"", cand.iSlot);
        JsonEscape(out, TokenOrEmpty(GC.getResourceInfo(cand.eResource)));
        out += "\"";
    }
    out += "},";
}


// Serialize schema-v2 turn digest.  MUST be called on the game thread.
static void BuildTurnJson(std::string& out, PlayerTypes eUploader)
{
    CvGame& kGame = GC.getGame();
    CvMap&  kMap  = GC.getMap();

    out.reserve(64 * 1024);
    out += "{";
    JsonAppendFmt(out, "\"schema\":%d,", KEKMOD_JSON_SCHEMA);
    JsonAppendFmt(out, "\"modVersion\":\"%s\",", KEKMOD_MOD_VERSION);
    JsonAppendFmt(out, "\"gameTurn\":%d,", kGame.getGameTurn());
    JsonAppendFmt(out, "\"gameStartYear\":%d,", kGame.getStartYear());

    out += "\"mapScript\":\"";
    JsonEscape(out, CvPreGame::mapScriptName().c_str());
    out += "\",";
    JsonAppendFmt(out, "\"mapSizeX\":%d,\"mapSizeY\":%d,",
                  kMap.getGridWidth(), kMap.getGridHeight());

    // Stable per-game id: the map GUID (also present in every save body).
    {
        char szGuid[40];
        FormatMapGuid(szGuid, sizeof(szGuid));
        JsonAppendFmt(out, "\"gameId\":\"%s\",", szGuid);
    }

    out += "\"uploaderSteamId\":\"";
    out += SteamIdFromNickname(CvPreGame::nickname(eUploader));
    out += "\",";

    // v5: present only once the game has ended (the game-end flush). For a
    // scrap "victory" each client reports its own team, so GDR must treat
    // VICTORY_SCRAP as a draw and ignore team/slots.
    if (kGame.getWinner() != NO_TEAM && kGame.getVictory() != NO_VICTORY)
    {
        CvVictoryInfo* pkVictory = GC.getVictoryInfo(kGame.getVictory());
        JsonAppendFmt(out, "\"winner\":{\"team\":%d,\"victory\":\"", (int)kGame.getWinner());
        JsonEscape(out, pkVictory ? pkVictory->GetType() : "");
        out += "\",\"slots\":[";
        bool bFirstWinner = true;
        for (int iW = 0; iW < MAX_MAJOR_CIVS; iW++)
        {
            CvPlayer& kWinPlayer = GET_PLAYER((PlayerTypes)iW);
            if (!kWinPlayer.isEverAlive() || kWinPlayer.getTeam() != kGame.getWinner())
                continue;
            if (!bFirstWinner) out += ",";
            bFirstWinner = false;
            JsonAppendFmt(out, "%d", iW);
        }
        out += "]},";
    }

    // v6: per-slot regional luxury -- computed and sent once per game per
    // session (absent from all later payloads).
    AppendRegionalLuxuries(out);

    // Major civs only (slots 0..MAX_MAJOR_CIVS-1); map + human players focus.
    out += "\"players\":[";
    bool bFirstPlayer = true;
    for (int iI = 0; iI < MAX_MAJOR_CIVS; iI++)
    {
        CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);
        if (!kPlayer.isEverAlive())
            continue;

        if (!bFirstPlayer) out += ",";
        bFirstPlayer = false;

        out += "{";
        JsonAppendFmt(out, "\"slot\":%d,", iI);

        out += "\"steamId\":\"";
        out += SteamIdFromNickname(CvPreGame::nickname((PlayerTypes)iI));
        out += "\",\"persona\":\"";
        JsonEscape(out, PersonaFromNickname(CvPreGame::nickname((PlayerTypes)iI)).c_str());
        out += "\",";

        out += "\"civ\":\"";
        JsonEscape(out, TokenOrEmpty(GC.getCivilizationInfo(kPlayer.getCivilizationType())));
        out += "\",";

        JsonAppendFmt(out, "\"human\":%s,", kPlayer.isHuman() ? "true" : "false");
        JsonAppendFmt(out, "\"alive\":%s,", kPlayer.isAlive() ? "true" : "false");

        // v4: live network presence + current capital-loss state (the
        // authoritative capital record is the cityCaptures event stream).
        JsonAppendFmt(out, "\"connected\":%s,", kPlayer.isConnected() ? "true" : "false");
        JsonAppendFmt(out, "\"lostCapital\":%s,", kPlayer.IsHasLostCapital() ? "true" : "false");
        JsonAppendFmt(out, "\"score\":%d,", kPlayer.GetScore());
        JsonAppendFmt(out, "\"gold\":%d,", kPlayer.GetTreasury()->GetGold());
        JsonAppendFmt(out, "\"goldPerTurn\":%d,", kPlayer.calculateGoldRate());
        JsonAppendFmt(out, "\"sciencePerTurn\":%d,", kPlayer.GetScienceTimes100() / 100);
        JsonAppendFmt(out, "\"culture\":%d,", kPlayer.getJONSCulture());
        JsonAppendFmt(out, "\"culturePerTurn\":%d,", kPlayer.GetTotalJONSCulturePerTurn());
        JsonAppendFmt(out, "\"faith\":%d,", kPlayer.GetFaith());
        JsonAppendFmt(out, "\"happiness\":%d,", kPlayer.GetExcessHappiness());
        JsonAppendFmt(out, "\"militaryMight\":%d,", kPlayer.GetMilitaryMight());
        JsonAppendFmt(out, "\"techCount\":%d,",
                      GET_TEAM(kPlayer.getTeam()).GetTeamTechs()->GetNumTechsKnown());
        JsonAppendFmt(out, "\"policyCount\":%d,",
                      kPlayer.GetPlayerPolicies()->GetNumPoliciesOwned());
        JsonAppendFmt(out, "\"numCities\":%d,", kPlayer.getNumCities());
        JsonAppendFmt(out, "\"population\":%d,", kPlayer.getTotalPopulation());

        // v2: current research (empty token + zeros when idle)
        {
            CvPlayerTechs* pkTechs = kPlayer.GetPlayerTechs();
            TechTypes eResearch = pkTechs ? pkTechs->GetCurrentResearch() : NO_TECH;
            out += "\"researching\":\"";
            if (eResearch != NO_TECH)
                JsonEscape(out, TokenOrEmpty(GC.getTechInfo(eResearch)));
            out += "\",";
            JsonAppendFmt(out, "\"researchProgress\":%d,\"researchCost\":%d,",
                          eResearch != NO_TECH ? pkTechs->GetResearchProgress(eResearch) : 0,
                          eResearch != NO_TECH ? pkTechs->GetResearchCost(eResearch) : 0);
        }

        // v2: adopted policies (XML tokens)
        out += "\"policies\":[";
        {
            bool bFirst = true;
            for (int iP = 0; iP < GC.getNumPolicyInfos(); iP++)
            {
                if (!kPlayer.GetPlayerPolicies()->HasPolicy((PolicyTypes)iP))
                    continue;
                if (!bFirst) out += ",";
                bFirst = false;
                out += "\"";
                JsonEscape(out, TokenOrEmpty(GC.getPolicyInfo((PolicyTypes)iP)));
                out += "\"";
            }
        }
        out += "],";

        // v2: religion founded ("" if none; pantheon reports RELIGION_PANTHEON)
        {
            ReligionTypes eReligion = kPlayer.GetReligions()->GetReligionCreatedByPlayer();
            out += "\"religionFounded\":\"";
            if (eReligion != NO_RELIGION)
                JsonEscape(out, TokenOrEmpty(GC.getReligionInfo(eReligion)));
            out += "\",";
        }

        // v2: owned resources (incl. imports), nonzero only
        out += "\"resources\":{";
        {
            bool bFirst = true;
            for (int iR = 0; iR < GC.getNumResourceInfos(); iR++)
            {
                int iCount = kPlayer.getNumResourceTotal((ResourceTypes)iR, true);
                if (iCount <= 0)
                    continue;
                if (!bFirst) out += ",";
                bFirst = false;
                out += "\"";
                JsonEscape(out, TokenOrEmpty(GC.getResourceInfo((ResourceTypes)iR)));
                out += "\"";
                JsonAppendFmt(out, ":%d", iCount);
            }
        }
        out += "},";

        // v2: major-civ slots this player's team is at war with
        out += "\"atWarWith\":[";
        {
            bool bFirst = true;
            for (int iJ = 0; iJ < MAX_MAJOR_CIVS; iJ++)
            {
                if (iJ == iI)
                    continue;
                CvPlayer& kOther = GET_PLAYER((PlayerTypes)iJ);
                if (!kOther.isEverAlive())
                    continue;
                if (!GET_TEAM(kPlayer.getTeam()).isAtWar(kOther.getTeam()))
                    continue;
                if (!bFirst) out += ",";
                bFirst = false;
                JsonAppendFmt(out, "%d", iJ);
            }
        }
        out += "],";

        out += "\"cities\":[";
        int iCityLoop = 0;
        bool bFirstCity = true;
        for (const CvCity* pCity = kPlayer.firstCity(&iCityLoop);
             pCity != NULL; pCity = kPlayer.nextCity(&iCityLoop))
        {
            if (!bFirstCity) out += ",";
            bFirstCity = false;
            out += "{";
            JsonAppendFmt(out, "\"x\":%d,\"y\":%d,", pCity->getX(), pCity->getY());
            out += "\"name\":\"";
            JsonEscape(out, pCity->getName().c_str());
            out += "\",";
            JsonAppendFmt(out, "\"pop\":%d,", pCity->getPopulation());
            JsonAppendFmt(out, "\"capital\":%s,", pCity->isCapital() ? "true" : "false");
            JsonAppendFmt(out, "\"foundedTurn\":%d,", pCity->getGameTurnFounded());

            // v2: ownership history + status flags
            JsonAppendFmt(out, "\"originalOwner\":%d,", (int)pCity->getOriginalOwner());
            JsonAppendFmt(out, "\"acquiredTurn\":%d,", pCity->getGameTurnAcquired());
            JsonAppendFmt(out, "\"puppet\":%s,", pCity->IsPuppet() ? "true" : "false");
            JsonAppendFmt(out, "\"occupied\":%s,", pCity->IsOccupied() ? "true" : "false");
            JsonAppendFmt(out, "\"razing\":%s,", pCity->IsRazing() ? "true" : "false");
            JsonAppendFmt(out, "\"resistance\":%s,", pCity->IsResistance() ? "true" : "false");
            JsonAppendFmt(out, "\"damage\":%d,\"maxHp\":%d,",
                          pCity->getDamage(), pCity->GetMaxHitPoints());

            // v2: per-turn yields
            JsonAppendFmt(out, "\"yieldFood\":%d,", pCity->getYieldRate(YIELD_FOOD, false));
            JsonAppendFmt(out, "\"yieldProduction\":%d,", pCity->getYieldRate(YIELD_PRODUCTION, false));
            JsonAppendFmt(out, "\"yieldGold\":%d,", pCity->getYieldRate(YIELD_GOLD, false));
            JsonAppendFmt(out, "\"yieldScience\":%d,", pCity->getYieldRate(YIELD_SCIENCE, false));
            JsonAppendFmt(out, "\"culturePerTurn\":%d,", pCity->getJONSCulturePerTurn());
            JsonAppendFmt(out, "\"faithPerTurn\":%d,", pCity->GetFaithPerTurn());

            // v2: majority religion ("" if none)
            {
                ReligionTypes eMajority = pCity->GetCityReligions()->GetReligiousMajority();
                out += "\"religion\":\"";
                if (eMajority != NO_RELIGION)
                    JsonEscape(out, TokenOrEmpty(GC.getReligionInfo(eMajority)));
                out += "\",";
            }

            // v2: constructed buildings (real only -- excludes free/trait ones)
            out += "\"buildings\":[";
            {
                const CvCityBuildings* pkBuildings = pCity->GetCityBuildings();
                bool bFirst = true;
                for (int iB = 0; iB < GC.getNumBuildingInfos(); iB++)
                {
                    if (pkBuildings->GetNumRealBuilding((BuildingTypes)iB) <= 0)
                        continue;
                    if (!bFirst) out += ",";
                    bFirst = false;
                    out += "\"";
                    JsonEscape(out, TokenOrEmpty(GC.getBuildingInfo((BuildingTypes)iB)));
                    out += "\"";
                }
            }
            out += "],";

            // v2: full production queue; head item carries the ETA
            out += "\"queue\":[";
            {
                bool bFirst = true;
                for (const OrderData* pOrder = pCity->headOrderQueueNode();
                     pOrder != NULL; pOrder = pCity->nextOrderQueueNode(pOrder))
                {
                    if (!bFirst) out += ",";

                    const char* pszKind = "unknown";
                    const char* pszItem = "";
                    switch (pOrder->eOrderType)
                    {
                    case ORDER_TRAIN:
                        pszKind = "unit";
                        pszItem = TokenOrEmpty(GC.getUnitInfo((UnitTypes)pOrder->iData1));
                        break;
                    case ORDER_CONSTRUCT:
                        pszKind = "building";
                        pszItem = TokenOrEmpty(GC.getBuildingInfo((BuildingTypes)pOrder->iData1));
                        break;
                    case ORDER_CREATE:
                        pszKind = "project";
                        pszItem = TokenOrEmpty(GC.getProjectInfo((ProjectTypes)pOrder->iData1));
                        break;
                    case ORDER_PREPARE:
                        pszKind = "specialist";
                        pszItem = TokenOrEmpty(GC.getSpecialistInfo((SpecialistTypes)pOrder->iData1));
                        break;
                    case ORDER_MAINTAIN:
                        pszKind = "process";
                        pszItem = TokenOrEmpty(GC.getProcessInfo((ProcessTypes)pOrder->iData1));
                        break;
                    default:
                        break;
                    }

                    out += "{\"kind\":\"";
                    out += pszKind;
                    out += "\",\"item\":\"";
                    JsonEscape(out, pszItem);
                    out += "\"";
                    if (bFirst)
                        JsonAppendFmt(out, ",\"turnsLeft\":%d", pCity->getProductionTurnsLeft());
                    out += "}";
                    bFirst = false;
                }
            }
            out += "]";
            out += "}";
        }
        out += "],";

        // v2: every unit on the map for this player
        out += "\"units\":[";
        {
            int iUnitLoop = 0;
            bool bFirst = true;
            for (const CvUnit* pUnit = kPlayer.firstUnit(&iUnitLoop);
                 pUnit != NULL; pUnit = kPlayer.nextUnit(&iUnitLoop))
            {
                if (!bFirst) out += ",";
                bFirst = false;
                out += "{";
                JsonAppendFmt(out, "\"x\":%d,\"y\":%d,", pUnit->getX(), pUnit->getY());
                out += "\"type\":\"";
                JsonEscape(out, TokenOrEmpty(GC.getUnitInfo(pUnit->getUnitType())));
                out += "\",";
                JsonAppendFmt(out, "\"hp\":%d,\"maxHp\":%d,",
                              pUnit->GetCurrHitPoints(), pUnit->GetMaxHitPoints());
                JsonAppendFmt(out, "\"xp\":%d,\"level\":%d",
                              pUnit->getExperience(), pUnit->getLevel());
                out += "}";
            }
        }
        out += "]";
#ifdef ENHANCED_GRAPHS
        // v7: Enhanced Graphs per-player stats, current-turn snapshot of every
        // EG_REPLAYDATASET_* the DLL now records (same values the in-game replay
        // graphs show). "_v":1 is a dummy always-present field so the object
        // opens without a leading comma.
        out += ",\"stats\":{\"_v\":1";
#ifdef EG_REPLAYDATASET_FAITHPERTURN
        JsonAppendFmt(out, ",\"faithPerTurn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_FAITHPERTURN"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALFAITH
        JsonAppendFmt(out, ",\"totalFaith\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALFAITH"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNSCIENTISTS
        JsonAppendFmt(out, ",\"greatScientistsBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNSCIENTISTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTSCIENTISTS
        JsonAppendFmt(out, ",\"greatScientistsBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTSCIENTISTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFSCIENTISTS
        JsonAppendFmt(out, ",\"totalGreatScientists\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFSCIENTISTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNENGINEERS
        JsonAppendFmt(out, ",\"greatEngineersBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNENGINEERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTENGINEERS
        JsonAppendFmt(out, ",\"greatEngineersBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTENGINEERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFENGINEERS
        JsonAppendFmt(out, ",\"totalGreatEngineers\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFENGINEERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNMERCHANTS
        JsonAppendFmt(out, ",\"greatMerchantsBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNMERCHANTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTMERCHANTS
        JsonAppendFmt(out, ",\"greatMerchantsBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTMERCHANTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFMERCHANTS
        JsonAppendFmt(out, ",\"totalGreatMerchants\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFMERCHANTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNWRITERS
        JsonAppendFmt(out, ",\"greatWritersBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNWRITERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTWRITERS
        JsonAppendFmt(out, ",\"greatWritersBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTWRITERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFWRITERS
        JsonAppendFmt(out, ",\"totalGreatWriters\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFWRITERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNARTISTS
        JsonAppendFmt(out, ",\"greatArtistsBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNARTISTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTARTISTS
        JsonAppendFmt(out, ",\"greatArtistsBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTARTISTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFARTISTS
        JsonAppendFmt(out, ",\"totalGreatArtists\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFARTISTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNMUSICIANS
        JsonAppendFmt(out, ",\"greatMusiciansBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNMUSICIANS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTMUSICIANS
        JsonAppendFmt(out, ",\"greatMusiciansBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTMUSICIANS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFMUSICIANS
        JsonAppendFmt(out, ",\"totalGreatMusicians\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFMUSICIANS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNGENERALS
        JsonAppendFmt(out, ",\"greatGeneralsBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNGENERALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTGENERALS
        JsonAppendFmt(out, ",\"greatGeneralsBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTGENERALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFGENERALS
        JsonAppendFmt(out, ",\"totalGreatGenerals\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFGENERALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBORNADMIRALS
        JsonAppendFmt(out, ",\"greatAdmiralsBorn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBORNADMIRALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTADMIRALS
        JsonAppendFmt(out, ",\"greatAdmiralsBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTADMIRALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFADMIRALS
        JsonAppendFmt(out, ",\"totalGreatAdmirals\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFADMIRALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMOFBOUGHTPROPHETS
        JsonAppendFmt(out, ",\"greatProphetsBought\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMOFBOUGHTPROPHETS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALNUMOFPROPHETS
        JsonAppendFmt(out, ",\"totalGreatProphets\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALNUMOFPROPHETS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_GOLDFROMBULLYING
        JsonAppendFmt(out, ",\"goldFromBullyingCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_GOLDFROMBULLYING"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_WORKERSFROMBULLING
        JsonAppendFmt(out, ",\"workersFromBullyingCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_WORKERSFROMBULLING"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMTRAINEDUNITS
        JsonAppendFmt(out, ",\"unitsTrained\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMTRAINEDUNITS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMLOSTUNITS
        JsonAppendFmt(out, ",\"unitsLost\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMLOSTUNITS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMKILLEDUNITS
        JsonAppendFmt(out, ",\"unitsKilled\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMKILLEDUNITS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMBUILTWONDERS
        JsonAppendFmt(out, ",\"wondersBuilt\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMBUILTWONDERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMREVEALEDTILES
        JsonAppendFmt(out, ",\"tilesRevealed\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMREVEALEDTILES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMSTOLENSCIENCE
        JsonAppendFmt(out, ",\"scienceStolen\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMSTOLENSCIENCE"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_DAMAGEDEALTTOUNITS
        JsonAppendFmt(out, ",\"damageDealtToUnits\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_DAMAGEDEALTTOUNITS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_DAMAGEDEALTTOCITIES
        JsonAppendFmt(out, ",\"damageDealtToCities\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_DAMAGEDEALTTOCITIES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_DAMAGETAKENBYUNITS
        JsonAppendFmt(out, ",\"damageTakenByUnits\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_DAMAGETAKENBYUNITS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_DAMAGETAKENBYCITIES
        JsonAppendFmt(out, ",\"damageTakenByCities\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_DAMAGETAKENBYCITIES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMDELEGATES
        JsonAppendFmt(out, ",\"congressDelegates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMDELEGATES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALCHOPS
        JsonAppendFmt(out, ",\"forestsJunglesChopped\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALCHOPS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_LOSTHAMMERSFROMLOSTWONDERS
        JsonAppendFmt(out, ",\"productionLostToFailedWonderRaces\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_LOSTHAMMERSFROMLOSTWONDERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMTIMESOPENEDDEMOGRAPHICS
        JsonAppendFmt(out, ",\"timesOpenedDemographics\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMTIMESOPENEDDEMOGRAPHICS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_SCIENTISTSTOTALSCIENCEBOOST
        JsonAppendFmt(out, ",\"totalScienceFromGreatScientists\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_SCIENTISTSTOTALSCIENCEBOOST"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_ENGINEERSTOTALHURRYBOOST
        JsonAppendFmt(out, ",\"totalProductionFromGreatEngineers\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_ENGINEERSTOTALHURRYBOOST"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_MERCHANTSTOTALTRADEBOOST
        JsonAppendFmt(out, ",\"totalGoldFromGreatMerchants\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_MERCHANTSTOTALTRADEBOOST"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_WRITERSTOTALCULTUREBOOST
        JsonAppendFmt(out, ",\"totalCultureFromGreatWriters\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_WRITERSTOTALCULTUREBOOST"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_MUSICIANSTOTALTOURISMBOOST
        JsonAppendFmt(out, ",\"totalTourismFromGreatMusicians\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_MUSICIANSTOTALTOURISMBOOST"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_POPULATIONLOSTFROMNUKES
        JsonAppendFmt(out, ",\"populationLostToNukes\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_POPULATIONLOSTFROMNUKES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_CSQUESTSCOMPLETED
        JsonAppendFmt(out, ",\"cityStateQuestsCompleted\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_CSQUESTSCOMPLETED"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_ALLIEDCS
        JsonAppendFmt(out, ",\"alliedCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_ALLIEDCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TIMESENTEREDCITYSCREEN
        JsonAppendFmt(out, ",\"timesEnteredCityScreen\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TIMESENTEREDCITYSCREEN"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_HAPPINESSFROMTRADEDEALS
        JsonAppendFmt(out, ",\"happinessFromTradeDeals\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_HAPPINESSFROMTRADEDEALS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_PERCENTOFCITIESWITHACTIVEWLTKD
        JsonAppendFmt(out, ",\"citiesInWeLoveTheKingDay\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_PERCENTOFCITIESWITHACTIVEWLTKD"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_FOLLOWERSOFPLAYERRELIGION
        JsonAppendFmt(out, ",\"followersYourReligion\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_FOLLOWERSOFPLAYERRELIGION"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_CITIESCONVERTEDTOPLAYERRELIGION
        JsonAppendFmt(out, ",\"citiesFollowingYourReligion\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_CITIESCONVERTEDTOPLAYERRELIGION"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOTALSPECIALISTCITIZENS
        JsonAppendFmt(out, ",\"totalSpecialistCitizens\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOTALSPECIALISTCITIZENS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_PERCENTSPECIALISTCITIZENS
        JsonAppendFmt(out, ",\"possibleSpecialistsFilled\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_PERCENTSPECIALISTCITIZENS"), kGame.getGameTurn()));
#endif
        JsonAppendFmt(out, ",\"goldPerTurnFromInternationalTradeRoutes\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_GPTINTERNATIONALTRADE"), kGame.getGameTurn()));
#ifdef EG_REPLAYDATASET_EFFECTIVESCIENCEPERTURN
        JsonAppendFmt(out, ",\"effectiveSciencePerTurn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_EFFECTIVESCIENCEPERTURN"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_DIEDSPIES
        JsonAppendFmt(out, ",\"spiesLost\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_DIEDSPIES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_KILLEDSPIES
        JsonAppendFmt(out, ",\"enemySpiesKilled\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_KILLEDSPIES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_FOODFROMCS
        JsonAppendFmt(out, ",\"foodFromCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_FOODFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_PRODUCTIONFROMCS
        JsonAppendFmt(out, ",\"productionFromCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_PRODUCTIONFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_CULTUREFROMCS
        JsonAppendFmt(out, ",\"cultureFromCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_CULTUREFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_SCIENCEFROMCS
        JsonAppendFmt(out, ",\"scienceFromCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_SCIENCEFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_FAITHFROMCS
        JsonAppendFmt(out, ",\"faithFromCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_FAITHFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_HAPPINESSFROMCS
        JsonAppendFmt(out, ",\"happinessFromCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_HAPPINESSFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_UNITSFROMCS
        JsonAppendFmt(out, ",\"unitsGiftedByCityStates\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_UNITSFROMCS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_TOURISMPERTURN
        JsonAppendFmt(out, ",\"tourismPerTurn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_TOURISMPERTURN"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGREATWORKSANDARTIFACTS
        JsonAppendFmt(out, ",\"greatWorksAndArtifacts\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGREATWORKSANDARTIFACTS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMLUXURY
        JsonAppendFmt(out, ",\"luxuryResourceTilesOwned\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMLUXURY"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMWORLDWONDERS
        JsonAppendFmt(out, ",\"worldWondersOwned\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMWORLDWONDERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMCREATEDWORLDWONDERS
        JsonAppendFmt(out, ",\"worldWondersBuiltByYou\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMCREATEDWORLDWONDERS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGPIMPROVEMENT
        JsonAppendFmt(out, ",\"greatPersonImprovements\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGPIMPROVEMENT"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGOLDONBUILDINGBUYS
        JsonAppendFmt(out, ",\"goldSpentBuyingBuildings\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGOLDONBUILDINGBUYS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGOLDONUNITBUYS
        JsonAppendFmt(out, ",\"goldSpentBuyingUnits\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGOLDONUNITBUYS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGOLDONUPGRADES
        JsonAppendFmt(out, ",\"goldSpentOnUnitUpgrades\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGOLDONUPGRADES"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_GOLDFROMKILLS
        JsonAppendFmt(out, ",\"goldFromKills\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_GOLDFROMKILLS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_CULTUREFROMKILLS
        JsonAppendFmt(out, ",\"cultureFromKills\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_CULTUREFROMKILLS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_EFFECTIVECULTUREPERTURN
        JsonAppendFmt(out, ",\"effectiveCulturePerTurn\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_EFFECTIVECULTUREPERTURN"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGOLDONGREATPEOPLEBUYS
        JsonAppendFmt(out, ",\"goldSpentBuyingGreatPeople\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGOLDONGREATPEOPLEBUYS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMGOLDONTILESBUYS
        JsonAppendFmt(out, ",\"goldSpentBuyingTiles\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMGOLDONTILESBUYS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_GOLDFROMPILLAGING
        JsonAppendFmt(out, ",\"goldFromPillaging\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_GOLDFROMPILLAGING"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_GOLDFROMPLUNDERING
        JsonAppendFmt(out, ",\"goldFromPlunderingTradeRoutes\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_GOLDFROMPLUNDERING"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_NUMFAITHONMILITARYUNITS
        JsonAppendFmt(out, ",\"faithSpentOnMilitaryUnits\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_NUMFAITHONMILITARYUNITS"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_FOODFROMTRADEROUTES_TIMES100
        JsonAppendFmt(out, ",\"foodFromTradeRoutesTimes100\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_FOODFROMTRADEROUTES_TIMES100"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_PRODUCTIONFROMTRADEROUTES_TIMES100
        JsonAppendFmt(out, ",\"productionFromTradeRoutesTimes100\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_PRODUCTIONFROMTRADEROUTES_TIMES100"), kGame.getGameTurn()));
#endif
#ifdef EG_REPLAYDATASET_ANARCHYTURNS
        JsonAppendFmt(out, ",\"turnsSpentInAnarchy\":%d", kPlayer.getReplayDataValue(kPlayer.getReplayDataSetIndex("REPLAYDATASET_ANARCHYTURNS"), kGame.getGameTurn()));
#endif
        out += "}";
#endif
        out += "}";
    }
    out += "]";

    // v3: ancient ruins popped since the previous upload, with actual rewards.
    // Hidden info (opponents only see the hut vanish) -- post-game use only.
    SyncEventBuffersToCurrentGame();   // drop stale events if a new game loaded
    out += ",\"ruins\":[";
    for (size_t i = 0; i < s_ruinEvents.size(); ++i)
    {
        const KekRuinEvent& evt = s_ruinEvents[i];
        if (i) out += ",";
        out += "{";
        JsonAppendFmt(out, "\"turn\":%d,\"slot\":%d,\"x\":%d,\"y\":%d",
                      evt.iTurn, evt.iSlot, evt.iX, evt.iY);
        out += ",\"goody\":\"";
        JsonEscape(out, evt.szGoody);
        out += "\"";
        if (evt.iGold)       JsonAppendFmt(out, ",\"gold\":%d",       evt.iGold);
        if (evt.iCulture)    JsonAppendFmt(out, ",\"culture\":%d",    evt.iCulture);
        if (evt.iFaith)      JsonAppendFmt(out, ",\"faith\":%d",      evt.iFaith);
        if (evt.iPopulation) JsonAppendFmt(out, ",\"population\":%d", evt.iPopulation);
        if (evt.iScience)    JsonAppendFmt(out, ",\"science\":%d",    evt.iScience);
        if (evt.iExperience) JsonAppendFmt(out, ",\"xp\":%d",         evt.iExperience);
        if (evt.iHeal)       JsonAppendFmt(out, ",\"heal\":%d",       evt.iHeal);
        if (evt.iBarbCount)  JsonAppendFmt(out, ",\"barbs\":%d",      evt.iBarbCount);
        if (evt.iMapReveal)  out += ",\"mapReveal\":true";
        if (evt.szTech[0])        { out += ",\"tech\":\"";        JsonEscape(out, evt.szTech);        out += "\""; }
        if (evt.szUnit[0])        { out += ",\"unit\":\"";        JsonEscape(out, evt.szUnit);        out += "\""; }
        if (evt.szUpgradeUnit[0]) { out += ",\"upgradeUnit\":\""; JsonEscape(out, evt.szUpgradeUnit); out += "\""; }
        if (evt.szResource[0])    { out += ",\"resource\":\"";    JsonEscape(out, evt.szResource);    out += "\""; }
        out += "}";
    }
    out += "]";

    // v4: MP voting-system activity (proposals / votes / results).
    out += ",\"votes\":[";
    for (size_t i = 0; i < s_voteEvents.size(); ++i)
    {
        const KekVoteEvent& evt = s_voteEvents[i];
        if (i) out += ",";
        out += "{\"kind\":\"";
        out += evt.szKind;
        out += "\"";
        JsonAppendFmt(out, ",\"turn\":%d,\"id\":%d", evt.iTurn, evt.iProposalId);
        JsonAppendFmt(out, ",\"type\":\"%s\"", VoteTypeToken(evt.iProposalType));
        JsonAppendFmt(out, ",\"owner\":%d", evt.iOwner);
        if (evt.iSubject >= 0) JsonAppendFmt(out, ",\"subject\":%d", evt.iSubject);
        if (strcmp(evt.szKind, "vote") == 0)
            JsonAppendFmt(out, ",\"slot\":%d,\"yes\":%s",
                          evt.iVoter, evt.iVoteYes ? "true" : "false");
        if (strcmp(evt.szKind, "result") == 0)
            JsonAppendFmt(out, ",\"status\":\"%s\"", VoteStatusToken(evt.iStatus));
        out += "}";
    }
    out += "]";

    // v4: every city changing hands (ordered; capitals can flip multiple
    // times within one turn, which the lostCapital snapshot misses).
    out += ",\"cityCaptures\":[";
    for (size_t i = 0; i < s_captureEvents.size(); ++i)
    {
        const KekCityCaptureEvent& evt = s_captureEvents[i];
        if (i) out += ",";
        out += "{";
        JsonAppendFmt(out, "\"turn\":%d,\"x\":%d,\"y\":%d", evt.iTurn, evt.iX, evt.iY);
        out += ",\"name\":\"";
        JsonEscape(out, evt.szName);
        out += "\"";
        JsonAppendFmt(out, ",\"fromSlot\":%d,\"toSlot\":%d", evt.iFromSlot, evt.iToSlot);
        JsonAppendFmt(out, ",\"capital\":%s", evt.iCapital ? "true" : "false");
        if (evt.iOriginalCapitalOf >= 0)
            JsonAppendFmt(out, ",\"originalCapitalOf\":%d", evt.iOriginalCapitalOf);
        JsonAppendFmt(out, ",\"conquest\":%s,\"gift\":%s",
                      evt.iConquest ? "true" : "false", evt.iGift ? "true" : "false");
        out += "}";
    }
    out += "]";

#ifdef REPLAY_EVENTS
    // v8: CvGame's own Replay Events log (the "Events" tab / end-game stats
    // feature) sent as an incremental delta since the last upload, same idea
    // as ruins/votes/cityCaptures above -- but reading from CvGame's list
    // directly rather than a buffer of our own: that list has to survive for
    // the whole game (the Replay Viewer needs full history), so it's never
    // cleared, only read from a cursor. Capped per payload so a long catch-up
    // (e.g. after a promoted-uploader gap) can't blow past the server's
    // payload size limit; any remainder is picked up by the next turn's
    // upload since the cursor only advances as far as we actually serialized.
    //
    // NOTE: this reads THIS client's replay list, and only the uploader's
    // payload is ever sent. Most event types fire from synchronized game
    // state, so every client's list agrees on them -- but UI-driven types
    // (REPLAYEVENT_EnterCityScreen, REPLAYEVENT_OpenDemoScreen, ...) are
    // recorded only on the acting player's own client. Those events reach
    // GDR only when the acting player IS the current uploader; every other
    // player's UI events stay local to their machine and are never sent.
    out += ",\"replayEvents\":[";
    {
        uint uiTotal = kGame.getNumReplayEvents();
        size_t iStart = s_iNextReplayEventIndex;
        if (iStart > (size_t)uiTotal) iStart = uiTotal;   // new-game guard
        size_t iEnd = iStart + KEKMOD_MAX_EVENTS_PER_BUFFER;
        if (iEnd > (size_t)uiTotal) iEnd = uiTotal;
        bool bFirstEvent = true;
        for (size_t i = iStart; i < iEnd; ++i)
        {
            const CvReplayEvent* pEvent = kGame.getReplayEvent((uint)i);
            if (!pEvent)
                break;
            if (!bFirstEvent) out += ",";
            bFirstEvent = false;
            out += "{";
            JsonAppendFmt(out, "\"turn\":%d,\"ts\":%d,\"type\":\"%s\",\"player\":%d",
                          pEvent->m_iTurn, pEvent->m_iTimestamp,
                          ReplayEventTypeToken(pEvent->m_eEventType), (int)pEvent->m_ePlayer);
            out += ",\"args\":[";
            for (size_t j = 0; j < pEvent->m_vNumericArgs.size(); ++j)
            {
                if (j) out += ",";
                JsonAppendFmt(out, "%d", pEvent->m_vNumericArgs[j]);
            }
            out += "]";
            if (!pEvent->m_strStringData.IsEmpty())
            {
                out += ",\"str\":\"";
                JsonEscape(out, pEvent->m_strStringData.c_str());
                out += "\"";
            }
            out += "}";
        }
        s_iNextReplayEventIndex = iEnd;
    }
    out += "]";
#endif

    ClearEventBuffers();   // drained into this payload

    out += "}";
}


// ---------------------------------------------------------------------------
// Pending-upload queue (store-and-forward)
//
// The game thread builds a payload per turn and enqueues it on EVERY client;
// only the current uploader kicks the single background worker, which drains
// the queue in FIFO order. 2xx pops the entry, a permanent rejection (4xx,
// except 401/403 auth and 429 rate limiting, which are transient server-side
// conditions) drops it, and a network failure / 5xx / 401 / 403 / 429 stops
// the drain leaving the rest queued -- the next turn's hook re-kicks the
// worker. Game-end payloads are special: no future turn will heal them, so a
// drain still holding one retries with backoff before giving up.
//
// Save files are queued by PATH, not contents: autosaves persist on disk,
// and if Civ has rotated the file away by flush time the JSON (the crucial
// part) still goes out alone. The write time captured at enqueue guards the
// other rotation hazard -- a reused autosave NAME now holding a newer turn's
// bytes -- by skipping the save when the times no longer match.
// ---------------------------------------------------------------------------

struct PendingUpload
{
    int         iTurn;
    bool        bGameEnd;      // final flush -> backoff retries, see above
    std::string strJson;       // built on the game thread
    std::string strSteamId;    // builder (= sender when this client uploads)
    std::string strGameId;     // map GUID hex
    std::string strSavePath;   // "" = JSON-only payload
    std::string strSaveName;
    FILETIME    saveWriteTime; // as observed at enqueue; mismatch = rotated

    PendingUpload() : iTurn(0), bGameEnd(false)
    {
        saveWriteTime.dwLowDateTime  = 0;
        saveWriteTime.dwHighDateTime = 0;
    }
};

static std::deque<PendingUpload> s_pendingUploads;          // guarded by s_queueLock
static std::string               s_pendingGameId;           // guarded by s_queueLock
static CRITICAL_SECTION          s_queueLock;
static bool                      s_bQueueLockInit = false;  // init on game thread
static volatile LONG             s_lWorkerBusy    = 0;      // one drain at a time

// Backoff schedule for a drain that still holds a game-end payload.
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

// Sends one queued payload: save first (best-effort), then the turn JSON.
// The JSON is the crucial part -- its outcome decides the entry's fate.
// BACKGROUND THREAD ONLY.
static SendResult SendPendingUpload(HINTERNET hConnect, const PendingUpload& entry)
{
    // Shared headers (built narrow, converted once).
    char szCommon[1024];
    _snprintf_s(szCommon, sizeof(szCommon), _TRUNCATE,
                "X-Mod-Version: %s\r\n"
                "X-Turn-Number: %d\r\n"
                "X-Game-Id: %s\r\n"
                "X-Uploader-Steam-Id: %s\r\n"
                "%s%s%s",
                KEKMOD_MOD_VERSION, entry.iTurn,
                entry.strGameId.c_str(), entry.strSteamId.c_str(),
                KEKMOD_API_KEY[0] ? "X-Api-Key: " : "",
                KEKMOD_API_KEY[0] ? KEKMOD_API_KEY : "",
                KEKMOD_API_KEY[0] ? "\r\n" : "");

    wchar_t wszHeaders[2048];

    // -- 1. POST the save (skipped if the file has been rotated away) --------
    if (!entry.strSavePath.empty())
    {
        char* pFileBuf = NULL;
        DWORD dwRead   = 0;
        HANDLE hFile = CreateFileA(
            entry.strSavePath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,   // don't block Civ V if it reopens it
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            // Autosave rotation can REUSE a filename for a newer turn's bytes;
            // a stale queued entry must not upload them under its old turn
            // number. Skip the save unless the write time still matches.
            FILETIME ftNow = {0, 0};
            if (GetFileTime(hFile, NULL, NULL, &ftNow) &&
                CompareFileTime(&ftNow, &entry.saveWriteTime) != 0)
            {
                WriteLog("[kekmod_http] save '%s' rewritten since turn %d was"
                         " queued -- sending turn JSON only",
                         entry.strSaveName.c_str(), entry.iTurn);
            }
            else
            {
                LARGE_INTEGER liSize;
                ZeroMemory(&liSize, sizeof(liSize));
                GetFileSizeEx(hFile, &liSize);
                DWORD dwFileSize = (DWORD)liSize.QuadPart;
                pFileBuf = new char[dwFileSize];
                if (!ReadFile(hFile, pFileBuf, dwFileSize, &dwRead, NULL))
                    dwRead = 0;
            }
            CloseHandle(hFile);
        }
        else
        {
            WriteLog("[kekmod_http] save '%s' no longer readable (error=%u) --"
                     " sending turn %d JSON only",
                     entry.strSavePath.c_str(), GetLastError(), entry.iTurn);
        }

        if (pFileBuf && dwRead > 0)
        {
            swprintf_s(wszHeaders, _countof(wszHeaders),
                       L"Content-Type: application/octet-stream\r\n"
                       L"X-Save-Filename: %hs\r\n%hs",
                       entry.strSaveName.c_str(), szCommon);
            DWORD dwStatus = 0;
            bool bSent = HttpPost(hConnect, KEKMOD_SAVE_PATH, wszHeaders,
                                  pFileBuf, dwRead, &dwStatus);
            delete[] pFileBuf;

            if (!bSent || dwStatus >= 500)
            {
                WriteLog("[kekmod_http] save POST turn %d failed (%s=%u); kept queued",
                         entry.iTurn, bSent ? "status" : "WinHTTP error",
                         bSent ? dwStatus : GetLastError());
                return SEND_RETRY;
            }
            if (dwStatus == 401 || dwStatus == 403)
            {
                // Auth rejection is a config problem (key mismatch with the
                // server), not a bad payload: keep everything queued so the
                // data survives until the server side is fixed.
                WriteLog("[kekmod_http] save POST turn %d auth-rejected (status=%u);"
                         " check KEKMOD_API_KEY vs the server's kek.api-key --"
                         " kept queued", entry.iTurn, dwStatus);
                return SEND_RETRY;
            }
            if (dwStatus == 429)
            {
                // Rate-limited at the edge (a backlog flush can outrun the
                // Cloudflare per-IP limit). The payload is fine -- keep it
                // queued and let the next turn's kick resume the drain.
                WriteLog("[kekmod_http] save POST turn %d rate-limited (status=429);"
                         " kept queued", entry.iTurn);
                return SEND_RETRY;
            }
            if (dwStatus >= 400)
                WriteLog("[kekmod_http] save POST turn %d rejected (status=%u);"
                         " dropping the save, keeping the JSON", entry.iTurn, dwStatus);
            else
                WriteLog("[kekmod_http] save POST '%s' (%u bytes) -> status=%u",
                         entry.strSaveName.c_str(), dwRead, dwStatus);
        }
        else
        {
            delete[] pFileBuf;
        }
    }

    // -- 2. POST the JSON digest ----------------------------------------------
    if (!entry.strJson.empty())
    {
        swprintf_s(wszHeaders, _countof(wszHeaders),
                   L"Content-Type: application/json\r\n%hs", szCommon);
        DWORD dwStatus = 0;
        bool bSent = HttpPost(hConnect, KEKMOD_TURNS_PATH, wszHeaders,
                              entry.strJson.c_str(), (DWORD)entry.strJson.size(),
                              &dwStatus);
        if (!bSent || dwStatus >= 500)
        {
            WriteLog("[kekmod_http] turn JSON POST turn %d failed (%s=%u); kept queued",
                     entry.iTurn, bSent ? "status" : "WinHTTP error",
                     bSent ? dwStatus : GetLastError());
            return SEND_RETRY;
        }
        if (dwStatus == 401 || dwStatus == 403)
        {
            // Config problem, not a bad payload -- never drop data over it.
            WriteLog("[kekmod_http] turn JSON turn %d auth-rejected (status=%u);"
                     " check KEKMOD_API_KEY vs the server's kek.api-key --"
                     " kept queued", entry.iTurn, dwStatus);
            return SEND_RETRY;
        }
        if (dwStatus == 429)
        {
            // Rate-limited at the edge -- same handling as the save POST.
            WriteLog("[kekmod_http] turn JSON turn %d rate-limited (status=429);"
                     " kept queued", entry.iTurn);
            return SEND_RETRY;
        }
        if (dwStatus >= 400)
        {
            WriteLog("[kekmod_http] turn JSON turn %d rejected (status=%u); dropped"
                     " permanently", entry.iTurn, dwStatus);
            return SEND_DROP;
        }
        WriteLog("[kekmod_http] turn JSON turn %d (%u bytes) -> status=%u",
                 entry.iTurn, (unsigned)entry.strJson.size(), dwStatus);
    }
    return SEND_OK;
}

// Drains the pending queue FIFO. Runs until the queue is empty (success) or
// a send fails (leaves the remainder queued for the next turn's kick). While
// a game-end payload is anywhere in the queue, failures retry in-thread on
// the backoff schedule first -- no later turn will ever heal those.
static DWORD WINAPI HttpQueueWorkerProc(LPVOID)
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
            PendingUpload entry;
            bool bHaveEntry       = false;
            bool bQueueHasGameEnd = false;
            EnterCriticalSection(&s_queueLock);
            if (!s_pendingUploads.empty())
            {
                entry      = s_pendingUploads.front();
                bHaveEntry = true;
                for (size_t i = 0; i < s_pendingUploads.size(); ++i)
                {
                    if (s_pendingUploads[i].bGameEnd)
                    {
                        bQueueHasGameEnd = true;
                        break;
                    }
                }
            }
            LeaveCriticalSection(&s_queueLock);
            if (!bHaveEntry)
                break;

            SendResult eResult = hConnect ? SendPendingUpload(hConnect, entry)
                                          : SEND_RETRY;

            if (eResult == SEND_RETRY)
            {
                if (bQueueHasGameEnd && iBackoffStep < (int)_countof(s_adwGameEndBackoffMs))
                {
                    WriteLog("[kekmod_http] game-end payload pending; retrying drain"
                             " in %us (attempt %d/%d)",
                             s_adwGameEndBackoffMs[iBackoffStep] / 1000,
                             iBackoffStep + 1, (int)_countof(s_adwGameEndBackoffMs));
                    Sleep(s_adwGameEndBackoffMs[iBackoffStep]);
                    iBackoffStep++;
                    // Handles may be stale after the wait: reconnect from scratch.
                    if (hConnect) { WinHttpCloseHandle(hConnect); hConnect = NULL; }
                    if (hSession) { WinHttpCloseHandle(hSession); hSession = NULL; }
                    OpenConnection(&hSession, &hConnect);
                    continue;
                }

                size_t nPending = 0;
                EnterCriticalSection(&s_queueLock);
                nPending = s_pendingUploads.size();
                LeaveCriticalSection(&s_queueLock);
                WriteLog("[kekmod_http] drain stopped at turn %d; %u payload(s) queued"
                         " -- retrying on the next turn's upload",
                         entry.iTurn, (unsigned)nPending);
                bDrainStopped = true;
                break;
            }

            // SEND_OK or SEND_DROP: this entry is finished either way. Pop
            // only if the front is still the entry we sent -- the game thread
            // clears the queue on a game change, and blindly popping would
            // eat the new game's first payload.
            EnterCriticalSection(&s_queueLock);
            if (!s_pendingUploads.empty() &&
                s_pendingUploads.front().iTurn == entry.iTurn &&
                s_pendingUploads.front().strGameId == entry.strGameId)
            {
                s_pendingUploads.pop_front();
            }
            LeaveCriticalSection(&s_queueLock);
            iBackoffStep = 0;
        }

        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);

        InterlockedExchange(&s_lWorkerBusy, 0);
        if (bDrainStopped)
            return 0;   // deliberate stop: the next enqueue re-kicks the worker

        // An entry enqueued while we were shutting down would otherwise wait a
        // full turn: if one is there and the flag is still free, claim it and
        // go around again.
        bool bMore = false;
        EnterCriticalSection(&s_queueLock);
        bMore = !s_pendingUploads.empty();
        LeaveCriticalSection(&s_queueLock);
        if (!bMore || InterlockedCompareExchange(&s_lWorkerBusy, 1, 0) != 0)
            return 0;
    }
}

// Queues one payload; kicks the worker (if idle) only when this client is
// the current uploader -- standby clients just accumulate. GAME THREAD ONLY.
static void EnqueueUpload(const PendingUpload& entry, bool bKickWorker)
{
    if (!s_bQueueLockInit)
    {
        InitializeCriticalSection(&s_queueLock);
        s_bQueueLockInit = true;
    }

    // The statics outlive a game when another save is loaded in the same
    // session: a stale buffer must never be flushed into the next game.
    size_t nStale   = 0;
    size_t nPending = 0;
    EnterCriticalSection(&s_queueLock);
    if (s_pendingGameId != entry.strGameId)
    {
        nStale = s_pendingUploads.size();
        s_pendingUploads.clear();
        s_pendingGameId = entry.strGameId;
    }
    s_pendingUploads.push_back(entry);
    nPending = s_pendingUploads.size();
    LeaveCriticalSection(&s_queueLock);

    if (nStale > 0)
        WriteLog("[kekmod_http] game changed -- discarded %u buffered payload(s)"
                 " from the previous game", (unsigned)nStale);

    if (entry.strJson.size() > KEKMOD_JSON_WARN_BYTES)
        WriteLog("[kekmod_http] WARNING: turn %d JSON is %u bytes -- approaching"
                 " the server's %u-byte cap; payloads over the cap are rejected"
                 " and dropped permanently",
                 entry.iTurn, (unsigned)entry.strJson.size(),
                 (unsigned)KEKMOD_JSON_MAX_BYTES);

    if (nPending > 1)
        WriteLog("[kekmod_http] queued turn %d (%u payloads pending%s)",
                 entry.iTurn, (unsigned)nPending,
                 bKickWorker ? "" : "; standby -- not the uploader");

    if (!bKickWorker)
        return;

    if (InterlockedCompareExchange(&s_lWorkerBusy, 1, 0) == 0)
    {
        HANDLE hThread = CreateThread(NULL, 0, HttpQueueWorkerProc, NULL, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
        else
        {
            InterlockedExchange(&s_lWorkerBusy, 0);
            WriteLog("[kekmod_http] CreateThread failed: error=%u -- payload stays"
                     " queued for the next turn", GetLastError());
        }
    }
}


// ---------------------------------------------------------------------------
// Public API -- called from the CvGame.cpp end-of-turn auto-save hook
// (GAME THREAD).
// ---------------------------------------------------------------------------

// Single-uploader rule: the hooks fire on every client in lockstep; every
// client BUILDS and BUFFERS the payload, but only the client whose LOCAL
// player is the first alive human drains the queue. The election is
// deterministic from synchronized game state, so exactly one client sends
// -- and when the uploader quits (slot flips to AI), every client agrees on
// the successor at the next hook without any coordination.
static bool LocalPlayerIsUploader(PlayerTypes eActive)
{
    for (int iJ = 0; iJ < MAX_PLAYERS; iJ++)
    {
        CvPlayer& kItPlayer = GET_PLAYER((PlayerTypes)iJ);
        if (kItPlayer.isAlive() && kItPlayer.isHuman())
            return eActive == (PlayerTypes)iJ;
    }
    return false;
}

void CvHttp_OnTurnAutoSave()
{
    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;
    bool bLocalIsUploader = LocalPlayerIsUploader(eActive);

    // Dedup: the hook can fire more than once per auto-save; buffer exactly
    // one payload per (game, turn).
    char szGuid[40];
    FormatMapGuid(szGuid, sizeof(szGuid));
    int iTurn = GC.getGame().getGameTurn();
    static std::string s_strLastGameId;
    static int         s_iLastTurn = -1;
    if (s_iLastTurn == iTurn && s_strLastGameId == szGuid)
        return;
    s_strLastGameId = szGuid;
    s_iLastTurn     = iTurn;

    PendingUpload entry;
    entry.iTurn     = iTurn;
    entry.strGameId = szGuid;

    // Attach the fresh autosave; if the newest file+time is what we already
    // attached (or autosaves are off on this client), the JSON goes alone.
    static char     s_szLastPath[MAX_PATH] = {0};
    static FILETIME s_lastWriteTime        = {0, 0};
    char     szPath[MAX_PATH] = {0};
    FILETIME writeTime        = {0, 0};
    if (FindMostRecentSave(szPath, sizeof(szPath), &writeTime))
    {
        if (strcmp(szPath, s_szLastPath) != 0 ||
            CompareFileTime(&writeTime, &s_lastWriteTime) != 0)
        {
            strncpy_s(s_szLastPath, sizeof(s_szLastPath), szPath, _TRUNCATE);
            s_lastWriteTime = writeTime;
            entry.strSavePath   = szPath;
            entry.saveWriteTime = writeTime;
            const char* pszBase = strrchr(szPath, '\\');
            entry.strSaveName   = pszBase ? pszBase + 1 : szPath;
        }
    }
    else
    {
        WriteLog("[kekmod_http] no .Civ5Save found in %s -- sending turn %d"
                 " JSON only", KEKMOD_SAVES_SUBPATH, iTurn);
    }

    // Game-state reads happen HERE, on the game thread. Enqueuing is the
    // hand-off point: the event buffers drained into this JSON now live in
    // the queue until the server acknowledges them (or, on a standby client,
    // until a promotion flushes them).
    BuildTurnJson(entry.strJson, eActive);
    entry.strSteamId = SteamIdFromNickname(CvPreGame::nickname(eActive));

    EnqueueUpload(entry, bLocalIsUploader);
}

void CvHttp_OnGameEnd()
{
    // setWinner runs on every client; every client buffers the final payload,
    // only the uploader flushes it (with backoff -- no later turn heals a
    // game-end payload).
    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;
    bool bLocalIsUploader = LocalPlayerIsUploader(eActive);

    PendingUpload entry;
    entry.bGameEnd = true;   // no later turn heals this -> backoff retries
    entry.iTurn = GC.getGame().getGameTurn();

    // Game-state reads happen HERE, on the game thread. The winner block and
    // any buffered vote-result events ride along in this payload; a repeat
    // flush is healed by server-side upsert idempotency.
    BuildTurnJson(entry.strJson, eActive);
    entry.strSteamId = SteamIdFromNickname(CvPreGame::nickname(eActive));
    {
        char szGuid[40];
        FormatMapGuid(szGuid, sizeof(szGuid));
        entry.strGameId = szGuid;
    }

    if (bLocalIsUploader)
        WriteLog("[kekmod_http] game end -- flushing final turn JSON (turn %d)",
                 entry.iTurn);
    EnqueueUpload(entry, bLocalIsUploader);
}

void CvHttp_OnProposalResolved()
{
    // Same rationale as CvHttp_OnGameEnd: a resolved proposal (IRR kicking a
    // leaver, in particular) may be the last thing that happens in a session
    // that never reaches another end-of-turn autosave, so flush now rather
    // than leaving the buffered "result" event (and anything else queued)
    // waiting on a turn boundary that might not come.
    PlayerTypes eActive = GC.getGame().getActivePlayer();
    if (eActive == NO_PLAYER)
        return;
    bool bLocalIsUploader = LocalPlayerIsUploader(eActive);

    PendingUpload entry;
    entry.iTurn = GC.getGame().getGameTurn();

    BuildTurnJson(entry.strJson, eActive);
    entry.strSteamId = SteamIdFromNickname(CvPreGame::nickname(eActive));
    {
        char szGuid[40];
        FormatMapGuid(szGuid, sizeof(szGuid));
        entry.strGameId = szGuid;
    }

    EnqueueUpload(entry, bLocalIsUploader);
}

#else // !_WIN32

// ---------------------------------------------------------------------------
// Non-Windows stub -- WinHTTP is not available; calls silently do nothing.
// ---------------------------------------------------------------------------
void CvHttp_OnTurnAutoSave() {}
void CvHttp_OnGameEnd() {}
void CvHttp_OnProposalResolved() {}
void CvHttp_RecordRuinEvent(const KekRuinEvent&) {}
void CvHttp_RecordVoteEvent(const KekVoteEvent&) {}
void CvHttp_RecordCityCaptureEvent(const KekCityCaptureEvent&) {}

#endif // _WIN32
