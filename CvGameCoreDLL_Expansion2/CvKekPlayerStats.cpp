//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//! \file    CvKekPlayerStats.cpp
//! \brief   Extra per-player stats written into the save. See the header.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include "CvGameCoreDLLPCH.h"   // Must be first (precompiled header)
#include "CvKekPlayerStats.h"
#include "CvGame.h"
#include "CvPlayer.h"

#ifdef KEK_PLAYER_STATS

// ---------------------------------------------------------------------------
// Pending local counts, since the last KekStats_RecordLocal. The click count
// is bumped by the hook on the window thread; the screen opens come from UI
// Lua, so they are interlocked too rather than assume which thread that is.
// ---------------------------------------------------------------------------
static volatile LONG g_lPendingClicks    = 0;
static volatile LONG g_lPendingDemoOpens = 0;
static volatile LONG g_lPendingCityOpens = 0;
static unsigned int  s_uiPendingActiveMs = 0;   // game thread only

// Active-turn clock: s_bClockRunning says whether the clock condition held at
// the last tick; the time since that tick is credited only if it did.
static bool  s_bClockRunning = false;
static DWORD s_dwLastTick    = 0;

// A single gap longer than this between two ticks (machine asleep, debugger)
// counts only this much.
static const DWORD KEK_STATS_MAX_TICK_GAP_MS = 30000;

// ---------------------------------------------------------------------------
// Click hook: a thread-local WH_MOUSE hook on the game window's thread. It
// sees every mouse message that thread takes from its queue, before the
// engine routes it to the UI or the map, so clicks on buttons count too.
// user32 is not in this project's link deps (see CvCrashReporter.cpp), so it
// is resolved at runtime. The PCH builds with NOUSER / NOMSG / NOWINMESSAGES,
// so the winuser.h names used here are declared locally (values from winuser.h).
// ---------------------------------------------------------------------------
typedef LRESULT (CALLBACK* KEK_HOOKPROC)(int, WPARAM, LPARAM);
static const int  KEK_WH_MOUSE           = 7;
static const int  KEK_HC_ACTION          = 0;
static const UINT KEK_WM_LBUTTONDOWN     = 0x0201;
static const UINT KEK_WM_LBUTTONDBLCLK   = 0x0203;
static const UINT KEK_WM_RBUTTONDOWN     = 0x0204;
static const UINT KEK_WM_RBUTTONDBLCLK   = 0x0206;
static const UINT KEK_WM_MBUTTONDOWN     = 0x0207;
static const UINT KEK_WM_MBUTTONDBLCLK   = 0x0209;

typedef HHOOK   (WINAPI* PFN_SetWindowsHookExA)(int, KEK_HOOKPROC, HINSTANCE, DWORD);
typedef BOOL    (WINAPI* PFN_UnhookWindowsHookEx)(HHOOK);
typedef LRESULT (WINAPI* PFN_CallNextHookEx)(HHOOK, int, WPARAM, LPARAM);
typedef HWND    (WINAPI* PFN_GetForegroundWindow)(void);
typedef DWORD   (WINAPI* PFN_GetWindowThreadProcessId)(HWND, LPDWORD);

static PFN_SetWindowsHookExA        g_pfnSetWindowsHookExA        = NULL;
static PFN_UnhookWindowsHookEx      g_pfnUnhookWindowsHookEx      = NULL;
static PFN_CallNextHookEx           g_pfnCallNextHookEx           = NULL;
static PFN_GetForegroundWindow      g_pfnGetForegroundWindow      = NULL;
static PFN_GetWindowThreadProcessId g_pfnGetWindowThreadProcessId = NULL;

static HHOOK g_hMouseHook       = NULL;
static bool  s_bUser32Failed    = false;
static DWORD s_dwLastHookTryTick = 0;

static bool LoadUser32HookFns()
{
	if (g_pfnSetWindowsHookExA)
		return true;
	if (s_bUser32Failed)
		return false;
	HMODULE hUser32 = LoadLibraryA("user32.dll");
	if (hUser32)
	{
		g_pfnSetWindowsHookExA = (PFN_SetWindowsHookExA)GetProcAddress(hUser32, "SetWindowsHookExA");
		g_pfnUnhookWindowsHookEx = (PFN_UnhookWindowsHookEx)GetProcAddress(hUser32, "UnhookWindowsHookEx");
		g_pfnCallNextHookEx = (PFN_CallNextHookEx)GetProcAddress(hUser32, "CallNextHookEx");
		g_pfnGetForegroundWindow = (PFN_GetForegroundWindow)GetProcAddress(hUser32, "GetForegroundWindow");
		g_pfnGetWindowThreadProcessId = (PFN_GetWindowThreadProcessId)GetProcAddress(hUser32, "GetWindowThreadProcessId");
	}
	if (!g_pfnSetWindowsHookExA || !g_pfnUnhookWindowsHookEx || !g_pfnCallNextHookEx
		|| !g_pfnGetForegroundWindow || !g_pfnGetWindowThreadProcessId)
	{
		g_pfnSetWindowsHookExA = NULL;
		s_bUser32Failed = true;
		return false;
	}
	return true;
}

static LRESULT CALLBACK KekStatsMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	// HC_ACTION only: HC_NOREMOVE is a PeekMessage look at a message that is
	// still queued and will come through again.
	if (nCode == KEK_HC_ACTION)
	{
		const UINT uiMsg = (UINT)wParam;
		if (uiMsg == KEK_WM_LBUTTONDOWN || uiMsg == KEK_WM_LBUTTONDBLCLK   // DBLCLK: second press of a double click
			|| uiMsg == KEK_WM_RBUTTONDOWN || uiMsg == KEK_WM_RBUTTONDBLCLK
			|| uiMsg == KEK_WM_MBUTTONDOWN || uiMsg == KEK_WM_MBUTTONDBLCLK)
		{
			InterlockedIncrement(&g_lPendingClicks);
		}
	}
	return g_pfnCallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

// The DLL has no handle to the game window. Once this process owns the
// foreground window, hook that window's thread. Retried every 2 s until then
// (e.g. the player is alt-tabbed while the game loads).
static void TryInstallMouseHook()
{
	if (g_hMouseHook)
		return;
	const DWORD dwNow = timeGetTime();
	if (s_dwLastHookTryTick != 0 && dwNow - s_dwLastHookTryTick < 2000)
		return;
	s_dwLastHookTryTick = dwNow;
	if (!LoadUser32HookFns())
		return;

	HWND hFg = g_pfnGetForegroundWindow();
	if (!hFg)
		return;
	DWORD dwPid = 0;
	const DWORD dwThread = g_pfnGetWindowThreadProcessId(hFg, &dwPid);
	if (dwPid != GetCurrentProcessId() || dwThread == 0)
		return;

	// This DLL's module handle, from the address of the hook procedure.
	MEMORY_BASIC_INFORMATION mbi;
	if (VirtualQuery((LPCVOID)&KekStatsMouseProc, &mbi, sizeof(mbi)) == 0)
		return;
	g_hMouseHook = g_pfnSetWindowsHookExA(KEK_WH_MOUSE, KekStatsMouseProc, (HINSTANCE)mbi.AllocationBase, dwThread);
}

// ---------------------------------------------------------------------------
// Datasets
// ---------------------------------------------------------------------------
static void AddToDataset(CvPlayer& kPlayer, const char* szDataSet, int iTurn, int iAdd)
{
	if (iTurn < 0)
		return;
	const unsigned int uiSet = kPlayer.getReplayDataSetIndex(szDataSet);
	int iValue = kPlayer.getReplayDataValue(uiSet, (unsigned int)iTurn);
	if (iValue < 0)   // -1: no entry for this turn yet
		iValue = 0;
	kPlayer.setReplayDataValue(uiSet, (unsigned int)iTurn, iValue + iAdd);
}

// The clock runs while the local player's own turn is active and not ended,
// and the game is not paused (a turn timer pause still counts: the player
// can keep playing).
static bool IsLocalTurnClockCondition()
{
	CvGame& kGame = GC.getGame();
	if (kGame.getGameState() != GAMESTATE_ON || kGame.isPaused())
		return false;
	const PlayerTypes eActive = kGame.getActivePlayer();
	if (eActive < 0 || eActive >= MAX_MAJOR_CIVS)
		return false;
	CvPlayer& kPlayer = GET_PLAYER(eActive);
	return kPlayer.isAlive() && kPlayer.isHuman() && !kPlayer.isObserver()
		&& kPlayer.isTurnActive() && !kPlayer.isEndTurn();
}

static void TickClock()
{
	const DWORD dwNow = timeGetTime();
	if (s_bClockRunning)
	{
		DWORD dwGap = dwNow - s_dwLastTick;   // unsigned: survives the 49-day wrap
		if (dwGap > KEK_STATS_MAX_TICK_GAP_MS)
			dwGap = KEK_STATS_MAX_TICK_GAP_MS;
		s_uiPendingActiveMs += dwGap;
	}
	s_bClockRunning = IsLocalTurnClockCondition();
	s_dwLastTick = dwNow;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void KekStats_Reset()
{
	InterlockedExchange(&g_lPendingClicks, 0);
	InterlockedExchange(&g_lPendingDemoOpens, 0);
	InterlockedExchange(&g_lPendingCityOpens, 0);
	s_uiPendingActiveMs = 0;
	s_bClockRunning = false;
	s_dwLastTick = timeGetTime();
}

void KekStats_Update()
{
	if (GC.getGame().getGameState() == GAMESTATE_ON)
		TryInstallMouseHook();
	TickClock();
}

void KekStats_OnDemographicsOpened()
{
	InterlockedIncrement(&g_lPendingDemoOpens);
}

void KekStats_OnCityScreenEntered()
{
	InterlockedIncrement(&g_lPendingCityOpens);
}

void KekStats_RecordLocal(int iTurn)
{
	TickClock();   // credit the clock up to now

	const PlayerTypes eActive = GC.getGame().getActivePlayer();
	if (eActive < 0 || eActive >= MAX_MAJOR_CIVS)
		return;
	CvPlayer& kPlayer = GET_PLAYER(eActive);
	if (!kPlayer.isHuman() || kPlayer.isObserver())
		return;   // pending counts stay; nothing to file them under

	AddToDataset(kPlayer, "REPLAYDATASET_KEK_CLICKS", iTurn, (int)InterlockedExchange(&g_lPendingClicks, 0));
	AddToDataset(kPlayer, "REPLAYDATASET_KEK_ACTIVETURNMS", iTurn, (int)s_uiPendingActiveMs);
	AddToDataset(kPlayer, "REPLAYDATASET_KEK_DEMOGRAPHICSOPENS", iTurn, (int)InterlockedExchange(&g_lPendingDemoOpens, 0));
	AddToDataset(kPlayer, "REPLAYDATASET_KEK_CITYSCREENOPENS", iTurn, (int)InterlockedExchange(&g_lPendingCityOpens, 0));
	s_uiPendingActiveMs = 0;
}

void KekStats_OnTimerReset(PlayerTypes ePlayer)
{
	if (ePlayer < 0 || ePlayer >= MAX_PLAYERS)
		return;
	AddToDataset(GET_PLAYER(ePlayer), "REPLAYDATASET_KEK_TIMERRESETS", GC.getGame().getGameTurn(), 1);
}

void KekStats_OnTimerPaused(PlayerTypes ePlayer)
{
	if (ePlayer < 0 || ePlayer >= MAX_PLAYERS)
		return;
	AddToDataset(GET_PLAYER(ePlayer), "REPLAYDATASET_KEK_TIMERPAUSES", GC.getGame().getGameTurn(), 1);
}

void KekStats_Shutdown()
{
	if (g_hMouseHook && g_pfnUnhookWindowsHookEx)
		g_pfnUnhookWindowsHookEx(g_hMouseHook);
	g_hMouseHook = NULL;
}

#endif // KEK_PLAYER_STATS
