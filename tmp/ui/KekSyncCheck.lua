-------------------------------------------------
-- KekSyncCheck.lua -- live desync detection (KEK_SYNC_FINGERPRINT, see
-- CvSyncFingerprint.h in the DLL).
--
-- Broadcasts this client's newest state fingerprint to the other players over
-- hidden chat and hands every fingerprint received back to the DLL, which
-- compares it with its own snapshot of the same turn slice. On the first
-- mismatch with a player the DLL writes kek_desync.log and posts a report;
-- this script only shows the player a one-line alert.
--
-- Loaded from InGame.lua. The messages start with SYNC_MARKER and are dropped
-- before they reach the chat panel in DiploCorner.lua (both the vanilla and
-- EUI versions) -- keep the marker in sync there.
-------------------------------------------------

local SYNC_MARKER = "~~KS|";

-- A DLL built without KEK_SYNC_FINGERPRINT has no bindings: stay inert.
if Game.KekSyncGetLatest == nil or Game.KekSyncCompare == nil then
	return;
end

local KEKSYNC_UNKNOWN      = 0;
local KEKSYNC_MISMATCH_NEW = 2;

local g_iLastSentSlice = -1;

-- A peer can be a slice or two ahead, so its fingerprint may arrive before we
-- have our own snapshot of that slice. Hold the newest one per player and
-- retry once we catch up.
local g_tPending = {};

function CompareFingerprint(fromPlayer, iTurn, iSlice, strHashes)
	local iResult = Game.KekSyncCompare(fromPlayer, iTurn, iSlice, strHashes);
	if iResult == KEKSYNC_MISMATCH_NEW then
		local pPlayer = Players[fromPlayer];
		local strName = (pPlayer ~= nil) and pPlayer:GetNickName() or tostring(fromPlayer);
		Events.GameplayAlertMessage("Desync detected with " .. strName .. " (turn " .. iTurn .. ").");
	end
	return iResult;
end

-------------------------------------------------
-- Send each new fingerprint once, and retry held fingerprints we have now
-- caught up with. The DLL only produces fingerprints in networked
-- multiplayer, so this does nothing in single player.
-------------------------------------------------
function OnKekSyncUpdate()
	local iTurn, iSlice, strHashes = Game.KekSyncGetLatest();
	if iSlice == nil or iSlice == g_iLastSentSlice then
		return;
	end
	g_iLastSentSlice = iSlice;
	Network.SendChat(SYNC_MARKER .. iTurn .. "|" .. iSlice .. "|" .. strHashes, -1, -1);

	for fromPlayer, tPending in pairs(g_tPending) do
		if tPending.Slice <= iSlice then
			g_tPending[fromPlayer] = nil;
			CompareFingerprint(fromPlayer, tPending.Turn, tPending.Slice, tPending.Hashes);
		end
	end
end
Events.LocalMachineAppUpdate.Add(OnKekSyncUpdate);

-------------------------------------------------
-- Check every fingerprint another player sends.
-------------------------------------------------
function OnKekSyncChat(fromPlayer, toPlayer, text, eTargetType)
	if type(text) ~= "string" or text:sub(1, #SYNC_MARKER) ~= SYNC_MARKER then
		return;
	end
	if fromPlayer == Game.GetActivePlayer() then
		return;
	end

	local strTurn, strSlice, strHashes = text:match("^~~KS|(%d+)|(%d+)|(%x+)$");
	if strHashes == nil then
		return;
	end
	local iTurn = tonumber(strTurn);
	local iSlice = tonumber(strSlice);

	if CompareFingerprint(fromPlayer, iTurn, iSlice, strHashes) == KEKSYNC_UNKNOWN then
		local _, iLocalSlice = Game.KekSyncGetLatest();
		if iLocalSlice == nil or iSlice > iLocalSlice then
			g_tPending[fromPlayer] = { Turn = iTurn, Slice = iSlice, Hashes = strHashes };
		end
	end
end
Events.GameMessageChat.Add(OnKekSyncChat);
