// CRcCompareViewer.cpp --- PE resource compare viewer (Win32 native)
// MWindowBase-based viewer replicating MMainWnd layout with MSplitterWnd.

#include "CRcCompareViewer.h"
#include <commctrl.h>
#include <commdlg.h>
#include <strsafe.h>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

// ============================================================================
// Subclass procs for proper painting and layout
// ============================================================================

// Toolbar host subclass: handle WM_CTLCOLORSTATIC/WM_CTLCOLORBTN so that
// child checkboxes and statics paint with the system button-face background.
static LRESULT CALLBACK ToolbarHostSubclassProc(
	HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
		{
			HDC hdc = (HDC)wParam;
			SetBkMode(hdc, TRANSPARENT);
			return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
		}
	case WM_ERASEBKGND:
		{
			HDC hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hwnd, &rc);
			FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
			return TRUE;
		}
	case WM_NCDESTROY:
		RemoveWindowSubclass(hwnd, ToolbarHostSubclassProc, uIdSubclass);
		break;
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Bottom panel subclass: resize the child ListView to fill the panel area.
static LRESULT CALLBACK BottomPanelSubclassProc(
	HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	if (uMsg == WM_SIZE)
	{
		HWND hwndList = (HWND)dwRefData;
		if (hwndList)
			MoveWindow(hwndList, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
	}
	else if (uMsg == WM_NCDESTROY)
	{
		RemoveWindowSubclass(hwnd, BottomPanelSubclassProc, uIdSubclass);
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Language name helper
static std::wstring LangIdToDisplayName(LANGID lang)
{
	WCHAR szLoc[MAX_PATH];
	LCID lcid = MAKELCID(lang, SORT_DEFAULT);
	if (GetLocaleInfoW(lcid, LOCALE_SLANGUAGE, szLoc, _countof(szLoc)))
	{
		WCHAR sz[MAX_PATH];
		StringCchPrintfW(sz, _countof(sz), L"%s (0x%04X)", szLoc, lang);
		return sz;
	}
	WCHAR sz[64];
	StringCchPrintfW(sz, _countof(sz), L"0x%04X", lang);
	return sz;
}

// ============================================================================
// Internal text edit dialog
// ============================================================================

class CTextEditDlgInternal : public MDialogBase
{
public:
	std::wstring m_wsPrimary;
	std::wstring m_wsSecondary;
	std::wstring m_wsResult;
	HWND m_hParent;
	HWND m_hwndStaticPriLabel;
	HWND m_hwndStaticPrimary;
	HWND m_hwndStaticSecLabel;
	HWND m_hwndEditSecondary;
	HWND m_hwndBtnOK;
	HWND m_hwndBtnCancel;
	HFONT m_hFont;

	CTextEditDlgInternal(const std::wstring& wsPrimary,
	                     const std::wstring& wsSecondary,
	                     HWND hParent)
		: m_wsPrimary(wsPrimary)
		, m_wsSecondary(wsSecondary)
		, m_wsResult(wsSecondary)
		, m_hParent(hParent)
		, m_hFont(NULL)
		, m_hwndStaticPriLabel(NULL)
		, m_hwndStaticPrimary(NULL)
		, m_hwndStaticSecLabel(NULL)
		, m_hwndEditSecondary(NULL)
		, m_hwndBtnOK(NULL)
		, m_hwndBtnCancel(NULL)
	{
	}

	~CTextEditDlgInternal()
	{
		if (m_hFont)
			DeleteObject(m_hFont);
	}

	INT_PTR DoModal()
	{
		WORD buf[256];
		ZeroMemory(buf, sizeof(buf));
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buf;
		pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
		              WS_VISIBLE | WS_THICKFRAME;
		pDlg->cdit = 0;
		pDlg->x = 0; pDlg->y = 0;
		pDlg->cx = 380; pDlg->cy = 260;
		WORD* pw = (WORD*)(pDlg + 1);
		*pw++ = 0; *pw++ = 0;
		LPCWSTR t = L"\x7F16\x8F91\x6587\x672C";
		while (*t) *pw++ = *t++;
		*pw++ = 0;
		return DialogBoxIndirectDx(m_hParent, pDlg);
	}

	virtual INT_PTR CALLBACK
	DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_INITDIALOG:
			{
				m_hFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
					L"Microsoft YaHei UI");
				if (!m_hFont)
					m_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

				RECT rcClient;
				GetClientRect(hwnd, &rcClient);
				int w = rcClient.right - rcClient.left;
				int h = rcClient.bottom - rcClient.top;
				HINSTANCE hInst = GetModuleHandle(NULL);
				int margin = 12, labelH = 24, priH = 90, gap = 10;
				int btnW = 100, btnH = 32;
				int y = margin;
				int right = w - margin;
				int btnY = h - margin - btnH;

				m_hwndStaticPriLabel = CreateWindowExW(0, L"STATIC", L"\x4E3B\x8BED\x8A00\x6587\x672C:",
					WS_CHILD | WS_VISIBLE | SS_LEFT,
					margin, y, right - margin, labelH, hwnd, NULL, hInst, NULL);
				SendMessageW(m_hwndStaticPriLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
				y += labelH + gap;

				m_hwndStaticPrimary = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC",
					m_wsPrimary.c_str(),
					WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
					margin, y, right - margin, priH, hwnd, NULL, hInst, NULL);
				SendMessageW(m_hwndStaticPrimary, WM_SETFONT, (WPARAM)m_hFont, TRUE);
				y += priH + gap;

				m_hwndStaticSecLabel = CreateWindowExW(0, L"STATIC", L"\x6B21\x8BED\x8A00\x6587\x672C:",
					WS_CHILD | WS_VISIBLE | SS_LEFT,
					margin, y, right - margin, labelH, hwnd, NULL, hInst, NULL);
				SendMessageW(m_hwndStaticSecLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
				y += labelH + gap;

				int editH = btnY - y - 20;
				if (editH < 80) editH = 80;

				m_hwndEditSecondary = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
					m_wsSecondary.c_str(),
					WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL |
					ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL,
					margin, y, right - margin, editH, hwnd, (HMENU)1001, hInst, NULL);
				SendMessageW(m_hwndEditSecondary, WM_SETFONT, (WPARAM)m_hFont, TRUE);
				SendMessageW(m_hwndEditSecondary, EM_SETSEL, 0, -1);
				SetFocus(m_hwndEditSecondary);

				int btnX = w - margin - (btnW * 2 + gap);
				m_hwndBtnOK = CreateWindowExW(0, L"BUTTON", L"\x786E\x5B9A",
					WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
					btnX, btnY, btnW, btnH, hwnd, (HMENU)IDOK, hInst, NULL);
				SendMessageW(m_hwndBtnOK, WM_SETFONT, (WPARAM)m_hFont, TRUE);

				m_hwndBtnCancel = CreateWindowExW(0, L"BUTTON", L"\x53D6\x6D88",
					WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
					btnX + btnW + gap, btnY, btnW, btnH, hwnd,
					(HMENU)IDCANCEL, hInst, NULL);
				SendMessageW(m_hwndBtnCancel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

				// Resize
				SetWindowPos(hwnd, NULL, 0, 0, 760, 520, SWP_NOMOVE | SWP_NOZORDER);
				// Center
				RECT rcP;
				if (m_hParent)
					GetWindowRect(m_hParent, &rcP);
				else
				{ rcP.left = 0; rcP.top = 0;
				  rcP.right = GetSystemMetrics(SM_CXSCREEN);
				  rcP.bottom = GetSystemMetrics(SM_CYSCREEN); }
				RECT rcS;
				GetWindowRect(hwnd, &rcS);
				int cx2 = rcS.right - rcS.left;
				int cy2 = rcS.bottom - rcS.top;
				SetWindowPos(hwnd, NULL,
					(rcP.left + rcP.right - cx2) / 2,
					(rcP.top + rcP.bottom - cy2) / 2,
					0, 0, SWP_NOSIZE | SWP_NOZORDER);
			}
			return FALSE; // focus already set

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK)
			{
				int len = GetWindowTextLengthW(m_hwndEditSecondary);
				m_wsResult.resize(len + 1, L'\0');
				GetWindowTextW(m_hwndEditSecondary, &m_wsResult[0], len + 1);
				m_wsResult.resize(len);
				EndDialog(hwnd, IDOK);
			}
			else if (LOWORD(wParam) == IDCANCEL)
			{
				EndDialog(hwnd, IDCANCEL);
			}
			return 0;

		case WM_CLOSE:
			EndDialog(hwnd, IDCANCEL);
			return 0;
		}
		return DefaultProcDx();
	}
};

// ============================================================================
// CRcCompareViewer
// ============================================================================

CRcCompareViewer::CRcCompareViewer(CRcCompareControler* pControler, HWND hParent)
	: m_pControler(pControler)
	, m_hParent(hParent)
	, m_nCurrentAddedIndex(-1)
	, m_nCurrentTaskIndex(-1)
	, m_eFilterMode(FILTER_ALL)
	, m_bUpdatingUI(false)
	, m_bShowPrimary(false)
	, m_hFont(NULL)
	, m_hwndToolbarHost(NULL)
	, m_hwndTreeResources(NULL)
	, m_hwndCodeEditor(NULL)
	, m_hwndListItems(NULL)
	, m_hwndBtnNextUnmod(NULL)
	, m_hwndBtnPrevUnmod(NULL)
	, m_hwndBtnExportRc(NULL)
	, m_hwndBtnSaveRes(NULL)
	, m_hwndStaticProgress(NULL)
	, m_hwndComboTask(NULL)
	, m_hwndComboFilter(NULL)
	, m_hwndStaticTaskLabel(NULL)
	, m_hwndStaticFilterLabel(NULL)
	, m_hwndBtnAcceptAll(NULL)
	, m_hwndBtnMarkAllDone(NULL)
	, m_hwndStaticPrimaryLang(NULL)
	, m_hwndComboPrimaryLang(NULL)
	, m_hwndStaticSecondaryLang(NULL)
	, m_hwndComboSecondaryLang(NULL)
	, m_hwndChkShowPrimary(NULL)
	, m_hwndPreviewHost(NULL)
	, m_hwndBottomPanel(NULL)
{
}

CRcCompareViewer::~CRcCompareViewer()
{
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = NULL;
	}
}

// Show viewer window with its own message loop (modal-like)
INT CRcCompareViewer::DoViewerWindow()
{
	// "资源比对查看器"
	LPCWSTR pszTitle = L"\x8D44\x6E90\x6BD4\x5BF9\x67E5\x770B\x5668";
	DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
	DWORD exStyle = WS_EX_APPWINDOW;

	if (!CreateWindowDx(m_hParent, pszTitle, style, exStyle))
		return -1;

	ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
	UpdateWindow(m_hwnd);

	// Disable parent to simulate modal behavior
	BOOL bParentEnabled = FALSE;
	if (m_hParent)
	{
		bParentEnabled = IsWindowEnabled(m_hParent);
		if (bParentEnabled)
			EnableWindow(m_hParent, FALSE);
	}

	// Run own message loop
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0))
	{
		if (!IsWindow(m_hwnd))
			break;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	// Re-enable parent
	if (m_hParent && bParentEnabled)
	{
		EnableWindow(m_hParent, TRUE);
		SetForegroundWindow(m_hParent);
	}

	return 0;
}

void CRcCompareViewer::SetCompareControler(CRcCompareControler* pControler)
{
	m_pControler = pControler;
	if (m_hwnd)
		RefreshAddedItemsList();
}

// ============================================================================
// Window procedure
// ============================================================================

LRESULT CALLBACK
CRcCompareViewer::WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		HANDLE_MSG(hwnd, WM_CREATE, OnCreate);
		HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);
	case WM_SIZE:
		OnSize(hwnd, LOWORD(lParam), HIWORD(lParam));
		return 0;
	case WM_CLOSE:
		OnClose(hwnd);
		return 0;
	case WM_COMMAND:
		OnCommand(hwnd, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
		return 0;
	case WM_NOTIFY:
		return OnNotify(hwnd, (int)wParam, (NMHDR*)lParam);
	case WM_TIMER:
		OnTimer((UINT_PTR)wParam);
		return 0;
	}
	return DefaultProcDx(hwnd, uMsg, wParam, lParam);
}

// ============================================================================
// WM_CREATE / WM_DESTROY
// ============================================================================

BOOL CRcCompareViewer::OnCreate(HWND hwnd, LPCREATESTRUCT lpcs)
{
	m_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	if (!m_hFont)
		m_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	CreateViewerControls();

	// Wire up modular components
	m_navTree.SetTreeView(m_hwndTreeResources);
	m_viewRenderer.SetHostWindows(m_hwndCodeEditor, m_hwndPreviewHost);

	RefreshTaskCombo();

	// Switch to the current task (wires EntrySet + runs comparison)
	int idx = m_pControler ? m_pControler->GetCurrentTaskIndex() : -1;
	if (idx >= 0 && m_pControler->GetTaskData(idx))
	{
		SwitchToTask(idx);
	}
	else
	{
		// Non-task mode: use whatever EntrySet is already set
		AutoDetectAndSetLanguages();
		if (m_pControler)
			m_pControler->DoCompare();
		BuildResourceTree();
		RefreshAddedItemsList();
		UpdateProgressDisplay();
	}

	SetTimer(hwnd, TIMER_UPDATE_PROGRESS, 1000, NULL);

	// Auto-navigate to first unmodified
	if (m_pControler && m_pControler->GetUnmodifiedCount() > 0)
		NavigateToNextUnmodified();

	return TRUE;
}

void CRcCompareViewer::OnDestroy(HWND hwnd)
{
	KillTimer(hwnd, TIMER_UPDATE_PROGRESS);
	PostQuitMessage(0);
}

void CRcCompareViewer::OnClose(HWND hwnd)
{
	if (!CanClose())
	{
		// "部分项目尚未翻译。仍然关闭？"
		int res = MessageBoxW(hwnd,
			L"\x90E8\x5206\x9879\x76EE\x5C1A\x672A\x7FFB\x8BD1\x3002\x4ECD\x7136\x5173\x95ED\xFF1F",
			L"\x786E\x8BA4\x5173\x95ED", MB_YESNO | MB_ICONQUESTION);
		if (res != IDYES)
			return;
	}
	DestroyWindow(hwnd);
}

void CRcCompareViewer::OnSize(HWND hwnd, int cx, int cy)
{
	if (!m_hwndToolbarHost || !m_splitter_main.m_hwnd)
		return;

	// Toolbar host at top
	const int TOOLBAR_H = 90;  // 3 rows: task/filter, lang, buttons
	MoveWindow(m_hwndToolbarHost, 0, 0, cx, TOOLBAR_H, TRUE);

	// Main splitter fills remaining area
	int splitterY = TOOLBAR_H;
	int splitterH = cy - splitterY;
	if (splitterH < 100) splitterH = 100;
	MoveWindow(m_splitter_main, 0, splitterY, cx, splitterH, TRUE);

	// Layout toolbar controls
	const int M = 6, CH = 24, BH = 28, GAP = 5;
	int y = M;
	int x = M;

	// Row 1: Task, Filter, Progress
	// "任务:"
	MoveWindow(m_hwndStaticTaskLabel, x, y + 3, 40, CH, TRUE); x += 44;
	MoveWindow(m_hwndComboTask, x, y, 160, 200, TRUE); x += 168;
	// "筛选:"
	MoveWindow(m_hwndStaticFilterLabel, x, y + 3, 40, CH, TRUE); x += 44;
	MoveWindow(m_hwndComboFilter, x, y, 110, 200, TRUE); x += 118;
	MoveWindow(m_hwndStaticProgress, x, y + 3, 160, CH, TRUE);
	y += CH + GAP;

	// Row 2: Language combos + checkbox
	x = M;
	// "主语言:"
	MoveWindow(m_hwndStaticPrimaryLang, x, y + 3, 55, CH, TRUE); x += 58;
	MoveWindow(m_hwndComboPrimaryLang, x, y, 150, 200, TRUE); x += 158;
	// "次语言:"
	MoveWindow(m_hwndStaticSecondaryLang, x, y + 3, 55, CH, TRUE); x += 58;
	MoveWindow(m_hwndComboSecondaryLang, x, y, 150, 200, TRUE); x += 158;
	// "显示主语言界面(只读)"
	MoveWindow(m_hwndChkShowPrimary, x, y + 2, 180, CH, TRUE);
	y += CH + GAP;

	// Row 3: Buttons
	x = M;
	int bw = 90;
	MoveWindow(m_hwndBtnPrevUnmod, x, y, bw, BH, TRUE); x += bw + 4;
	MoveWindow(m_hwndBtnNextUnmod, x, y, bw, BH, TRUE); x += bw + 10;
	MoveWindow(m_hwndBtnAcceptAll, x, y, 140, BH, TRUE); x += 144;
	MoveWindow(m_hwndBtnMarkAllDone, x, y, 120, BH, TRUE); x += 130;
	MoveWindow(m_hwndBtnExportRc, x, y, 80, BH, TRUE); x += 84;
	MoveWindow(m_hwndBtnSaveRes, x, y, 80, BH, TRUE);
}

// ============================================================================
// WM_COMMAND
// ============================================================================

void CRcCompareViewer::OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDC_BTN_NEXT_UNMOD:       OnBtnNextUnmodified(); break;
	case IDC_BTN_PREV_UNMOD:       OnBtnPrevUnmodified(); break;
	case IDC_BTN_EXPORT_RC:        OnBtnExportRc(); break;
	case IDC_BTN_SAVE_RES:         OnBtnSave(); break;
	case IDC_BTN_ACCEPT_ALL_PRIMARY: OnBtnAcceptAllPrimary(); break;
	case IDC_BTN_MARK_ALL_DONE:    OnBtnMarkAllDone(); break;
	case IDC_CHK_SHOW_PRIMARY:     OnChkShowPrimary(); break;
	case IDC_COMBO_TASK:
		if (codeNotify == CBN_SELCHANGE)
			OnComboTaskSelChange();
		break;
	case IDC_COMBO_FILTER:
		if (codeNotify == CBN_SELCHANGE)
			OnComboFilterSelChange();
		break;
	case IDC_COMBO_PRIMARY_LANG:
		if (codeNotify == CBN_SELCHANGE)
			OnComboPrimaryLangSelChange();
		break;
	case IDC_COMBO_SECONDARY_LANG:
		if (codeNotify == CBN_SELCHANGE)
			OnComboSecondaryLangSelChange();
		break;
	}
}

// ============================================================================
// WM_NOTIFY
// ============================================================================

LRESULT CRcCompareViewer::OnNotify(HWND hwnd, int idFrom, NMHDR* pNMHDR)
{
	if (idFrom == IDC_LIST_ADDED_ITEMS)
	{
		if (pNMHDR->code == NM_DBLCLK)
		{
			LPNMITEMACTIVATE pNMIA = (LPNMITEMACTIVATE)pNMHDR;
			if (pNMIA->iItem >= 0)
				BeginEditSecondaryText(pNMIA->iItem);
			return 0;
		}
		else if (pNMHDR->code == LVN_ITEMCHANGED)
		{
			LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)pNMHDR;
			if ((pNMLV->uNewState & LVIS_SELECTED) && !m_bUpdatingUI)
			{
				if (pNMLV->iItem >= 0 && pNMLV->iItem < (int)m_vecFilteredIndices.size())
				{
					m_nCurrentAddedIndex = m_vecFilteredIndices[pNMLV->iItem];
					// Navigate tree to matching resource
					const auto* pItem = m_pControler->GetResult(m_nCurrentAddedIndex);
					if (pItem)
						NavigateTreeToCompareItem(*pItem);
				}
			}
			return 0;
		}
	}
	else if (idFrom == IDC_TREE_RESOURCES)
	{
		if (pNMHDR->code == TVN_SELCHANGEDW)
		{
			OnTreeSelChanged((NMTREEVIEWW*)pNMHDR);
			return 0;
		}
	}
	return 0;
}

void CRcCompareViewer::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_UPDATE_PROGRESS)
		UpdateProgressDisplay();
}

// ============================================================================
// Control creation
// ============================================================================

void CRcCompareViewer::CreateViewerControls()
{
	HWND hwnd = m_hwnd;
	HINSTANCE hInst = GetModuleHandle(NULL);
	DWORD style;

	// --- Toolbar host (a simple STATIC container at the top) ---
	m_hwndToolbarHost = CreateWindowExW(0, L"STATIC", NULL,
		WS_CHILD | WS_VISIBLE | SS_SIMPLE,
		0, 0, 100, 90, hwnd, (HMENU)(UINT_PTR)IDC_TOOLBAR_HOST, hInst, NULL);

	// Toolbar controls (children of the toolbar host)
	HWND tb = m_hwndToolbarHost;

	// Task label + combo
	// "任务:"
	m_hwndStaticTaskLabel = CreateWindowExW(0, L"STATIC", L"\x4EFB\x52A1:",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 40, 20, tb, (HMENU)(UINT_PTR)IDC_STATIC_TASK_LABEL, hInst, NULL);
	SendMessageW(m_hwndStaticTaskLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndComboTask = CreateWindowExW(0, L"COMBOBOX", NULL,
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		0, 0, 160, 200, tb, (HMENU)(UINT_PTR)IDC_COMBO_TASK, hInst, NULL);
	SendMessageW(m_hwndComboTask, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Filter label + combo
	// "筛选:"
	m_hwndStaticFilterLabel = CreateWindowExW(0, L"STATIC", L"\x7B5B\x9009:",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 40, 20, tb, (HMENU)(UINT_PTR)IDC_STATIC_FILTER_LABEL, hInst, NULL);
	SendMessageW(m_hwndStaticFilterLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndComboFilter = CreateWindowExW(0, L"COMBOBOX", NULL,
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		0, 0, 110, 200, tb, (HMENU)(UINT_PTR)IDC_COMBO_FILTER, hInst, NULL);
	SendMessageW(m_hwndComboFilter, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	// "全部", "待处理", "已翻译"
	SendMessageW(m_hwndComboFilter, CB_ADDSTRING, 0, (LPARAM)L"\x5168\x90E8");
	SendMessageW(m_hwndComboFilter, CB_ADDSTRING, 0, (LPARAM)L"\x5F85\x5904\x7406");
	SendMessageW(m_hwndComboFilter, CB_ADDSTRING, 0, (LPARAM)L"\x5DF2\x7FFB\x8BD1");
	SendMessageW(m_hwndComboFilter, CB_SETCURSEL, FILTER_ALL, 0);

	// Primary language
	// "主语言:"
	m_hwndStaticPrimaryLang = CreateWindowExW(0, L"STATIC", L"\x4E3B\x8BED\x8A00:",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 55, 20, tb, (HMENU)(UINT_PTR)IDC_STATIC_PRIMARY_LANG, hInst, NULL);
	SendMessageW(m_hwndStaticPrimaryLang, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndComboPrimaryLang = CreateWindowExW(0, L"COMBOBOX", NULL,
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		0, 0, 150, 200, tb, (HMENU)(UINT_PTR)IDC_COMBO_PRIMARY_LANG, hInst, NULL);
	SendMessageW(m_hwndComboPrimaryLang, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Secondary language
	// "次语言:"
	m_hwndStaticSecondaryLang = CreateWindowExW(0, L"STATIC", L"\x6B21\x8BED\x8A00:",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 55, 20, tb, (HMENU)(UINT_PTR)IDC_STATIC_SECONDARY_LANG, hInst, NULL);
	SendMessageW(m_hwndStaticSecondaryLang, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndComboSecondaryLang = CreateWindowExW(0, L"COMBOBOX", NULL,
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		0, 0, 150, 200, tb, (HMENU)(UINT_PTR)IDC_COMBO_SECONDARY_LANG, hInst, NULL);
	SendMessageW(m_hwndComboSecondaryLang, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// "显示主语言界面(只读)"
	m_hwndChkShowPrimary = CreateWindowExW(0, L"BUTTON",
		L"\x663E\x793A\x4E3B\x8BED\x8A00\x754C\x9762(\x53EA\x8BFB)",
		WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
		0, 0, 180, 20, tb, (HMENU)(UINT_PTR)IDC_CHK_SHOW_PRIMARY, hInst, NULL);
	SendMessageW(m_hwndChkShowPrimary, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Progress
	m_hwndStaticProgress = CreateWindowExW(0, L"STATIC", L"0 / 0 \x5B8C\x6210",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 160, 20, tb, (HMENU)(UINT_PTR)IDC_STATIC_PROGRESS, hInst, NULL);
	SendMessageW(m_hwndStaticProgress, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Buttons (children of toolbar host)
	auto MakeBtn = [&](LPCWSTR text, int id) -> HWND
	{
		HWND h = CreateWindowExW(0, L"BUTTON", text,
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			0, 0, 90, 25, tb, (HMENU)(UINT_PTR)id, hInst, NULL);
		SendMessageW(h, WM_SETFONT, (WPARAM)m_hFont, TRUE);
		return h;
	};
	// "<< 上一个", "下一个 >>"
	m_hwndBtnPrevUnmod   = MakeBtn(L"<< \x4E0A\x4E00\x4E2A", IDC_BTN_PREV_UNMOD);
	m_hwndBtnNextUnmod   = MakeBtn(L"\x4E0B\x4E00\x4E2A >>", IDC_BTN_NEXT_UNMOD);
	// "导出", "保存"
	m_hwndBtnExportRc    = MakeBtn(L"\x5BFC\x51FA", IDC_BTN_EXPORT_RC);
	m_hwndBtnSaveRes     = MakeBtn(L"\x4FDD\x5B58", IDC_BTN_SAVE_RES);
	// "接受所有主语言", "全部标记完成"
	m_hwndBtnAcceptAll   = MakeBtn(L"\x63A5\x53D7\x6240\x6709\x4E3B\x8BED\x8A00", IDC_BTN_ACCEPT_ALL_PRIMARY);
	m_hwndBtnMarkAllDone = MakeBtn(L"\x5168\x90E8\x6807\x8BB0\x5B8C\x6210", IDC_BTN_MARK_ALL_DONE);

	// --- Main splitter: top(resource area) / bottom(compare results) ---
	style = WS_CHILD | WS_VISIBLE | SWS_VERT | SWS_TOPALIGN;
	m_splitter_main.CreateDx(hwnd, 2, style);
	m_splitter_main.SetPaneMinExtent(0, 200);
	m_splitter_main.SetPaneMinExtent(1, 150);

	// --- Top splitter: left(tree) / right(code+preview) ---
	style = WS_CHILD | WS_VISIBLE | SWS_HORZ | SWS_LEFTALIGN;
	m_splitter_top.CreateDx(m_splitter_main, 2, style);
	m_splitter_top.SetPaneMinExtent(0, 150);
	m_splitter_top.SetPaneMinExtent(1, 200);

	// --- Inner splitter: top(dialog preview - primary) / bottom(code editor - secondary) ---
	style = WS_CHILD | WS_VISIBLE | SWS_VERT | SWS_TOPALIGN;
	m_splitter_inner.CreateDx(m_splitter_top, 2, style);
	m_splitter_inner.SetPaneMinExtent(0, 100);
	m_splitter_inner.SetPaneMinExtent(1, 60);

	// --- TreeView (left pane of top splitter) ---
	style = WS_CHILD | WS_VISIBLE | WS_BORDER |
	        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
	m_hwndTreeResources = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, NULL,
		style, 0, 0, 250, 400, m_splitter_top,
		(HMENU)(UINT_PTR)IDC_TREE_RESOURCES, hInst, NULL);
	SendMessageW(m_hwndTreeResources, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// --- Code editor (top pane of inner splitter) ---
	m_hwndCodeEditor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
		0, 0, 400, 200, m_splitter_inner,
		(HMENU)(UINT_PTR)IDC_CODE_EDITOR, hInst, NULL);
	{
		HFONT hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
		if (!hMono)
			hMono = (HFONT)GetStockObject(ANSI_FIXED_FONT);
		SendMessageW(m_hwndCodeEditor, WM_SETFONT, (WPARAM)hMono, TRUE);
	}

	// --- Dialog preview host (bottom pane of inner splitter) ---
	m_hwndPreviewHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", NULL,
		WS_CHILD | WS_VISIBLE | SS_SIMPLE,
		0, 0, 400, 200, m_splitter_inner,
		(HMENU)(UINT_PTR)IDC_PREVIEW_HOST, hInst, NULL);

	// --- Bottom panel (container for compare results ListView) ---
	// This is a simple STATIC that holds the ListView. It becomes
	// the bottom pane of m_splitter_main.
	m_hwndBottomPanel = CreateWindowExW(0, L"STATIC", NULL,
		WS_CHILD | WS_VISIBLE | SS_SIMPLE | WS_CLIPCHILDREN,
		0, 0, 400, 200, m_splitter_main, NULL, hInst, NULL);

	// --- Compare results ListView (child of bottom panel) ---
	m_hwndListItems = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
		WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 400, 200, m_hwndBottomPanel,
		(HMENU)(UINT_PTR)IDC_LIST_ADDED_ITEMS, hInst, NULL);
	ListView_SetExtendedListViewStyle(m_hwndListItems,
		LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	SendMessageW(m_hwndListItems, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	LVCOLUMNW lvc;
	ZeroMemory(&lvc, sizeof(lvc));
	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
	lvc.fmt = LVCFMT_LEFT;
	// "类型"
	lvc.pszText = const_cast<LPWSTR>(L"\x7C7B\x578B"); lvc.cx = 70;
	ListView_InsertColumn(m_hwndListItems, COL_TYPE, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"ID"); lvc.cx = 100;
	ListView_InsertColumn(m_hwndListItems, COL_ID, &lvc);
	// "主语言文本"
	lvc.pszText = const_cast<LPWSTR>(L"\x4E3B\x8BED\x8A00\x6587\x672C"); lvc.cx = 200;
	ListView_InsertColumn(m_hwndListItems, COL_PRIMARY_TEXT, &lvc);
	// "次语言文本"
	lvc.pszText = const_cast<LPWSTR>(L"\x6B21\x8BED\x8A00\x6587\x672C"); lvc.cx = 200;
	ListView_InsertColumn(m_hwndListItems, COL_SECONDARY_TEXT, &lvc);
	// "状态"
	lvc.pszText = const_cast<LPWSTR>(L"\x72B6\x6001"); lvc.cx = 80;
	ListView_InsertColumn(m_hwndListItems, COL_STATUS, &lvc);

	// --- Wire up splitter panes ---
	m_splitter_top.SetPane(0, m_hwndTreeResources);
	m_splitter_top.SetPane(1, m_splitter_inner);
	m_splitter_top.SetPaneExtent(0, 220);

	// Preview host on top (primary display), code editor on bottom (secondary)
	m_splitter_inner.SetPane(0, m_hwndPreviewHost);
	m_splitter_inner.SetPane(1, m_hwndCodeEditor);

	m_splitter_main.SetPane(0, m_splitter_top);
	m_splitter_main.SetPane(1, m_hwndBottomPanel);

	// Initial proportions: resource area maximized, list area compact
	m_splitter_main.SetPaneExtent(1, 180);
	// Preview host gets most of the inner space
	m_splitter_inner.SetPaneExtent(1, 150);

	// Subclass toolbar host to handle WM_CTLCOLORSTATIC for checkbox painting
	SetWindowSubclass(m_hwndToolbarHost, ToolbarHostSubclassProc, 0, 0);

	// Subclass bottom panel to auto-resize the child ListView
	SetWindowSubclass(m_hwndBottomPanel, BottomPanelSubclassProc, 0,
		(DWORD_PTR)m_hwndListItems);
}

// ============================================================================
// Data display
// ============================================================================

void CRcCompareViewer::RefreshAddedItemsList()
{
	if (!m_hwndListItems || !m_pControler)
		return;

	m_bUpdatingUI = true;
	ListView_DeleteAllItems(m_hwndListItems);
	m_vecFilteredIndices.clear();

	const auto& results = m_pControler->GetResults();
	for (int i = 0; i < (int)results.size(); ++i)
	{
		const auto& item = results[i];

		// Apply filter
		bool bShow = true;
		switch (m_eFilterMode)
		{
		case FILTER_PENDING:
			bShow = !item.bModified;
			break;
		case FILTER_TRANSLATED:
			bShow = item.bModified;
			break;
		default:
			break;
		}
		if (!bShow)
			continue;

		int nRow = (int)m_vecFilteredIndices.size();
		m_vecFilteredIndices.push_back(i);

		// Type
		LPCWSTR pszType = L"";
		switch (item.eType)
		{
		case CIT_CONTROL:        pszType = L"\x63A7\x4EF6"; break;   // "控件"
		case CIT_STRING:         pszType = L"\x5B57\x7B26\x4E32"; break; // "字符串"
		case CIT_MENU:           pszType = L"\x83DC\x5355"; break;   // "菜单"
		case CIT_DIALOG_CAPTION: pszType = L"\x6807\x9898"; break;   // "标题"
		case CIT_RESOURCE:       pszType = L"\x8D44\x6E90"; break;   // "资源"
		}

		LVITEMW lvi;
		ZeroMemory(&lvi, sizeof(lvi));
		lvi.mask = LVIF_TEXT | LVIF_PARAM;
		lvi.iItem = nRow;
		lvi.lParam = (LPARAM)i;
		lvi.pszText = const_cast<LPWSTR>(pszType);
		int idx = ListView_InsertItem(m_hwndListItems, &lvi);

		// ID
		WCHAR szID[128] = { 0 };
		switch (item.eType)
		{
		case CIT_CONTROL:
			wsprintfW(szID, L"Dlg:%u Ctrl:%u", item.nDialogID, item.nControlID);
			break;
		case CIT_STRING:
			wsprintfW(szID, L"Str:%u", item.nStringID);
			break;
		case CIT_MENU:
			wsprintfW(szID, L"Menu:%u", item.nMenuID);
			break;
		case CIT_DIALOG_CAPTION:
			wsprintfW(szID, L"Dlg:%u", item.nDialogID);
			break;
		case CIT_RESOURCE:
			StringCchPrintfW(szID, _countof(szID), L"%s / %s",
				item.wsResType.c_str(), item.wsResName.c_str());
			break;
		}
		ListView_SetItemText(m_hwndListItems, idx, COL_ID, szID);

		// Primary text
		ListView_SetItemText(m_hwndListItems, idx, COL_PRIMARY_TEXT,
			const_cast<LPWSTR>(item.wsPrimaryText.c_str()));

		// Secondary text
		ListView_SetItemText(m_hwndListItems, idx, COL_SECONDARY_TEXT,
			const_cast<LPWSTR>(item.wsSecondaryText.c_str()));

		// Status
		LPCWSTR pszStatus;
		if (item.eType == CIT_RESOURCE)
		{
			// "新增" / "已变更"
			pszStatus = (item.eStatus == CIS_ADDED)
				? L"\x65B0\x589E" : L"\x5DF2\x53D8\x66F4";
		}
		else
		{
			// "完成" / "待处理"
			pszStatus = item.bModified ? L"\x5B8C\x6210" : L"\x5F85\x5904\x7406";
		}
		ListView_SetItemText(m_hwndListItems, idx, COL_STATUS,
			const_cast<LPWSTR>(pszStatus));
	}

	m_bUpdatingUI = false;

	// Resize ListView to fill bottom panel
	if (m_hwndBottomPanel)
	{
		RECT rc;
		GetClientRect(m_hwndBottomPanel, &rc);
		MoveWindow(m_hwndListItems, 0, 0, rc.right, rc.bottom, TRUE);
	}
}

void CRcCompareViewer::UpdateProgressDisplay()
{
	if (!m_hwndStaticProgress || !m_pControler)
		return;
	SetWindowTextW(m_hwndStaticProgress, GetProgressText().c_str());
}

std::wstring CRcCompareViewer::GetProgressText() const
{
	if (!m_pControler) return L"N/A";

	int total = m_pControler->GetResultCount();
	int unmod = m_pControler->GetUnmodifiedCount();
	int done = total - unmod;

	WCHAR szBuf[128];
	wsprintfW(szBuf, L"%d / %d \x5B8C\x6210", done, total);
	return szBuf;
}

bool CRcCompareViewer::CanClose() const
{
	if (!m_pControler) return true;
	return m_pControler->IsAllModified();
}

void CRcCompareViewer::UpdateListItemDisplay(int nAddedIndex)
{
	if (!m_hwndListItems || !m_pControler)
		return;

	const auto* pItem = m_pControler->GetResult(nAddedIndex);
	if (!pItem) return;

	for (int row = 0; row < (int)m_vecFilteredIndices.size(); ++row)
	{
		if (m_vecFilteredIndices[row] == nAddedIndex)
		{
			ListView_SetItemText(m_hwndListItems, row, COL_SECONDARY_TEXT,
				const_cast<LPWSTR>(pItem->wsSecondaryText.c_str()));
			LPCWSTR pszStatus = pItem->bModified ? L"\x5B8C\x6210" : L"\x5F85\x5904\x7406";
			ListView_SetItemText(m_hwndListItems, row, COL_STATUS,
				const_cast<LPWSTR>(pszStatus));
			break;
		}
	}
}

// ============================================================================
// Navigation
// ============================================================================

bool CRcCompareViewer::NavigateToAddedItem(int nIndex)
{
	if (!m_hwndListItems || !m_pControler)
		return false;

	if (nIndex < 0 || nIndex >= m_pControler->GetResultCount())
		return false;

	m_nCurrentAddedIndex = nIndex;

	for (int row = 0; row < (int)m_vecFilteredIndices.size(); ++row)
	{
		if (m_vecFilteredIndices[row] == nIndex)
		{
			m_bUpdatingUI = true;
			ListView_SetItemState(m_hwndListItems, row,
				LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			ListView_EnsureVisible(m_hwndListItems, row, FALSE);
			m_bUpdatingUI = false;
			return true;
		}
	}
	return false;
}

bool CRcCompareViewer::NavigateToNextUnmodified()
{
	if (!m_pControler) return false;
	int idx = m_pControler->GetNextUnmodifiedIndex(m_nCurrentAddedIndex);
	if (idx < 0) return false;
	return NavigateToAddedItem(idx);
}

bool CRcCompareViewer::NavigateToPrevUnmodified()
{
	if (!m_pControler) return false;

	const auto& results = m_pControler->GetResults();
	int start = m_nCurrentAddedIndex - 1;
	if (start < 0) start = (int)results.size() - 1;

	for (int i = start; i >= 0; --i)
	{
		if (!results[i].bModified)
			return NavigateToAddedItem(i);
	}
	for (int i = (int)results.size() - 1; i > start; --i)
	{
		if (!results[i].bModified)
			return NavigateToAddedItem(i);
	}
	return false;
}

// ============================================================================
// Task / filter management
// ============================================================================

void CRcCompareViewer::RefreshTaskCombo()
{
	if (!m_hwndComboTask || !m_pControler) return;

	SendMessageW(m_hwndComboTask, CB_RESETCONTENT, 0, 0);
	const auto& proj = m_pControler->GetProject();
	for (int i = 0; i < (int)proj.tasks.size(); ++i)
	{
		std::wstring wsName = proj.tasks[i].taskName;
		if (wsName.empty())
		{
			WCHAR szBuf[32];
			wsprintfW(szBuf, L"Task %d", i + 1);
			wsName = szBuf;
		}
		SendMessageW(m_hwndComboTask, CB_ADDSTRING, 0, (LPARAM)wsName.c_str());
	}
	if (proj.tasks.size() > 0)
		SendMessageW(m_hwndComboTask, CB_SETCURSEL, 0, 0);
}

void CRcCompareViewer::SwitchToTask(int nTaskIndex)
{
	if (!m_pControler) return;
	m_nCurrentTaskIndex = nTaskIndex;
	m_pControler->SetCurrentTaskIndex(nTaskIndex);

	// Wire up the task's EntrySet data to the controller
	auto* pData = m_pControler->GetTaskData(nTaskIndex);
	if (pData && pData->bLoaded)
	{
		m_pControler->SetPrimaryEntrySet(&pData->primaryRes);
		m_pControler->SetSecondaryEntrySet(&pData->secondaryRes);
	}

	AutoDetectAndSetLanguages();

	// Run comparison for this task
	m_pControler->DoCompare();

	BuildResourceTree();
	RefreshAddedItemsList();
	UpdateProgressDisplay();
}

void CRcCompareViewer::OnComboTaskSelChange()
{
	int sel = (int)SendMessageW(m_hwndComboTask, CB_GETCURSEL, 0, 0);
	if (sel >= 0)
		SwitchToTask(sel);
}

void CRcCompareViewer::OnComboFilterSelChange()
{
	int sel = (int)SendMessageW(m_hwndComboFilter, CB_GETCURSEL, 0, 0);
	if (sel >= 0)
	{
		m_eFilterMode = (EFilterMode)sel;
		RefreshAddedItemsList();
	}
}

void CRcCompareViewer::ApplyFilter()
{
	RefreshAddedItemsList();
}

// ============================================================================
// Text editing
// ============================================================================

void CRcCompareViewer::BeginEditSecondaryText(int nListIndex)
{
	if (nListIndex < 0 || nListIndex >= (int)m_vecFilteredIndices.size())
		return;

	int resultIdx = m_vecFilteredIndices[nListIndex];
	const auto* pItem = m_pControler->GetResult(resultIdx);
	if (!pItem) return;

	std::wstring wsNew = ShowTextEditDialog(
		pItem->wsPrimaryText, pItem->wsSecondaryText, L"Edit Translation");

	if (wsNew != pItem->wsSecondaryText)
	{
		m_pControler->UpdateItemText(resultIdx, wsNew);
		m_pControler->SetItemModified(resultIdx, true);
		UpdateListItemDisplay(resultIdx);
		UpdateProgressDisplay();
	}
}

std::wstring CRcCompareViewer::ShowTextEditDialog(
	const std::wstring& wsPrimary,
	const std::wstring& wsCurrent,
	const std::wstring& wsTitle)
{
	CTextEditDlgInternal dlg(wsPrimary, wsCurrent, m_hwnd);
	if (dlg.DoModal() == IDOK)
		return dlg.m_wsResult;
	return wsCurrent;
}

// ============================================================================
// Button handlers
// ============================================================================

void CRcCompareViewer::OnBtnNextUnmodified()
{
	if (!NavigateToNextUnmodified())
		MessageBoxW(m_hwnd, L"\x6240\x6709\x9879\x76EE\x5DF2\x7FFB\x8BD1\x3002",
			L"\x63D0\x793A", MB_ICONINFORMATION);
}

void CRcCompareViewer::OnBtnPrevUnmodified()
{
	if (!NavigateToPrevUnmodified())
		MessageBoxW(m_hwnd, L"\x6240\x6709\x9879\x76EE\x5DF2\x7FFB\x8BD1\x3002",
			L"\x63D0\x793A", MB_ICONINFORMATION);
}

void CRcCompareViewer::OnBtnExportRc()
{
	if (!m_pControler) return;

	WCHAR szFile[MAX_PATH] = { 0 };
	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"\x53EF\x6267\x884C\x6587\x4EF6 (*.exe;*.dll)\0*.exe;*.dll\0\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"\x5BFC\x51FA\x6B21\x8BED\x8A00 PE";
	ofn.lpstrDefExt = L"dll";
	ofn.Flags = OFN_OVERWRITEPROMPT;

	if (GetSaveFileNameW(&ofn))
	{
		if (m_pControler->SaveSecondary(szFile))
			MessageBoxW(m_hwnd, L"\x5BFC\x51FA\x6210\x529F\x3002",
				L"\x5BFC\x51FA", MB_ICONINFORMATION);
		else
			MessageBoxW(m_hwnd, L"\x5BFC\x51FA\x5931\x8D25\x3002",
				L"\x9519\x8BEF", MB_ICONERROR);
	}
}

void CRcCompareViewer::OnBtnSave()
{
	if (!m_pControler) return;

	SMergeResult result = m_pControler->ApplyChanges();
	WCHAR szMsg[256];
	wsprintfW(szMsg, L"\x5408\x5E76\x5B8C\x6210: %d \x5DF2\x66F4\x65B0\xFF0C%d \x5DF2\x63D2\x5165\x3002",
		result.nUpdatedControls + result.nUpdatedStrings + result.nUpdatedMenuItems + result.nUpdatedCaptions,
		result.nInsertedControls + result.nInsertedStrings + result.nInsertedMenuItems);
	MessageBoxW(m_hwnd, szMsg, L"\x4FDD\x5B58", MB_ICONINFORMATION);
}

void CRcCompareViewer::OnBtnAcceptAllPrimary()
{
	if (!m_pControler) return;

	const auto& results = m_pControler->GetResults();
	for (int i = 0; i < (int)results.size(); ++i)
	{
		if (!results[i].bModified)
		{
			m_pControler->UpdateItemText(i, results[i].wsPrimaryText);
			m_pControler->SetItemModified(i, true);
		}
	}
	RefreshAddedItemsList();
	UpdateProgressDisplay();
}

void CRcCompareViewer::OnBtnMarkAllDone()
{
	if (!m_pControler) return;

	const auto& results = m_pControler->GetResults();
	for (int i = 0; i < (int)results.size(); ++i)
	{
		if (!results[i].bModified)
			m_pControler->SetItemModified(i, true);
	}
	RefreshAddedItemsList();
	UpdateProgressDisplay();
}

// ============================================================================
// Resource tree
// ============================================================================

// ============================================================================
// Resource tree (delegated to NavigationTree module)
// ============================================================================

void CRcCompareViewer::ClearResourceTree()
{
	m_navTree.Clear();
}

void CRcCompareViewer::BuildResourceTree()
{
	m_navTree.Clear();

	if (!m_pControler) return;

	// Build tree from the active EntrySet
	EntrySet* pRes = m_bShowPrimary
		? m_pControler->GetPrimaryEntrySet()
		: m_pControler->GetSecondaryEntrySet();

	if (!pRes) return;

	// Get the language filter from the combo; if "(自动)" is selected (0),
	// use the controller's effective language so we show just one language.
	LANGID langFilter = m_bShowPrimary
		? GetSelectedPrimaryLang() : GetSelectedSecondaryLang();
	if (langFilter == 0)
	{
		const SLanguageContext& ctx = m_bShowPrimary
			? m_pControler->GetPrimaryContext()
			: m_pControler->GetSecondaryContext();
		if (ctx.IsResolved())
			langFilter = ctx.wEffectiveLang;
	}

	m_navTree.Build(*pRes, langFilter);
}

void CRcCompareViewer::OnTreeSelChanged(NMTREEVIEWW* pNMTV)
{
	EntryBase* pEntry = m_navTree.OnSelectionChanged(pNMTV);
	if (pEntry)
		PreviewEntry(pEntry);
	else
		m_viewRenderer.Clear();
}

// ============================================================================
// Resource preview (delegated to ViewRenderer module)
// ============================================================================

void CRcCompareViewer::PreviewEntry(EntryBase* pEntry)
{
	m_viewRenderer.RenderEntry(pEntry);
}

// ============================================================================
// Language management
// ============================================================================

void CRcCompareViewer::PopulateLanguageCombos()
{
	if (!m_hwndComboPrimaryLang || !m_hwndComboSecondaryLang)
		return;

	SendMessageW(m_hwndComboPrimaryLang, CB_RESETCONTENT, 0, 0);
	SendMessageW(m_hwndComboSecondaryLang, CB_RESETCONTENT, 0, 0);

	// "(自动)" = show all
	SendMessageW(m_hwndComboPrimaryLang, CB_ADDSTRING, 0, (LPARAM)L"(\x81EA\x52A8)");
	SendMessageW(m_hwndComboPrimaryLang, CB_SETITEMDATA, 0, 0);
	SendMessageW(m_hwndComboSecondaryLang, CB_ADDSTRING, 0, (LPARAM)L"(\x81EA\x52A8)");
	SendMessageW(m_hwndComboSecondaryLang, CB_SETITEMDATA, 0, 0);

	for (size_t i = 0; i < m_vecPrimaryLangs.size(); ++i)
	{
		std::wstring display = LangIdToDisplayName(m_vecPrimaryLangs[i]);
		int idx = (int)SendMessageW(m_hwndComboPrimaryLang, CB_ADDSTRING,
			0, (LPARAM)display.c_str());
		SendMessageW(m_hwndComboPrimaryLang, CB_SETITEMDATA,
			idx, (LPARAM)m_vecPrimaryLangs[i]);
	}

	for (size_t i = 0; i < m_vecSecondaryLangs.size(); ++i)
	{
		std::wstring display = LangIdToDisplayName(m_vecSecondaryLangs[i]);
		int idx = (int)SendMessageW(m_hwndComboSecondaryLang, CB_ADDSTRING,
			0, (LPARAM)display.c_str());
		SendMessageW(m_hwndComboSecondaryLang, CB_SETITEMDATA,
			idx, (LPARAM)m_vecSecondaryLangs[i]);
	}

	// Auto-select if single language
	if (m_vecPrimaryLangs.size() == 1)
		SendMessageW(m_hwndComboPrimaryLang, CB_SETCURSEL, 1, 0);
	else
		SendMessageW(m_hwndComboPrimaryLang, CB_SETCURSEL, 0, 0);

	if (m_vecSecondaryLangs.size() == 1)
		SendMessageW(m_hwndComboSecondaryLang, CB_SETCURSEL, 1, 0);
	else
		SendMessageW(m_hwndComboSecondaryLang, CB_SETCURSEL, 0, 0);
}

LANGID CRcCompareViewer::GetSelectedPrimaryLang() const
{
	if (!m_hwndComboPrimaryLang) return 0;
	int sel = (int)SendMessageW(m_hwndComboPrimaryLang, CB_GETCURSEL, 0, 0);
	if (sel < 0) return 0;
	return (LANGID)SendMessageW(m_hwndComboPrimaryLang, CB_GETITEMDATA, sel, 0);
}

LANGID CRcCompareViewer::GetSelectedSecondaryLang() const
{
	if (!m_hwndComboSecondaryLang) return 0;
	int sel = (int)SendMessageW(m_hwndComboSecondaryLang, CB_GETCURSEL, 0, 0);
	if (sel < 0) return 0;
	return (LANGID)SendMessageW(m_hwndComboSecondaryLang, CB_GETITEMDATA, sel, 0);
}

void CRcCompareViewer::AutoDetectAndSetLanguages()
{
	m_vecPrimaryLangs.clear();
	m_vecSecondaryLangs.clear();

	if (!m_pControler)
	{
		PopulateLanguageCombos();
		return;
	}

	const EntrySet* pPrimary = m_pControler->GetPrimaryEntrySet();
	const EntrySet* pSecondary = m_pControler->GetSecondaryEntrySet();

	if (pPrimary)
		m_vecPrimaryLangs = CRcCompareControler::DetectLanguages(pPrimary);
	if (pSecondary)
		m_vecSecondaryLangs = CRcCompareControler::DetectLanguages(pSecondary);

	PopulateLanguageCombos();

	if (m_vecPrimaryLangs.size() == 1)
		m_pControler->SetPrimaryLanguage(m_vecPrimaryLangs[0]);
	if (m_vecSecondaryLangs.size() == 1)
		m_pControler->SetSecondaryLanguage(m_vecSecondaryLangs[0]);
}

void CRcCompareViewer::OnComboPrimaryLangSelChange()
{
	LANGID lang = GetSelectedPrimaryLang();
	if (m_pControler && lang != 0)
	{
		m_pControler->SetPrimaryLanguage(lang);
		m_pControler->DoCompare();
		RefreshAddedItemsList();
		UpdateProgressDisplay();
	}
	BuildResourceTree();
}

void CRcCompareViewer::OnComboSecondaryLangSelChange()
{
	LANGID lang = GetSelectedSecondaryLang();
	if (m_pControler && lang != 0)
	{
		m_pControler->SetSecondaryLanguage(lang);
		m_pControler->DoCompare();
		RefreshAddedItemsList();
		UpdateProgressDisplay();
	}
	BuildResourceTree();
}

void CRcCompareViewer::OnChkShowPrimary()
{
	m_bShowPrimary = (SendMessageW(m_hwndChkShowPrimary, BM_GETCHECK, 0, 0) == BST_CHECKED);
	BuildResourceTree();
	m_viewRenderer.Clear();
}

// ============================================================================
// Navigation from compare result to tree (delegated to NavigationTree)
// ============================================================================

void CRcCompareViewer::NavigateTreeToCompareItem(const SCompareItem& item)
{
	m_navTree.NavigateToCompareItem(item);
}
