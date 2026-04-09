// CTaskEditDialog.cpp --- Task configuration edit dialog (Win32 native)

#include "CTaskEditDialog.h"
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace rceditor {

static const int ROW_HEIGHT = 26;
static const int ROW_SPACING = 4;
static const int LABEL_WIDTH = 130;
static const int BTN_WIDTH = 34;
static const int MARGIN = 10;
static const int GROUP_HEADER_HEIGHT = 22;

CTaskEditDialog::CTaskEditDialog(SCompareTaskConfig& task, HWND hParent)
	: m_task(task)
	, m_hParent(hParent)
	, m_hFont(NULL)
	, m_hwndLblPriGroup(NULL), m_hwndLblPriDll(NULL), m_hwndEdtPriDll(NULL), m_hwndBtnPriDll(NULL)
	, m_hwndLblPriRc(NULL),  m_hwndEdtPriRc(NULL),  m_hwndBtnPriRc(NULL)
	, m_hwndLblPriH(NULL),   m_hwndEdtPriH(NULL),   m_hwndBtnPriH(NULL)
	, m_hwndLblSecGroup(NULL), m_hwndLblSecDll(NULL), m_hwndEdtSecDll(NULL), m_hwndBtnSecDll(NULL)
	, m_hwndLblSecRc(NULL),  m_hwndEdtSecRc(NULL),  m_hwndBtnSecRc(NULL)
	, m_hwndLblSecH(NULL),   m_hwndEdtSecH(NULL),   m_hwndBtnSecH(NULL)
	, m_hwndLblOutGroup(NULL), m_hwndLblOutDir(NULL), m_hwndEdtOutDir(NULL), m_hwndBtnOutDir(NULL)
	, m_hwndBtnOk(NULL), m_hwndBtnCancel(NULL)
{
}

CTaskEditDialog::~CTaskEditDialog()
{
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = NULL;
	}
}

INT_PTR CTaskEditDialog::DoEditDialog()
{
	// Build a DLGTEMPLATE at runtime
	m_dlgTemplateBuffer.assign(512, 0);
	DLGTEMPLATE* pDlg = reinterpret_cast<DLGTEMPLATE*>(m_dlgTemplateBuffer.data());
	pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
	              WS_VISIBLE | WS_THICKFRAME;
	pDlg->dwExtendedStyle = WS_EX_DLGMODALFRAME;
	pDlg->cdit = 0;
	pDlg->x = 0; pDlg->y = 0;
	pDlg->cx = (short)MulDiv(760, 4, LOWORD(GetDialogBaseUnits()));
	pDlg->cy = (short)MulDiv(410, 8, HIWORD(GetDialogBaseUnits()));

	WORD* pw = reinterpret_cast<WORD*>(pDlg + 1);
	*pw++ = 0; // no menu
	*pw++ = 0; // default class
	// Title
	LPCWSTR pszTitle = L"\x7F16\x8F91\x6BD4\x5BF9\x4EFB\x52A1";
	while (*pszTitle) *pw++ = *pszTitle++;
	*pw++ = 0;

	return DialogBoxIndirectDx(m_hParent, pDlg);
}

INT_PTR CALLBACK
CTaskEditDialog::DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		return OnInitDialog(hwnd);

	case WM_SIZE:
		OnSize(hwnd, LOWORD(lParam), HIWORD(lParam));
		return 0;

	case WM_COMMAND:
		OnCommand(hwnd, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
		return 0;

	case WM_CLOSE:
		EndDialog(hwnd, IDCANCEL);
		return 0;
	}
	return DefaultProcDx();
}

BOOL CTaskEditDialog::OnInitDialog(HWND hwnd)
{
	m_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	if (!m_hFont)
		m_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	CreateControls();
	PopulateData();

	// Resize and center
	SetWindowPos(hwnd, NULL, 0, 0, 760, 410, SWP_NOMOVE | SWP_NOZORDER);
	RECT rcParent;
	if (m_hParent)
		GetWindowRect(m_hParent, &rcParent);
	else
	{
		rcParent.left = 0; rcParent.top = 0;
		rcParent.right = GetSystemMetrics(SM_CXSCREEN);
		rcParent.bottom = GetSystemMetrics(SM_CYSCREEN);
	}
	RECT rcSelf;
	GetWindowRect(hwnd, &rcSelf);
	int cx = rcSelf.right - rcSelf.left;
	int cy = rcSelf.bottom - rcSelf.top;
	SetWindowPos(hwnd, NULL,
		(rcParent.left + rcParent.right - cx) / 2,
		(rcParent.top + rcParent.bottom - cy) / 2,
		0, 0, SWP_NOSIZE | SWP_NOZORDER);

	LayoutControls();
	return TRUE;
}

void CTaskEditDialog::OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDC_BTN_PRI_DLL:  OnBrowsePrimaryDll(); break;
	case IDC_BTN_PRI_RC:   OnBrowsePrimaryRc(); break;
	case IDC_BTN_PRI_H:    OnBrowsePrimaryH(); break;
	case IDC_BTN_SEC_DLL:  OnBrowseSecondaryDll(); break;
	case IDC_BTN_SEC_RC:   OnBrowseSecondaryRc(); break;
	case IDC_BTN_SEC_H:    OnBrowseSecondaryH(); break;
	case IDC_BTN_OUT_DIR:  OnBrowseOutputDir(); break;
	case IDOK:
		CollectData();
		EndDialog(hwnd, IDOK);
		break;
	case IDCANCEL:
		EndDialog(hwnd, IDCANCEL);
		break;
	}
}

void CTaskEditDialog::OnSize(HWND hwnd, int cx, int cy)
{
	if (m_hwndEdtPriDll)
		LayoutControls();
}

// ============================================================================
// Control creation
// ============================================================================

int CTaskEditDialog::CreatePathRow(int y, UINT idLabel, UINT idEdit, UINT idBtn,
                                   LPCWSTR pszLabelText,
                                   HWND& hLabel, HWND& hEdit, HWND& hBtn)
{
	HWND hwnd = m_hwnd;
	HINSTANCE hInst = GetModuleHandle(NULL);

	RECT rcClient;
	GetClientRect(hwnd, &rcClient);
	int editWidth = (rcClient.right - rcClient.left) - MARGIN * 2 - LABEL_WIDTH - BTN_WIDTH - 8;
	if (editWidth < 100) editWidth = 100;

	int x = MARGIN;

	hLabel = CreateWindowExW(0, L"STATIC", pszLabelText,
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		x, y + 3, LABEL_WIDTH, ROW_HEIGHT, hwnd, (HMENU)(UINT_PTR)idLabel, hInst, NULL);
	SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	x += LABEL_WIDTH + 4;

	hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
		x, y, editWidth, ROW_HEIGHT, hwnd, (HMENU)(UINT_PTR)idEdit, hInst, NULL);
	SendMessageW(hEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	x += editWidth + 4;

	hBtn = CreateWindowExW(0, L"BUTTON", L"...",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		x, y, BTN_WIDTH, ROW_HEIGHT, hwnd, (HMENU)(UINT_PTR)idBtn, hInst, NULL);
	SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	return y + ROW_HEIGHT + ROW_SPACING;
}

void CTaskEditDialog::CreateControls()
{
	HWND hwnd = m_hwnd;
	HINSTANCE hInst = GetModuleHandle(NULL);
	int y = MARGIN;

	// Primary group
	m_hwndLblPriGroup = CreateWindowExW(0, L"STATIC", L"\x4E3B\x8BED\x8A00 (\x6E90)",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		MARGIN, y, 400, GROUP_HEADER_HEIGHT, hwnd,
		(HMENU)(UINT_PTR)IDC_STATIC_PRIMARY_GROUP, hInst, NULL);
	SendMessageW(m_hwndLblPriGroup, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	y += GROUP_HEADER_HEIGHT + 4;

	y = CreatePathRow(y, IDC_STATIC_PRI_DLL, IDC_EDIT_PRI_DLL, IDC_BTN_PRI_DLL,
		L"DLL \x8DEF\x5F84:", m_hwndLblPriDll, m_hwndEdtPriDll, m_hwndBtnPriDll);
	y = CreatePathRow(y, IDC_STATIC_PRI_RC, IDC_EDIT_PRI_RC, IDC_BTN_PRI_RC,
		L"RC \x8DEF\x5F84:", m_hwndLblPriRc, m_hwndEdtPriRc, m_hwndBtnPriRc);
	y = CreatePathRow(y, IDC_STATIC_PRI_H, IDC_EDIT_PRI_H, IDC_BTN_PRI_H,
		L"resource.h \x8DEF\x5F84:", m_hwndLblPriH, m_hwndEdtPriH, m_hwndBtnPriH);
	y += 8;

	// Secondary group
	m_hwndLblSecGroup = CreateWindowExW(0, L"STATIC", L"\x6B21\x8BED\x8A00 (\x76EE\x6807)",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		MARGIN, y, 400, GROUP_HEADER_HEIGHT, hwnd,
		(HMENU)(UINT_PTR)IDC_STATIC_SEC_GROUP, hInst, NULL);
	SendMessageW(m_hwndLblSecGroup, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	y += GROUP_HEADER_HEIGHT + 4;

	y = CreatePathRow(y, IDC_STATIC_SEC_DLL, IDC_EDIT_SEC_DLL, IDC_BTN_SEC_DLL,
		L"DLL \x8DEF\x5F84:", m_hwndLblSecDll, m_hwndEdtSecDll, m_hwndBtnSecDll);
	y = CreatePathRow(y, IDC_STATIC_SEC_RC, IDC_EDIT_SEC_RC, IDC_BTN_SEC_RC,
		L"RC \x8DEF\x5F84:", m_hwndLblSecRc, m_hwndEdtSecRc, m_hwndBtnSecRc);
	y = CreatePathRow(y, IDC_STATIC_SEC_H, IDC_EDIT_SEC_H, IDC_BTN_SEC_H,
		L"resource.h \x8DEF\x5F84:", m_hwndLblSecH, m_hwndEdtSecH, m_hwndBtnSecH);
	y += 8;

	// Output group
	m_hwndLblOutGroup = CreateWindowExW(0, L"STATIC", L"\x8F93\x51FA (\x5BFC\x51FA\x8DEF\x5F84)",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		MARGIN, y, 400, GROUP_HEADER_HEIGHT, hwnd,
		(HMENU)(UINT_PTR)IDC_STATIC_OUT_GROUP, hInst, NULL);
	SendMessageW(m_hwndLblOutGroup, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	y += GROUP_HEADER_HEIGHT + 4;

	y = CreatePathRow(y, IDC_STATIC_OUT_DIR, IDC_EDIT_OUT_DIR, IDC_BTN_OUT_DIR,
		L"\x8F93\x51FA\x76EE\x5F55:", m_hwndLblOutDir, m_hwndEdtOutDir, m_hwndBtnOutDir);

	// OK / Cancel buttons
	m_hwndBtnOk = CreateWindowExW(0, L"BUTTON", L"\x786E\x5B9A",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		0, 0, 80, ROW_HEIGHT + 2, hwnd, (HMENU)(UINT_PTR)IDOK, hInst, NULL);
	SendMessageW(m_hwndBtnOk, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndBtnCancel = CreateWindowExW(0, L"BUTTON", L"\x53D6\x6D88",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 80, ROW_HEIGHT + 2, hwnd, (HMENU)(UINT_PTR)IDCANCEL, hInst, NULL);
	SendMessageW(m_hwndBtnCancel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
}

void CTaskEditDialog::LayoutControls()
{
	RECT rcClient;
	GetClientRect(m_hwnd, &rcClient);

	int w = rcClient.right - rcClient.left;
	int editWidth = w - MARGIN * 2 - LABEL_WIDTH - BTN_WIDTH - 8;
	if (editWidth < 100) editWidth = 100;

	// Adjust all edit + button pairs
	struct EditBtnPair { HWND hEdit; HWND hBtn; };
	EditBtnPair pairs[] = {
		{ m_hwndEdtPriDll, m_hwndBtnPriDll },
		{ m_hwndEdtPriRc,  m_hwndBtnPriRc  },
		{ m_hwndEdtPriH,   m_hwndBtnPriH   },
		{ m_hwndEdtSecDll, m_hwndBtnSecDll },
		{ m_hwndEdtSecRc,  m_hwndBtnSecRc  },
		{ m_hwndEdtSecH,   m_hwndBtnSecH   },
		{ m_hwndEdtOutDir, m_hwndBtnOutDir },
	};

	for (auto& pair : pairs)
	{
		if (!pair.hEdit) continue;
		RECT rcEdit;
		GetWindowRect(pair.hEdit, &rcEdit);
		POINT pt = { rcEdit.left, rcEdit.top };
		ScreenToClient(m_hwnd, &pt);
		int x = MARGIN + LABEL_WIDTH + 4;
		MoveWindow(pair.hEdit, x, pt.y, editWidth, ROW_HEIGHT, TRUE);
		MoveWindow(pair.hBtn, x + editWidth + 4, pt.y, BTN_WIDTH, ROW_HEIGHT, TRUE);
	}

	// OK / Cancel buttons at bottom-right
	const int btnW = 90;
	const int btnH = 28;
	const int btnGap = 10;
	int h = rcClient.bottom - rcClient.top;
	int btnY = h - MARGIN - btnH;
	if (m_hwndBtnCancel)
		MoveWindow(m_hwndBtnCancel, w - MARGIN - btnW, btnY, btnW, btnH, TRUE);
	if (m_hwndBtnOk)
		MoveWindow(m_hwndBtnOk, w - MARGIN - btnW * 2 - btnGap, btnY, btnW, btnH, TRUE);
}

// ============================================================================
// Data fill / collect
// ============================================================================

void CTaskEditDialog::PopulateData()
{
	SetWindowTextW(m_hwndEdtPriDll, m_task.primaryDllPath.c_str());
	SetWindowTextW(m_hwndEdtPriRc, m_task.primaryRcPath.c_str());
	SetWindowTextW(m_hwndEdtPriH, m_task.primaryResourceHPath.c_str());

	SetWindowTextW(m_hwndEdtSecDll, m_task.secondaryDllPath.c_str());
	SetWindowTextW(m_hwndEdtSecRc, m_task.secondaryRcPath.c_str());
	SetWindowTextW(m_hwndEdtSecH, m_task.secondaryResourceHPath.c_str());

	std::wstring wsOutDir = m_task.outputDir;
	if (wsOutDir.empty())
		wsOutDir = BuildDefaultOutputDir();
	SetWindowTextW(m_hwndEdtOutDir, wsOutDir.c_str());
}

void CTaskEditDialog::CollectData()
{
	auto GetEditText = [](HWND hEdit) -> std::wstring
	{
		int len = GetWindowTextLengthW(hEdit);
		if (len <= 0) return std::wstring();
		std::wstring ws(len + 1, L'\0');
		GetWindowTextW(hEdit, &ws[0], len + 1);
		ws.resize(len);
		return ws;
	};

	m_task.primaryDllPath = GetEditText(m_hwndEdtPriDll);
	m_task.primaryRcPath = GetEditText(m_hwndEdtPriRc);
	m_task.primaryResourceHPath = GetEditText(m_hwndEdtPriH);

	m_task.secondaryDllPath = GetEditText(m_hwndEdtSecDll);
	m_task.secondaryRcPath = GetEditText(m_hwndEdtSecRc);
	m_task.secondaryResourceHPath = GetEditText(m_hwndEdtSecH);

	m_task.outputDir = GetEditText(m_hwndEdtOutDir);
	if (m_task.outputDir.empty())
		m_task.outputDir = BuildDefaultOutputDir();
	m_task.outputDllPath.clear();
	m_task.outputRcPath.clear();
	m_task.outputResourceHPath.clear();
}

std::wstring CTaskEditDialog::BuildDefaultOutputDir() const
{
	WCHAR szBuf[MAX_PATH] = { 0 };
	if (m_hwndEdtSecDll)
		GetWindowTextW(m_hwndEdtSecDll, szBuf, MAX_PATH);

	std::wstring wsBinPath = szBuf;
	if (wsBinPath.empty())
		wsBinPath = m_task.secondaryDllPath;
	if (wsBinPath.empty())
		return std::wstring();

	size_t slashPos = wsBinPath.rfind(L'\\');
	if (slashPos == std::wstring::npos)
		slashPos = wsBinPath.rfind(L'/');

	std::wstring wsDir;
	std::wstring wsFileName = wsBinPath;
	if (slashPos != std::wstring::npos)
	{
		wsDir = wsBinPath.substr(0, slashPos);
		wsFileName = wsBinPath.substr(slashPos + 1);
	}

	size_t dotPos = wsFileName.rfind(L'.');
	if (dotPos != std::wstring::npos && dotPos > 0)
		wsFileName = wsFileName.substr(0, dotPos);

	if (wsFileName.empty())
		wsFileName = L"merged";

	if (wsDir.empty())
		return wsFileName + L"_merged";
	return wsDir + L"\\" + wsFileName + L"_merged";
}

// ============================================================================
// File browse (XP compatible)
// ============================================================================

void CTaskEditDialog::BrowseForFile(HWND hwndEdit, LPCWSTR pszFilter, LPCWSTR pszTitle)
{
	WCHAR szFile[MAX_PATH] = { 0 };
	GetWindowTextW(hwndEdit, szFile, MAX_PATH);

	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = pszFilter;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = pszTitle;
	ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;

	if (GetOpenFileNameW(&ofn))
	{
		SetWindowTextW(hwndEdit, szFile);
	}
}

void CTaskEditDialog::BrowseForFolder(HWND hwndEdit, LPCWSTR pszTitle)
{
	// Use SHBrowseForFolder (XP compatible)
	BROWSEINFOW bi;
	ZeroMemory(&bi, sizeof(bi));
	bi.hwndOwner = m_hwnd;
	bi.lpszTitle = pszTitle;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

	LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
	if (pidl)
	{
		WCHAR szPath[MAX_PATH] = { 0 };
		if (SHGetPathFromIDListW(pidl, szPath))
		{
			SetWindowTextW(hwndEdit, szPath);
		}
		CoTaskMemFree(pidl);
	}
}

void CTaskEditDialog::OnBrowsePrimaryDll()
{
	BrowseForFile(m_hwndEdtPriDll,
		L"Binary Files (*.dll;*.exe)\0*.dll;*.exe\0DLL Files (*.dll)\0*.dll\0EXE Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0",
		L"\x9009\x62E9\x4E3B\x8BED\x8A00\x4E8C\x8FDB\x5236\x6587\x4EF6");
}

void CTaskEditDialog::OnBrowsePrimaryRc()
{
	BrowseForFile(m_hwndEdtPriRc,
		L"RC Files (*.rc)\0*.rc\0All Files (*.*)\0*.*\0",
		L"\x9009\x62E9\x4E3B\x8BED\x8A00 RC \x6587\x4EF6");
}

void CTaskEditDialog::OnBrowsePrimaryH()
{
	BrowseForFile(m_hwndEdtPriH,
		L"Header Files (*.h)\0*.h\0All Files (*.*)\0*.*\0",
		L"\x9009\x62E9\x4E3B\x8BED\x8A00 resource.h");
}

void CTaskEditDialog::OnBrowseSecondaryDll()
{
	BrowseForFile(m_hwndEdtSecDll,
		L"Binary Files (*.dll;*.exe)\0*.dll;*.exe\0DLL Files (*.dll)\0*.dll\0EXE Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0",
		L"\x9009\x62E9\x6B21\x8BED\x8A00\x4E8C\x8FDB\x5236\x6587\x4EF6");

	// Auto-fill output dir
	WCHAR szOutDir[MAX_PATH] = { 0 };
	GetWindowTextW(m_hwndEdtOutDir, szOutDir, MAX_PATH);
	if (szOutDir[0] == L'\0')
	{
		std::wstring wsDefault = BuildDefaultOutputDir();
		SetWindowTextW(m_hwndEdtOutDir, wsDefault.c_str());
	}
}

void CTaskEditDialog::OnBrowseSecondaryRc()
{
	BrowseForFile(m_hwndEdtSecRc,
		L"RC Files (*.rc)\0*.rc\0All Files (*.*)\0*.*\0",
		L"\x9009\x62E9\x6B21\x8BED\x8A00 RC \x6587\x4EF6");
}

void CTaskEditDialog::OnBrowseSecondaryH()
{
	BrowseForFile(m_hwndEdtSecH,
		L"Header Files (*.h)\0*.h\0All Files (*.*)\0*.*\0",
		L"\x9009\x62E9\x6B21\x8BED\x8A00 resource.h");
}

void CTaskEditDialog::OnBrowseOutputDir()
{
	BrowseForFolder(m_hwndEdtOutDir, L"\x9009\x62E9\x8F93\x51FA\x76EE\x5F55");
}

} // namespace rceditor
