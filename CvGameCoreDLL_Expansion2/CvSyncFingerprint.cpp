//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvSyncFingerprint.cpp
//! \brief   Live multiplayer desync detection -- see CvSyncFingerprint.h.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include "CvGameCoreDLLPCH.h"   // Must be first (precompiled header)
#include "CvSyncFingerprint.h"
#include "CvHttpUtils.h"
#include "CvGame.h"
#include "CvPreGame.h"

#ifdef KEK_SYNC_FINGERPRINT

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define KEK_SYNC_LOG "kek_desync.log"

namespace
{

enum SyncArea
{
	AREA_GAME,
	AREA_PLAYERS,
	AREA_UNITS,
	AREA_CITIES,
	NUM_AREAS
};

const char* const s_aszAreaNames[NUM_AREAS] = { "game", "players", "units", "cities" };

// Column names per area, in the order the values are written in TakeSnapshot.
// They go into kek_desync.log and the uploaded report so the two sides can be
// compared field by field.
const char* const s_aszPlayerFields[] = {
	"slot", "alive", "team", "goldTimes100", "culture", "faith", "numCities",
	"numUnits", "numPolicies", "numTechs", "goldenAgeTurns" };
const char* const s_aszUnitFields[] = {
	"owner", "id", "type", "x", "y", "damage", "moves", "experience", "level",
	"embarked", "fortifyTurns", "promotionHash" };
const char* const s_aszCityFields[] = {
	"owner", "id", "x", "y", "damage", "population", "foodTimes100",
	"productionTimes100", "numBuildings", "orderQueueLength", "orderType",
	"orderData" };

const int NUM_PLAYER_FIELDS = sizeof(s_aszPlayerFields) / sizeof(s_aszPlayerFields[0]);
const int NUM_UNIT_FIELDS   = sizeof(s_aszUnitFields) / sizeof(s_aszUnitFields[0]);
const int NUM_CITY_FIELDS   = sizeof(s_aszCityFields) / sizeof(s_aszCityFields[0]);

// One fixed-width table of ints per area.
struct AreaData
{
	int              iWidth;
	std::vector<int> aiValues;

	AreaData() : iWidth(0) {}
	int NumRows() const { return iWidth > 0 ? (int)aiValues.size() / iWidth : 0; }
};

struct Snapshot
{
	bool         bValid;
	int          iTurn;
	int          iSlice;
	unsigned int auiHash[NUM_AREAS];
	AreaData     aArea[NUM_AREAS];

	Snapshot() : bValid(false), iTurn(-1), iSlice(-1)
	{
		for (int i = 0; i < NUM_AREAS; i++)
			auiHash[i] = 0;
	}
};

// ~64 s of history at 20 slices per snapshot: far longer than a chat message
// takes to cross the network.
const int RING_SIZE = 32;

Snapshot s_aRing[RING_SIZE];
int      s_iNewest    = -1;   // index of the newest snapshot, -1 = none yet
int      s_iLastSlice = -1;

// Per remote player: last slice known to match, and whether the current
// out-of-sync episode has already been reported.
int  s_aiLastGoodSlice[MAX_PLAYERS];
bool s_abReported[MAX_PLAYERS];

void ResetState()
{
	for (int i = 0; i < RING_SIZE; i++)
		s_aRing[i] = Snapshot();
	s_iNewest    = -1;
	s_iLastSlice = -1;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		s_aiLastGoodSlice[i] = -1;
		s_abReported[i]      = false;
	}
}

// FNV-1a, 32-bit.
inline void HashInt(unsigned int& uiHash, int iValue)
{
	unsigned int uiValue = (unsigned int)iValue;
	for (int i = 0; i < 4; i++)
	{
		uiHash ^= (uiValue & 0xFF);
		uiHash *= 16777619u;
		uiValue >>= 8;
	}
}

unsigned int HashArea(const AreaData& kArea)
{
	unsigned int uiHash = 2166136261u;
	for (size_t i = 0; i < kArea.aiValues.size(); i++)
		HashInt(uiHash, kArea.aiValues[i]);
	return uiHash;
}

unsigned int HashPromotions(const CvUnit& kUnit, int iNumPromotions)
{
	unsigned int uiHash = 2166136261u;
	for (int i = 0; i < iNumPromotions; i++)
	{
		if (kUnit.isHasPromotion((PromotionTypes)i))
			HashInt(uiHash, i);
	}
	return uiHash;
}

void TakeSnapshot(Snapshot& kSnap)
{
	CvGame& kGame = GC.getGame();

	kSnap.bValid = true;
	kSnap.iTurn  = kGame.getGameTurn();
	kSnap.iSlice = kGame.getTurnSlice();

	// -- game: turn, slice, synced RNG, war matrix ---------------------------
	{
		AreaData& kArea = kSnap.aArea[AREA_GAME];
		kArea.aiValues.clear();
		const CvRandom& kRand = kGame.getJonRand();
		kArea.aiValues.push_back(kSnap.iTurn);
		kArea.aiValues.push_back(kSnap.iSlice);
		kArea.aiValues.push_back((int)kRand.getSeed());
		kArea.aiValues.push_back((int)kRand.getCallCount());
		for (int iI = 0; iI < MAX_CIV_TEAMS; iI++)
		{
			unsigned int uiLow = 0;
			unsigned int uiHigh = 0;
			const CvTeam& kTeam = GET_TEAM((TeamTypes)iI);
			for (int iJ = 0; iJ < MAX_CIV_TEAMS; iJ++)
			{
				if (kTeam.isAtWar((TeamTypes)iJ))
				{
					if (iJ < 32)
						uiLow |= (1u << iJ);
					else
						uiHigh |= (1u << (iJ - 32));
				}
			}
			kArea.aiValues.push_back((int)uiLow);
			kArea.aiValues.push_back((int)uiHigh);
		}
		kArea.iWidth = (int)kArea.aiValues.size();
	}

	// -- players ----------------------------------------------------------------
	{
		AreaData& kArea = kSnap.aArea[AREA_PLAYERS];
		kArea.iWidth = NUM_PLAYER_FIELDS;
		kArea.aiValues.clear();
		for (int iI = 0; iI < MAX_PLAYERS; iI++)
		{
			CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);
			if (!kPlayer.isEverAlive())
				continue;
			kArea.aiValues.push_back(iI);
			kArea.aiValues.push_back(kPlayer.isAlive() ? 1 : 0);
			kArea.aiValues.push_back((int)kPlayer.getTeam());
			kArea.aiValues.push_back(kPlayer.GetTreasury()->GetGoldTimes100());
			kArea.aiValues.push_back(kPlayer.getJONSCulture());
			kArea.aiValues.push_back(kPlayer.GetFaith());
			kArea.aiValues.push_back(kPlayer.getNumCities());
			kArea.aiValues.push_back(kPlayer.getNumUnits());
			kArea.aiValues.push_back(kPlayer.GetPlayerPolicies()->GetNumPoliciesOwned());
			kArea.aiValues.push_back(GET_TEAM(kPlayer.getTeam()).GetTeamTechs()->GetNumTechsKnown());
			kArea.aiValues.push_back(kPlayer.getGoldenAgeTurns());
		}
	}

	// -- units --------------------------------------------------------------------
	{
		AreaData& kArea = kSnap.aArea[AREA_UNITS];
		kArea.iWidth = NUM_UNIT_FIELDS;
		kArea.aiValues.clear();
		const int iNumPromotions = GC.getNumPromotionInfos();
		for (int iI = 0; iI < MAX_PLAYERS; iI++)
		{
			CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);
			if (!kPlayer.isEverAlive())
				continue;
			int iLoop = 0;
			for (CvUnit* pUnit = kPlayer.firstUnit(&iLoop); pUnit != NULL; pUnit = kPlayer.nextUnit(&iLoop))
			{
				kArea.aiValues.push_back(iI);
				kArea.aiValues.push_back(pUnit->GetID());
				kArea.aiValues.push_back((int)pUnit->getUnitType());
				kArea.aiValues.push_back(pUnit->getX());
				kArea.aiValues.push_back(pUnit->getY());
				kArea.aiValues.push_back(pUnit->getDamage());
				kArea.aiValues.push_back(pUnit->getMoves());
				kArea.aiValues.push_back(pUnit->getExperience());
				kArea.aiValues.push_back(pUnit->getLevel());
				kArea.aiValues.push_back(pUnit->isEmbarked() ? 1 : 0);
				kArea.aiValues.push_back(pUnit->getFortifyTurns());
				kArea.aiValues.push_back((int)HashPromotions(*pUnit, iNumPromotions));
			}
		}
	}

	// -- cities -------------------------------------------------------------------
	{
		AreaData& kArea = kSnap.aArea[AREA_CITIES];
		kArea.iWidth = NUM_CITY_FIELDS;
		kArea.aiValues.clear();
		for (int iI = 0; iI < MAX_PLAYERS; iI++)
		{
			CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);
			if (!kPlayer.isEverAlive())
				continue;
			int iLoop = 0;
			for (CvCity* pCity = kPlayer.firstCity(&iLoop); pCity != NULL; pCity = kPlayer.nextCity(&iLoop))
			{
				const OrderData* pOrder = pCity->headOrderQueueNode();
				kArea.aiValues.push_back(iI);
				kArea.aiValues.push_back(pCity->GetID());
				kArea.aiValues.push_back(pCity->getX());
				kArea.aiValues.push_back(pCity->getY());
				kArea.aiValues.push_back(pCity->getDamage());
				kArea.aiValues.push_back(pCity->getPopulation());
				kArea.aiValues.push_back(pCity->getFoodTimes100());
				kArea.aiValues.push_back(pCity->getProductionTimes100());
				kArea.aiValues.push_back(pCity->GetCityBuildings()->GetNumBuildings());
				kArea.aiValues.push_back(pCity->getOrderQueueLength());
				kArea.aiValues.push_back(pOrder ? (int)pOrder->eOrderType : -1);
				kArea.aiValues.push_back(pOrder ? pOrder->iData1 : -1);
			}
		}
	}

	for (int i = 0; i < NUM_AREAS; i++)
		kSnap.auiHash[i] = HashArea(kSnap.aArea[i]);
}

void FormatHashes(const unsigned int* auiHash, char* pszOut, size_t nLen)
{
	_snprintf_s(pszOut, nLen, _TRUNCATE, "%08x%08x%08x%08x",
	            auiHash[AREA_GAME], auiHash[AREA_PLAYERS], auiHash[AREA_UNITS], auiHash[AREA_CITIES]);
}

bool ParseHashes(const char* psz, unsigned int* auiHash)
{
	if (psz == NULL || strlen(psz) != 8 * NUM_AREAS)
		return false;
	for (int i = 0; i < NUM_AREAS; i++)
	{
		char szPart[9];
		memcpy(szPart, psz + i * 8, 8);
		szPart[8] = '\0';
		char* pEnd = NULL;
		auiHash[i] = (unsigned int)strtoul(szPart, &pEnd, 16);
		if (pEnd != szPart + 8)
			return false;
	}
	return true;
}

const Snapshot* FindSnapshot(int iSlice)
{
	for (int i = 0; i < RING_SIZE; i++)
	{
		if (s_aRing[i].bValid && s_aRing[i].iSlice == iSlice)
			return &s_aRing[i];
	}
	return NULL;
}

const char* const* AreaFieldNames(int iArea, int& iCount)
{
	switch (iArea)
	{
	case AREA_PLAYERS: iCount = NUM_PLAYER_FIELDS; return s_aszPlayerFields;
	case AREA_UNITS:   iCount = NUM_UNIT_FIELDS;   return s_aszUnitFields;
	case AREA_CITIES:  iCount = NUM_CITY_FIELDS;   return s_aszCityFields;
	default:           iCount = 0;                 return NULL;
	}
}

FILogFile* DesyncLog()
{
	FILogFileMgr* pMgr = FILogFileMgr::PeekInstance();
	return pMgr ? pMgr->GetLog(KEK_SYNC_LOG, FILogFile::kDontTimeStamp) : NULL;
}

// Writes the snapshot's rows for the mismatched areas (plus the small game
// area) to kek_desync.log.
void LogSnapshot(const Snapshot& kSnap, PlayerTypes eRemote, const unsigned int* auiRemote, int iLastGood)
{
	FILogFile* pLog = DesyncLog();
	if (!pLog)
		return;

	char szLocal[40], szRemote[40];
	FormatHashes(kSnap.auiHash, szLocal, sizeof(szLocal));
	FormatHashes(auiRemote, szRemote, sizeof(szRemote));
	pLog->Msg("=== DESYNC with player %d (%s): turn %d slice %d, last matching slice %d",
	          (int)eRemote, CvPreGame::nickname(eRemote).c_str(), kSnap.iTurn, kSnap.iSlice, iLastGood);
	pLog->Msg("    local  %s", szLocal);
	pLog->Msg("    remote %s", szRemote);

	for (int iArea = 0; iArea < NUM_AREAS; iArea++)
	{
		bool bMismatch = kSnap.auiHash[iArea] != auiRemote[iArea];
		if (!bMismatch && iArea != AREA_GAME)
			continue;
		const AreaData& kArea = kSnap.aArea[iArea];
		int iNumFields = 0;
		const char* const* aszFields = AreaFieldNames(iArea, iNumFields);
		pLog->Msg("--- %s (%s, %d rows)", s_aszAreaNames[iArea], bMismatch ? "MISMATCH" : "match", kArea.NumRows());
		if (aszFields)
		{
			std::string strHeader;
			for (int f = 0; f < iNumFields; f++)
			{
				if (f) strHeader += ",";
				strHeader += aszFields[f];
			}
			pLog->Msg("%s", strHeader.c_str());
		}
		// The engine's log line buffer size is unknown and the game row (war
		// matrix) is long, so rows are split every 24 values.
		const int iValuesPerLine = 24;
		for (int iRow = 0; iRow < kArea.NumRows(); iRow++)
		{
			std::string strRow;
			char szNum[16];
			for (int f = 0; f < kArea.iWidth; f++)
			{
				if (f > 0 && f % iValuesPerLine == 0)
				{
					pLog->Msg("%s,", strRow.c_str());
					strRow = "  ";
				}
				_snprintf_s(szNum, sizeof(szNum), _TRUNCATE, (f % iValuesPerLine) ? ",%d" : "%d", kArea.aiValues[iRow * kArea.iWidth + f]);
				strRow += szNum;
			}
			pLog->Msg("%s", strRow.c_str());
		}
	}
}

void AppendHashArray(std::string& out, const unsigned int* auiHash)
{
	char szNum[16];
	out += "[";
	for (int i = 0; i < NUM_AREAS; i++)
	{
		_snprintf_s(szNum, sizeof(szNum), _TRUNCATE, i ? ",\"%08x\"" : "\"%08x\"", auiHash[i]);
		out += szNum;
	}
	out += "]";
}

// Report JSON: the whole snapshot for the mismatched areas (plus the game
// area). The server pairs reports from both sides by gameId + slice.
void BuildReportJson(std::string& out, const Snapshot& kSnap, PlayerTypes eLocal, PlayerTypes eRemote,
                     const unsigned int* auiRemote, int iLastGood)
{
	char szBuf[256];
	_snprintf_s(szBuf, sizeof(szBuf), _TRUNCATE,
	            "{\"schema\":1,\"turn\":%d,\"slice\":%d,\"reporterSlot\":%d,\"remoteSlot\":%d,\"lastGoodSlice\":%d,",
	            kSnap.iTurn, kSnap.iSlice, (int)eLocal, (int)eRemote, iLastGood);
	out += szBuf;
	out += "\"localHashes\":";
	AppendHashArray(out, kSnap.auiHash);
	out += ",\"remoteHashes\":";
	AppendHashArray(out, auiRemote);

	out += ",\"mismatched\":[";
	bool bFirst = true;
	for (int iArea = 0; iArea < NUM_AREAS; iArea++)
	{
		if (kSnap.auiHash[iArea] == auiRemote[iArea])
			continue;
		if (!bFirst) out += ",";
		bFirst = false;
		out += "\"";
		out += s_aszAreaNames[iArea];
		out += "\"";
	}
	out += "],\"areas\":{";

	bFirst = true;
	for (int iArea = 0; iArea < NUM_AREAS; iArea++)
	{
		if (kSnap.auiHash[iArea] == auiRemote[iArea] && iArea != AREA_GAME)
			continue;
		const AreaData& kArea = kSnap.aArea[iArea];
		if (!bFirst) out += ",";
		bFirst = false;
		out += "\"";
		out += s_aszAreaNames[iArea];
		out += "\":{\"fields\":[";
		int iNumFields = 0;
		const char* const* aszFields = AreaFieldNames(iArea, iNumFields);
		for (int f = 0; f < iNumFields; f++)
		{
			if (f) out += ",";
			out += "\"";
			out += aszFields[f];
			out += "\"";
		}
		out += "],\"rows\":[";
		char szNum[16];
		for (int iRow = 0; iRow < kArea.NumRows(); iRow++)
		{
			out += iRow ? ",[" : "[";
			for (int f = 0; f < kArea.iWidth; f++)
			{
				_snprintf_s(szNum, sizeof(szNum), _TRUNCATE, f ? ",%d" : "%d", kArea.aiValues[iRow * kArea.iWidth + f]);
				out += szNum;
			}
			out += "]";
		}
		out += "]}";
	}
	out += "}}";
}

} // namespace

//	--------------------------------------------------------------------------------
void KekSync_OnSliceEnd()
{
	CvGame& kGame = GC.getGame();
	if (!kGame.isNetworkMultiPlayer())
		return;

	const int iSlice = kGame.getTurnSlice();
	// The slice advances by exactly one per update. Anything else means a save
	// was loaded or this client rejoined after a resync: what we hold (history,
	// "already reported" flags) belongs to a different game state.
	if (s_iLastSlice < 0 || iSlice != s_iLastSlice + 1)
		ResetState();
	s_iLastSlice = iSlice;

	if (iSlice % KEK_SYNC_SLICE_INTERVAL != 0)
		return;

	s_iNewest = (s_iNewest + 1) % RING_SIZE;
	TakeSnapshot(s_aRing[s_iNewest]);
}

//	--------------------------------------------------------------------------------
bool KekSync_GetLatest(int& iTurn, int& iSlice, CvString& strHashes)
{
	if (s_iNewest < 0 || !s_aRing[s_iNewest].bValid)
		return false;
	const Snapshot& kSnap = s_aRing[s_iNewest];
	char szHashes[40];
	FormatHashes(kSnap.auiHash, szHashes, sizeof(szHashes));
	iTurn     = kSnap.iTurn;
	iSlice    = kSnap.iSlice;
	strHashes = szHashes;
	return true;
}

//	--------------------------------------------------------------------------------
KekSyncCompareResult KekSync_Compare(PlayerTypes eRemote, int iTurn, int iSlice, const char* szRemoteHashes)
{
	if (eRemote < 0 || eRemote >= MAX_PLAYERS)
		return KEKSYNC_UNKNOWN;

	unsigned int auiRemote[NUM_AREAS];
	if (!ParseHashes(szRemoteHashes, auiRemote))
		return KEKSYNC_UNKNOWN;

	const Snapshot* pSnap = FindSnapshot(iSlice);
	if (pSnap == NULL || pSnap->iTurn != iTurn)
		return KEKSYNC_UNKNOWN;

	bool bMatch = true;
	for (int i = 0; i < NUM_AREAS; i++)
	{
		if (pSnap->auiHash[i] != auiRemote[i])
			bMatch = false;
	}

	if (bMatch)
	{
		if (iSlice > s_aiLastGoodSlice[eRemote])
			s_aiLastGoodSlice[eRemote] = iSlice;
		if (s_abReported[eRemote])
		{
			// Back in sync (e.g. after a resync): the next mismatch is a new episode.
			s_abReported[eRemote] = false;
			FILogFile* pLog = DesyncLog();
			if (pLog)
				pLog->Msg("=== back in sync with player %d at turn %d slice %d", (int)eRemote, iTurn, iSlice);
		}
		return KEKSYNC_MATCH;
	}

	if (s_abReported[eRemote])
		return KEKSYNC_MISMATCH_ONGOING;
	s_abReported[eRemote] = true;

	const int iLastGood = s_aiLastGoodSlice[eRemote];
	LogSnapshot(*pSnap, eRemote, auiRemote, iLastGood);

	const PlayerTypes eLocal = GC.getGame().getActivePlayer();
	std::string strJson;
	BuildReportJson(strJson, *pSnap, eLocal, eRemote, auiRemote, iLastGood);
	CvHttp_PostDesyncReport(pSnap->iTurn, pSnap->iSlice, eLocal, eRemote, strJson);

	return KEKSYNC_MISMATCH_NEW;
}

#else // KEK_SYNC_FINGERPRINT

void KekSync_OnSliceEnd() {}
bool KekSync_GetLatest(int&, int&, CvString&) { return false; }
KekSyncCompareResult KekSync_Compare(PlayerTypes, int, int, const char*) { return KEKSYNC_UNKNOWN; }

#endif // KEK_SYNC_FINGERPRINT
