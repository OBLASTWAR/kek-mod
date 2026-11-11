#pragma once
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvHttpUtils.h
//! \brief   Outbound HTTP for the GDR bot: turn-end save upload, crash and
//!          desync reports.
//!
//! Network I/O is dispatched on a background thread so the game loop is never
//! blocked; game state is only read on the game thread. Results are logged to
//! kekmod_http.log via FILogFile. No-op on non-Windows (WinHTTP is
//! Windows-only).
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#include <string.h>
#include <string>

// Mod version string, e.g. "2.0-beta1". The KEKMOD_MOD_VERSION define lives
// in CvHttpUtils.cpp and must stay there (the release scripts sed it by
// filename); consumers outside that file use this accessor.
const char* CvHttp_GetModVersion();

// Second half of the game key (X-Game-Key): a hash of both random seeds, the
// map size and the map script. The first half is the map random seed.
unsigned CvHttp_GameKeyHash();

// Call from the CvGame.cpp end-of-turn auto-save hook (game thread).
// Every client spools and uploads its own copy of the fresh autosave (or
// reports why it has none). Network I/O runs on background threads.
void CvHttp_OnTurnAutoSave();

// One popped ancient ruin (goody hut) with the ACTUAL resolved benefit --
// filled in by CvPlayer::receiveGoody as each reward branch runs, then
// logged as REPLAYEVENT_GoodyHut's args (KEK_SAVE_DATA), which the save
// carries. Zero/empty fields mean "not this benefit".
struct KekRuinEvent
{
    int  iSlot;                // receiving player
    int  iX, iY;               // hut plot
    char szGoody[64];          // GoodyHuts type token (GOODY_GOLD, ...)
    int  iGold;                // gold after the random roll
    int  iCulture;             // culture after game-speed scaling
    int  iFaith;               // faith (flat / pantheon / prophet-percent)
    int  iPopulation;          // pop added to nearest city
    int  iScience;             // FLAT_SCIENCE_FROM_TECH_RUIN beakers
    int  iExperience;          // unit XP granted
    int  iHeal;                // unit damage healed
    int  iBarbCount;           // hostile ruin: barbarians spawned
    int  iMapReveal;           // 1 if a map area was revealed
    char szTech[64];           // tech granted (non-flat tech ruin)
    char szUnit[64];           // unit spawned (e.g. settler/worker hut)
    char szUpgradeUnit[64];    // unit the popping unit upgraded into
    char szResource[64];       // resource force-revealed near capital

    KekRuinEvent() { memset(this, 0, sizeof(*this)); }
};

// One city changing hands, filled in CvPlayer::acquireCity and logged as
// REPLAYEVENT_CityAcquired's args (KEK_SAVE_DATA), which the save carries.
// Events -- not snapshots -- because capitals commonly flip several times
// within one turn under simultaneous turns.
struct KekCityCaptureEvent
{
    int  iX, iY;
    char szName[64];
    int  iFromSlot, iToSlot;
    int  iCapital;           // 1 if this was the loser's CURRENT capital
    int  iOriginalCapitalOf; // slot whose founding capital sits here, -1 none
    int  iConquest, iGift;   // acquireCity flags (neither = trade/other)

    KekCityCaptureEvent() { memset(this, 0, sizeof(*this)); iOriginalCapitalOf = -1; }
};

// POSTs one crash-report dump (Phase 3, plan/CRASH_REPORTER_PLAN.md; see
// CvCrashReporter.cpp). Reads pszDumpPath's raw bytes as the body;
// pszMetaJson (the sidecar JSON, single line, no trailing newline) goes in
// the X-Crash-Meta header. Returns true only on a 2xx response;
// *pdwStatusOut is set either way for logging. BACKGROUND THREAD ONLY
// (blocking WinHTTP calls) -- never call from the game thread.
bool CvHttp_PostCrashDump(const char* pszDumpPath, const char* pszKind,
                          const char* pszMetaJson, DWORD* pdwStatusOut);

// Call from CvGame::setWinner (game thread). Game end never gets another
// end-of-turn autosave, so the deciding vote result / final state would be
// lost; this autosaves and uploads the final save from every client.
void CvHttp_OnGameEnd();

// Call from CvMPVotingSystem::SetProposalCompletion (game thread) whenever a
// proposal (IRR/CC/SCRAP) resolves. With bSave (a passed IRR) it marks a save
// as due: CvHttp_OnUpdate takes it, since an IRR kick can be the last thing
// that happens in a session that never reaches another end-of-turn autosave.
// A passed CC or Scrap ends the game, which saves.
void CvHttp_OnProposalResolved(bool bSave);

// Call from the top of CvGame::update (game thread). After a passed IRR
// it autosaves and uploads the save from every client: the save holds the
// result even if no end-of-turn autosave follows. Deferred to here because the
// proposal is marked complete only after CvHttp_OnProposalResolved returns, and
// a proposal can resolve inside turn processing, where a save is not taken.
void CvHttp_OnUpdate();

// Posts one desync report (KEK_SYNC_FINGERPRINT) built by CvSyncFingerprint.cpp
// to /api/desyncs. strReportJson is a complete JSON object; the game id, mod
// version and both players' Steam ids are added here. Every client sends its
// own report -- no single-uploader rule. Game thread; the POST runs on a
// background thread with a short retry.
void CvHttp_PostDesyncReport(int iTurn, int iSlice, PlayerTypes eLocal, PlayerTypes eRemote,
                             const std::string& strReportJson);
