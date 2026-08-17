#include "CvGameCoreDLLPCH.h"
#include "CvNetworkTest.h"

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

#pragma comment(lib, "winhttp.lib")

// Writes a line to kekmod_network.log next to the game executable
static void WriteLog(const char* szMsg)
{
	char szPath[MAX_PATH];
	GetModuleFileNameA(NULL, szPath, MAX_PATH);

	// Strip filename, keep directory
	char* pLastSlash = strrchr(szPath, '\\');
	if (pLastSlash)
		*(pLastSlash + 1) = '\0';

	strcat_s(szPath, "kekmod_network.log");

	FILE* f = NULL;
	fopen_s(&f, szPath, "a");
	if (f)
	{
		fprintf(f, "%s\n", szMsg);
		fclose(f);
	}

	OutputDebugStringA(szMsg);
	OutputDebugStringA("\n");
}

void KekMod_TestNetworkCall(const char* szUrl)
{
	WriteLog("[KekMod] Starting network test...");

	// Convert URL to wide string
	int nLen = MultiByteToWideChar(CP_UTF8, 0, szUrl, -1, NULL, 0);
	wchar_t* wszUrl = new wchar_t[nLen];
	MultiByteToWideChar(CP_UTF8, 0, szUrl, -1, wszUrl, nLen);

	// Crack the URL into components
	URL_COMPONENTS urlComp = {};
	urlComp.dwStructSize = sizeof(urlComp);

	wchar_t wszHostName[256] = {};
	wchar_t wszUrlPath[1024] = {};
	urlComp.lpszHostName    = wszHostName;
	urlComp.dwHostNameLength = 256;
	urlComp.lpszUrlPath     = wszUrlPath;
	urlComp.dwUrlPathLength  = 1024;

	if (!WinHttpCrackUrl(wszUrl, 0, 0, &urlComp))
	{
		WriteLog("[KekMod] ERROR: Failed to parse URL");
		delete[] wszUrl;
		return;
	}

	BOOL bHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

	// Open session
	HINTERNET hSession = WinHttpOpen(
		L"KekMod/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);

	if (!hSession)
	{
		WriteLog("[KekMod] ERROR: WinHttpOpen failed");
		delete[] wszUrl;
		return;
	}

	// Connect
	HINTERNET hConnect = WinHttpConnect(hSession, wszHostName, urlComp.nPort, 0);
	if (!hConnect)
	{
		WriteLog("[KekMod] ERROR: WinHttpConnect failed");
		WinHttpCloseHandle(hSession);
		delete[] wszUrl;
		return;
	}

	// Open GET request
	DWORD dwFlags = bHttps ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(
		hConnect,
		L"GET",
		wszUrlPath,
		NULL,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		dwFlags);

	if (!hRequest)
	{
		WriteLog("[KekMod] ERROR: WinHttpOpenRequest failed");
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		delete[] wszUrl;
		return;
	}

	// Send
	BOOL bSent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
	if (!bSent)
	{
		WriteLog("[KekMod] ERROR: WinHttpSendRequest failed");
	}
	else if (!WinHttpReceiveResponse(hRequest, NULL))
	{
		WriteLog("[KekMod] ERROR: WinHttpReceiveResponse failed");
	}
	else
	{
		// Read status code
		DWORD dwStatusCode = 0;
		DWORD dwSize = sizeof(dwStatusCode);
		WinHttpQueryHeaders(
			hRequest,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			NULL,
			&dwStatusCode,
			&dwSize,
			NULL);

		char szLog[256];
		sprintf_s(szLog, "[KekMod] Network test SUCCESS - HTTP %d from %s", (int)dwStatusCode, szUrl);
		WriteLog(szLog);
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	delete[] wszUrl;
}
