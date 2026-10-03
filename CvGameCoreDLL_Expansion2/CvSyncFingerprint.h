#pragma once
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvSyncFingerprint.h
//! \brief   Live multiplayer desync detection (KEK_SYNC_FINGERPRINT).
//!
//! Every KEK_SYNC_SLICE_INTERVAL turn slices each client hashes its synced
//! game state into four short fingerprints (game, players, units, cities) and
//! keeps the last few snapshots, raw values included. The in-game UI script
//! (tmp/ui/KekSyncCheck.lua) broadcasts the newest fingerprint over hidden
//! chat and hands every fingerprint it receives back to KekSync_Compare. The
//! first mismatch with a given player writes the matching snapshot to
//! kek_desync.log and uploads it (CvHttp_PostDesyncReport), so the server
//! gets both sides' view of the same slice.
//!
//! Everything here only READS game state, draws no random numbers and sends
//! no game messages, so it cannot cause a desync itself.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Call from CvGame::update right after the turn slice advances (game thread).
void KekSync_OnSliceEnd();

// Newest fingerprint: false until the first snapshot of this game exists.
// strHashes is 32 hex characters (four 8-character area hashes).
bool KekSync_GetLatest(int& iTurn, int& iSlice, CvString& strHashes);

enum KekSyncCompareResult
{
	KEKSYNC_UNKNOWN          = 0, // slice not (or no longer) in our history
	KEKSYNC_MATCH            = 1,
	KEKSYNC_MISMATCH_NEW     = 2, // first mismatch with this player: reported
	KEKSYNC_MISMATCH_ONGOING = 3, // already reported, still out of sync
};

// Compares a fingerprint received from eRemote for iSlice with our own.
KekSyncCompareResult KekSync_Compare(PlayerTypes eRemote, int iTurn, int iSlice, const char* szRemoteHashes);
