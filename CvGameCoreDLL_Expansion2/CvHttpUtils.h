#pragma once
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvHttpUtils.h
//! \brief   Outbound HTTP for kek-mod 1.5: turn-end save upload + compact
//!          JSON telemetry for the GDR bot.
//!
//! Network I/O is dispatched on a background thread so the game loop is never
//! blocked; game state is only read on the game thread. Results are logged to
//! kekmod_http.log via FILogFile. No-op on non-Windows (WinHTTP is
//! Windows-only).
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#include <string.h>

// Call from the CvGame.cpp end-of-turn auto-save hook (game thread).
// Applies the single-uploader rule (local player == first alive human),
// dedups against the last uploaded save, builds the schema-v1 turn JSON,
// then POSTs save + JSON on a background thread.
void CvHttp_OnTurnAutoSave();

// One popped ancient ruin (goody hut) with the ACTUAL resolved benefit --
// filled in by CvPlayer::receiveGoody as each reward branch runs, then handed
// to CvHttp_RecordRuinEvent. Zero/empty fields mean "not this benefit".
struct KekRuinEvent
{
    int  iTurn;                // set by CvHttp_RecordRuinEvent
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

// Buffer a ruin event on the game thread; drained into the next turn JSON
// ("ruins" array, schema 3) by the auto-save hook. Self-resets when a
// different game (map GUID) is loaded. No-op on non-Windows.
void CvHttp_RecordRuinEvent(const KekRuinEvent& evt);

// One kek MP voting-system event (schema 4): proposal created, vote cast,
// or proposal resolved. Filled by the CvMPVotingSystem hooks.
struct KekVoteEvent
{
    int  iTurn;          // set by CvHttp_RecordVoteEvent
    char szKind[12];     // "proposal" | "vote" | "result"
    int  iProposalId;    // CvMPVotingSystem proposal id (stable per game)
    int  iProposalType;  // MPVotingSystemProposalTypes: 0 irr, 1 cc, 2 scrap
    int  iOwner;         // proposing slot
    int  iSubject;       // cc: proposed winner; irr: the leaver; -1 none
    int  iVoter;         // kind=vote only, else -1
    int  iVoteYes;       // kind=vote: 1 yes / 0 no
    int  iStatus;        // kind=result: -1 invalid, 1 passed, 2 failed

    KekVoteEvent() { memset(this, 0, sizeof(*this)); iVoter = -1; iSubject = -1; }
};

// One city changing hands (schema 4), recorded in CvPlayer::acquireCity.
// Events -- not snapshots -- because capitals commonly flip several times
// within one turn under simultaneous turns.
struct KekCityCaptureEvent
{
    int  iTurn;              // set by CvHttp_RecordCityCaptureEvent
    int  iX, iY;
    char szName[64];
    int  iFromSlot, iToSlot;
    int  iCapital;           // 1 if this was the loser's CURRENT capital
    int  iOriginalCapitalOf; // slot whose founding capital sits here, -1 none
    int  iConquest, iGift;   // acquireCity flags (neither = trade/other)

    KekCityCaptureEvent() { memset(this, 0, sizeof(*this)); iOriginalCapitalOf = -1; }
};

// Same buffering contract as CvHttp_RecordRuinEvent ("votes" and
// "cityCaptures" arrays, schema 4).
void CvHttp_RecordVoteEvent(const KekVoteEvent& evt);
void CvHttp_RecordCityCaptureEvent(const KekCityCaptureEvent& evt);

// Call from CvGame::setWinner (game thread). Game end never gets another
// end-of-turn autosave, so the deciding vote result / final state would be
// lost; this builds and POSTs the turn JSON immediately (JSON only -- no
// save accompanies it). Applies the same single-uploader gate as the
// auto-save hook.
void CvHttp_OnGameEnd();

// Call from CvMPVotingSystem::SetProposalCompletion (game thread) whenever a
// proposal (IRR/CC/SCRAP) resolves. A resolved proposal doesn't necessarily
// imply another end-of-turn autosave will ever happen in that session (e.g.
// an IRR kick can end the game for the remaining human without tripping a
// victory condition), so the buffered "result" event -- and anything else
// queued -- would otherwise sit unsent until a turn boundary that may never
// come. Builds and POSTs the turn JSON immediately (JSON only -- no save
// accompanies it), same single-uploader gate as the other hooks.
void CvHttp_OnProposalResolved();
