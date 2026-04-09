// RcCompare.cpp --- Standalone PE resource compare tool
// Entry point that launches the compare dialog directly.

#include <initguid.h>
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <locale.h>
#include <strsafe.h>
#include "Res.hpp"
#include "ConstantsDB.hpp"
#include "RisohSettings.hpp"
#include "CFormRcCompare.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")

// Global variable definitions required by libRisohEditor
EntrySet g_res;
ConstantsDB g_db;
RisohSettings g_settings;
HWND g_hMainWnd = NULL;
LPWSTR g_pszLogFile = NULL;

// Stub for TextFromLang (used by EntryBase::get_lang_label in Res.obj)
MStringW TextFromLang(WORD lang)
{
	WCHAR sz[MAX_PATH];
	LCID lcid = MAKELCID(lang, SORT_DEFAULT);
	if (lcid == 0)
	{
		StringCchPrintfW(sz, _countof(sz), L"Neutral (0)");
	}
	else
	{
		WCHAR szLoc[MAX_PATH];
		if (GetLocaleInfo(lcid, LOCALE_SLANGUAGE, szLoc, _countof(szLoc)))
			StringCchPrintfW(sz, _countof(sz), L"%s (%u)", szLoc, lang);
		else
			StringCchPrintfW(sz, _countof(sz), L"%u", lang);
	}
	return MStringW(sz);
}

extern "C"
INT WINAPI
wWinMain(HINSTANCE   hInstance,
         HINSTANCE   hPrevInstance,
         LPWSTR      lpCmdLine,
         INT         nCmdShow)
{
	// COM initialization
	HRESULT hrCoInit = CoInitialize(NULL);

	// Initialize common controls (v6 via manifest)
	INITCOMMONCONTROLSEX iccx;
	iccx.dwSize = sizeof(iccx);
	iccx.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES |
	             ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
	             ICC_PROGRESS_CLASS | ICC_TAB_CLASSES;
	InitCommonControlsEx(&iccx);

	// Set locale for proper string handling
	setlocale(LC_CTYPE, "");

	// Launch the compare dialog
	CFormRcCompare dlg(NULL);
	dlg.DoCompareDialog();

	// Cleanup
	if (SUCCEEDED(hrCoInit))
		CoUninitialize();

	return 0;
}
