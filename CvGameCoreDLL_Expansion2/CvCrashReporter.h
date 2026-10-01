#pragma once
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvCrashReporter.h
//! \brief   Process-wide unhandled-exception filter + minidump writer, plus
//!          a hang watchdog (Phases 1-2 of plan/CRASH_REPORTER_PLAN.md; port
//!          of the Community Patch implementation, gated by NQM_MINIDUMPS).
//!
//! On any unhandled exception anywhere in the process -- base exe, Firaxis
//! DLLs, drivers, not just this DLL -- writes a minidump plus a small JSON
//! metadata sidecar to
//!   Documents\My Games\Sid Meier's Civilization 5\kekmod\crashlogs\
//! then shows a dialog pointing the player at the report files. A background
//! watchdog thread additionally detects a stalled game thread (deadlocks
//! raise no exception, so the filter alone can never catch them) and writes
//! the same kind of dump for the live process. The dumps are hand-reportable
//! today and become the payload for the automatic upload in Phase 3. No-op
//! on non-Windows or without NQM_MINIDUMPS.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Call once from CvGlobals::init() (game thread). Late-boot install is
// deliberate: the LAST caller of SetUnhandledExceptionFilter wins, and by
// init() time Steam and the game have already registered theirs. Also starts
// the hang watchdog thread.
void KekCrashReporter_Install();

// Call once from CvGameCoreDLL.cpp's DllMain (DLL_PROCESS_DETACH), before
// singletons are torn down: signals the watchdog thread to stop and waits
// (bounded, a few seconds) for it to exit cleanly. No-op if never installed.
void KekCrashReporter_Shutdown();

// Call from the top of CvGame::update() (game thread), every tick. One
// volatile write -- zero sync cost, zero desync surface. The watchdog thread
// polls the age of this heartbeat to detect a stalled game thread. Also
// moves the phase to KEK_PHASE_INGAME (unless a turn is mid-process).
void KekCrashReporter_Heartbeat();

// What the game was doing, stamped into every report ("phase" + "phaseMs"
// in the sidecar, so the server can tell a lobby crash from a mid-game one
// -- turn/numHumans alone can't: both read 0 in the lobby AND while loading).
// The hang watchdog only runs in INGAME/TURN: CvGame::update() doesn't tick
// in menus or while loading, so a stale heartbeat there is not a hang.
enum KekCrashPhase
{
	KEK_PHASE_FRONTEND = 0,	// no game: boot, menus, lobby, after exit to menu
	KEK_PHASE_LOADING,		// CvGame::init / CvGame::Read until the first update tick
	KEK_PHASE_INGAME,		// CvGame::update() ticking
	KEK_PHASE_TURN,			// inside CvGame::doTurn() (turn rollover)
	KEK_PHASE_SHUTDOWN		// CvGlobals::uninit -- DLL being torn down
};

// Game thread only. Cheap (two volatile writes); safe to call every frame.
void KekCrashReporter_SetPhase(KekCrashPhase ePhase);

// DEV BUILDS ONLY (KEKMOD_BUILD_DEV; compiled to a no-op in prod).
// Polled from CvGame::update(): if crashlogs\crashtest.txt exists, the
// file is deleted (so the next launch is clean) and the process crashes or
// hangs deliberately -- file content "thread" faults on a spawned thread
// (proves the filter is process-wide), "hang" sleeps the game thread past
// the watchdog threshold, anything else faults on the game thread.
// Acceptance test for the filter + watchdog + dump + sidecar pipeline.
void KekCrashReporter_CheckTestTrigger();

// Pending-report submission (Phase 3, plan/CRASH_REPORTER_PLAN.md). Scanned
// once from KekCrashReporter_Install(), which also shows a native Yes/No
// dialog right there (blocking, before any menu Lua exists -- Game is not
// Lua-bound at the bare FrontEnd screen, so this can't be a themed in-game
// popup) and acts on the player's choice. No public API beyond Install();
// this is entirely internal to CvCrashReporter.cpp.
