#pragma once
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvKekPlayerStats.h
//! \brief   Extra per-player stats written into the save (KEK_PLAYER_STATS).
//!
//! Two kinds, both stored as per-turn replay datasets (value = that turn's
//! count, not a running total) and kept out of the replay viewer:
//!
//! LOCAL ONLY -- each client counts its own player and writes only that
//! player's datasets, right before each KEK save (recordKekReplayData). No
//! network message, so another client's save holds nothing for this player:
//! read player X's local stats from player X's own uploaded save. Per-turn
//! values survive a reconnect or reload (both replace the local history with
//! the host's copy); sum them across that player's saves.
//!   REPLAYDATASET_KEK_CLICKS            mouse button presses in the game window
//!   REPLAYDATASET_KEK_ACTIVETURNMS      ms with the own turn active and not ended
//!   REPLAYDATASET_KEK_DEMOGRAPHICSOPENS Demographics screen opens
//!   REPLAYDATASET_KEK_CITYSCREENOPENS   city screen opens
//!
//! SYNCED -- counted where the timer command is applied on every client, so
//! every save agrees.
//!   REPLAYDATASET_KEK_TIMERRESETS       turn timer resets by this player
//!   REPLAYDATASET_KEK_TIMERPAUSES       turn timer pauses by this player (not unpauses)
//!
//! Replay data is not part of the sync archive or the sync checksum, so the
//! local values differing between clients cannot cause a desync. None of this
//! draws random numbers or changes game state.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// CvGame::reset (new game, load, rejoin): drop anything still pending.
void KekStats_Reset();

// Top of CvGame::update, every frame: runs the active-turn clock and installs
// the click hook once the game window is known. GAME THREAD ONLY.
void KekStats_Update();

// From the UI (Lua), local only.
void KekStats_OnDemographicsOpened();
void KekStats_OnCityScreenEntered();

// Inside CvGame::recordKekReplayData: adds everything pending since the last
// call to the local player's datasets for iTurn (zero counts are written too,
// so an entry marks a turn this client recorded). GAME THREAD ONLY.
void KekStats_RecordLocal(int iTurn);

// From ResponseGiftUnit (-1 reset, -7 pause), which runs on every client.
void KekStats_OnTimerReset(PlayerTypes ePlayer);
void KekStats_OnTimerPaused(PlayerTypes ePlayer);

// DllMain DLL_PROCESS_DETACH: removes the click hook before the code it
// points into is unloaded.
void KekStats_Shutdown();
