// CConflictResolveDialog.cpp --- ID conflict resolution dialog (Win32 native)

#include "CConflictResolveDialog.h"
#include <commctrl.h>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")

CConflictResolveDialog::CConflictResolveDialog(
	const std::vector<SConflictInfo>& conflicts, HWND hParent)
	: m_selectedIndex(-1)
	, m_hFont(NULL)
	, m_hParent(hParent)
	, m_hwndListConflicts(NULL)
	, m_hwndStaticDetail(NULL)
	, m_hwndComboResolution(NULL)
	, m_hwndEditManualValue(NULL)
	, m_hwndBtnKeepAll(NULL)
	, m_hwndBtnPrimaryAll(NULL)
	, m_hwndBtnAutoAll(NULL)
	, m_hwndStaticStats(NULL)
{
	m_entries.reserve(conflicts.size());
	for (const auto& c : conflicts)
	{
		SConflictResolutionEntry entry;
		entry.conflict = c;
		entry.resolution = CR_KEEP_SECONDARY;
		entry.nManualValue = 0;
		m_entries.push_back(entry);
	}
}

CConflictResolveDialog::~CConflictResolveDialog()
{
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = NULL;
	}
}

INT_PTR CConflictResolveDialog::DoResolveDialog()
{
	// Build a runtime dialog template
	WORD buf[512];
	ZeroMemory(buf, sizeof(buf));
	DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buf;
	pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
	              DS_MODALFRAME | DS_CENTER | WS_VISIBLE;
	pDlg->dwExtendedStyle = 0;
	pDlg->cdit = 0;
	pDlg->x = 0; pDlg->y = 0;
	pDlg->cx = 350; pDlg->cy = 250;

	// Menu, class, title (all empty)
	WORD* pw = (WORD*)(pDlg + 1);
	*pw++ = 0; // no menu
	*pw++ = 0; // default class
	// Title
	LPCWSTR pszTitle = L"\x89E3\x51B3\x51B2\x7A81";
	while (*pszTitle)
		*pw++ = *pszTitle++;
	*pw++ = 0;

	return DialogBoxIndirectDx(m_hParent, pDlg);
}

INT_PTR CALLBACK
CConflictResolveDialog::DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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

	case WM_NOTIFY:
		return OnNotify(hwnd, (int)wParam, (NMHDR*)lParam);

	case WM_CLOSE:
		EndDialog(hwnd, IDCANCEL);
		return 0;
	}
	return DefaultProcDx();
}

BOOL CConflictResolveDialog::OnInitDialog(HWND hwnd)
{
	m_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	if (!m_hFont)
		m_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	CreateControls();
	PopulateList();

	// Resize and center
	SetWindowPos(hwnd, NULL, 0, 0, 700, 500, SWP_NOMOVE | SWP_NOZORDER);

	// Center on parent
	RECT rcParent, rcSelf;
	if (m_hParent)
		GetWindowRect(m_hParent, &rcParent);
	else
	{
		rcParent.left = 0; rcParent.top = 0;
		rcParent.right = GetSystemMetrics(SM_CXSCREEN);
		rcParent.bottom = GetSystemMetrics(SM_CYSCREEN);
	}
	GetWindowRect(hwnd, &rcSelf);
	int cx = rcSelf.right - rcSelf.left;
	int cy = rcSelf.bottom - rcSelf.top;
	int x = (rcParent.left + rcParent.right - cx) / 2;
	int y = (rcParent.top + rcParent.bottom - cy) / 2;
	SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

	// Stats text
	WCHAR szStats[64];
	wsprintfW(szStats, L"\x51B2\x7A81\x603B\x6570: %d", (int)m_entries.size());
	SetWindowTextW(m_hwndStaticStats, szStats);

	LayoutControls();
	return TRUE;
}

void CConflictResolveDialog::OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDC_BTN_KEEP_ALL:
		OnBtnKeepAll();
		break;
	case IDC_BTN_PRIMARY_ALL:
		OnBtnUsePrimaryAll();
		break;
	case IDC_BTN_AUTO_ALL:
		OnBtnAutoAll();
		break;
	case IDC_COMBO_RESOLUTION:
		if (codeNotify == CBN_SELCHANGE)
			OnComboResolutionChanged();
		break;
	case IDOK:
		// Collect manual value before closing
		if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_entries.size())
		{
			if (m_entries[m_selectedIndex].resolution == CR_MANUAL)
			{
				WCHAR szVal[64] = { 0 };
				GetWindowTextW(m_hwndEditManualValue, szVal, 64);
				m_entries[m_selectedIndex].nManualValue = (UINT)_wtoi(szVal);
			}
		}
		EndDialog(hwnd, IDOK);
		break;
	case IDCANCEL:
		EndDialog(hwnd, IDCANCEL);
		break;
	}
}

LRESULT CConflictResolveDialog::OnNotify(HWND hwnd, int idFrom, NMHDR* pNMHDR)
{
	if (idFrom == IDC_LIST_CONFLICTS && pNMHDR->code == LVN_ITEMCHANGED)
	{
		LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
		if (pNMLV->uNewState & LVIS_SELECTED)
			UpdateDetailPanel(pNMLV->iItem);
		return 0;
	}
	return 0;
}

void CConflictResolveDialog::OnSize(HWND hwnd, int cx, int cy)
{
	LayoutControls();
}

// ============================================================================
// Control creation
// ============================================================================

void CConflictResolveDialog::CreateControls()
{
	HWND hwnd = m_hwnd;
	HINSTANCE hInst = GetModuleHandle(NULL);
	int y = 10;

	// Stats label
	m_hwndStaticStats = CreateWindowExW(0, L"STATIC", L"",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		10, y, 400, 20, hwnd, (HMENU)(UINT_PTR)IDC_STATIC_STATS, hInst, NULL);
	SendMessageW(m_hwndStaticStats, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	y += 24;

	// Batch buttons
	m_hwndBtnKeepAll = CreateWindowExW(0, L"BUTTON", L"\x4FDD\x7559\x6240\x6709\x6B21\x8BED\x8A00",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		10, y, 150, 26, hwnd, (HMENU)(UINT_PTR)IDC_BTN_KEEP_ALL, hInst, NULL);
	SendMessageW(m_hwndBtnKeepAll, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndBtnPrimaryAll = CreateWindowExW(0, L"BUTTON", L"\x4F7F\x7528\x6240\x6709\x4E3B\x8BED\x8A00",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		170, y, 140, 26, hwnd, (HMENU)(UINT_PTR)IDC_BTN_PRIMARY_ALL, hInst, NULL);
	SendMessageW(m_hwndBtnPrimaryAll, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hwndBtnAutoAll = CreateWindowExW(0, L"BUTTON", L"\x81EA\x52A8\x91CD\x65B0\x5206\x914D",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		320, y, 150, 26, hwnd, (HMENU)(UINT_PTR)IDC_BTN_AUTO_ALL, hInst, NULL);
	SendMessageW(m_hwndBtnAutoAll, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	y += 32;

	// Conflict list (ListView)
	m_hwndListConflicts = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
		WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		10, y, 660, 250, hwnd, (HMENU)(UINT_PTR)IDC_LIST_CONFLICTS, hInst, NULL);
	SendMessageW(m_hwndListConflicts, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	ListView_SetExtendedListViewStyle(m_hwndListConflicts,
		LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

	LVCOLUMNW lvc;
	ZeroMemory(&lvc, sizeof(lvc));
	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
	lvc.fmt = LVCFMT_LEFT;

	lvc.pszText = const_cast<LPWSTR>(L"#"); lvc.cx = 30;
	ListView_InsertColumn(m_hwndListConflicts, 0, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x7C7B\x578B"); lvc.cx = 100;
	ListView_InsertColumn(m_hwndListConflicts, 1, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x4E3B\x8BED\x8A00"); lvc.cx = 140;
	ListView_InsertColumn(m_hwndListConflicts, 2, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x6B21\x8BED\x8A00"); lvc.cx = 140;
	ListView_InsertColumn(m_hwndListConflicts, 3, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x89E3\x51B3\x65B9\x6848"); lvc.cx = 120;
	ListView_InsertColumn(m_hwndListConflicts, 4, &lvc);
	y += 256;

	// Detail panel
	m_hwndStaticDetail = CreateWindowExW(0, L"STATIC", L"\x9009\x62E9\x4E00\x4E2A\x51B2\x7A81\x67E5\x770B\x8BE6\x60C5",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		10, y, 400, 20, hwnd, (HMENU)(UINT_PTR)IDC_STATIC_DETAIL, hInst, NULL);
	SendMessageW(m_hwndStaticDetail, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	y += 24;

	// Resolution combo box
	m_hwndComboResolution = CreateWindowExW(0, L"COMBOBOX", NULL,
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
		10, y, 190, 200, hwnd, (HMENU)(UINT_PTR)IDC_COMBO_RESOLUTION, hInst, NULL);
	SendMessageW(m_hwndComboResolution, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	SendMessageW(m_hwndComboResolution, CB_ADDSTRING, 0, (LPARAM)L"\x4FDD\x7559\x6B21\x8BED\x8A00");
	SendMessageW(m_hwndComboResolution, CB_ADDSTRING, 0, (LPARAM)L"\x4F7F\x7528\x4E3B\x8BED\x8A00");
	SendMessageW(m_hwndComboResolution, CB_ADDSTRING, 0, (LPARAM)L"\x81EA\x52A8\x91CD\x5206\x914D");
	SendMessageW(m_hwndComboResolution, CB_ADDSTRING, 0, (LPARAM)L"\x624B\x52A8\x8F93\x5165");
	EnableWindow(m_hwndComboResolution, FALSE);

	// Manual value edit
	m_hwndEditManualValue = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
		210, y, 100, 24, hwnd, (HMENU)(UINT_PTR)IDC_EDIT_MANUAL_VALUE, hInst, NULL);
	SendMessageW(m_hwndEditManualValue, WM_SETFONT, (WPARAM)m_hFont, TRUE);
	EnableWindow(m_hwndEditManualValue, FALSE);
}

void CConflictResolveDialog::LayoutControls()
{
	if (!m_hwndListConflicts)
		return;

	RECT rc;
	GetClientRect(m_hwnd, &rc);

	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;

	// Resize ListView to fill available space (keep 180px for bottom controls)
	RECT rcList;
	GetWindowRect(m_hwndListConflicts, &rcList);
	POINT pt = { rcList.left, rcList.top };
	ScreenToClient(m_hwnd, &pt);
	int listH = h - pt.y - 80;
	if (listH < 100) listH = 100;
	MoveWindow(m_hwndListConflicts, pt.x, pt.y, w - 20, listH, TRUE);
}

// ============================================================================
// Data population
// ============================================================================

void CConflictResolveDialog::PopulateList()
{
	ListView_DeleteAllItems(m_hwndListConflicts);

	for (int i = 0; i < (int)m_entries.size(); ++i)
	{
		const auto& e = m_entries[i];

		WCHAR szNum[16];
		wsprintfW(szNum, L"%d", i + 1);

		LVITEMW lvi;
		ZeroMemory(&lvi, sizeof(lvi));
		lvi.mask = LVIF_TEXT;
		lvi.iItem = i;
		lvi.pszText = szNum;
		int idx = ListView_InsertItem(m_hwndListConflicts, &lvi);

		LPCWSTR pszType = L"Unknown";
		switch (e.conflict.eType)
		{
		case CT_MACRO_NAME: pszType = L"Macro Name"; break;
		case CT_ID_VALUE:   pszType = L"ID Value"; break;
		case CT_CONTROL_ID: pszType = L"Control ID"; break;
		}
		ListView_SetItemText(m_hwndListConflicts, idx, 1, const_cast<LPWSTR>(pszType));

		WCHAR szPri[128];
		wsprintfW(szPri, L"%s = %u", e.conflict.wsPrimaryName.c_str(),
			e.conflict.nPrimaryValue);
		ListView_SetItemText(m_hwndListConflicts, idx, 2, szPri);

		WCHAR szSec[128];
		wsprintfW(szSec, L"%s = %u", e.conflict.wsSecondaryName.c_str(),
			e.conflict.nSecondaryValue);
		ListView_SetItemText(m_hwndListConflicts, idx, 3, szSec);

		ListView_SetItemText(m_hwndListConflicts, idx, 4,
			const_cast<LPWSTR>(L"\x4FDD\x7559\x6B21\x8BED\x8A00"));
	}
}

void CConflictResolveDialog::UpdateDetailPanel(int selectedIndex)
{
	m_selectedIndex = selectedIndex;
	if (selectedIndex < 0 || selectedIndex >= (int)m_entries.size())
	{
		SetWindowTextW(m_hwndStaticDetail, L"\x9009\x62E9\x4E00\x4E2A\x51B2\x7A81\x67E5\x770B\x8BE6\x60C5");
		EnableWindow(m_hwndComboResolution, FALSE);
		EnableWindow(m_hwndEditManualValue, FALSE);
		return;
	}

	const auto& e = m_entries[selectedIndex];
	// Build a detail string from conflict info
	WCHAR szDetail[256];
	wsprintfW(szDetail, L"Primary: %s=%u  Secondary: %s=%u  Dialog: %u",
		e.conflict.wsPrimaryName.c_str(), e.conflict.nPrimaryValue,
		e.conflict.wsSecondaryName.c_str(), e.conflict.nSecondaryValue,
		e.conflict.nDialogID);
	SetWindowTextW(m_hwndStaticDetail, szDetail);

	EnableWindow(m_hwndComboResolution, TRUE);
	SendMessageW(m_hwndComboResolution, CB_SETCURSEL, (WPARAM)e.resolution, 0);

	BOOL bManual = (e.resolution == CR_MANUAL);
	EnableWindow(m_hwndEditManualValue, bManual);
	if (bManual)
	{
		WCHAR szVal[32];
		wsprintfW(szVal, L"%u", e.nManualValue);
		SetWindowTextW(m_hwndEditManualValue, szVal);
	}
}

// ============================================================================
// Event handlers
// ============================================================================

void CConflictResolveDialog::OnComboResolutionChanged()
{
	if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_entries.size())
		return;

	int sel = (int)SendMessageW(m_hwndComboResolution, CB_GETCURSEL, 0, 0);
	if (sel < 0) return;

	m_entries[m_selectedIndex].resolution = (EConflictResolution)sel;

	static const LPCWSTR resNames[] = {
		L"\x4FDD\x7559\x6B21\x8BED\x8A00", L"\x4F7F\x7528\x4E3B\x8BED\x8A00", L"\x81EA\x52A8\x91CD\x5206\x914D", L"\x624B\x52A8\x8F93\x5165"
	};
	ListView_SetItemText(m_hwndListConflicts, m_selectedIndex, 4,
		const_cast<LPWSTR>(resNames[sel]));

	BOOL bManual = (sel == (int)CR_MANUAL);
	EnableWindow(m_hwndEditManualValue, bManual);
	if (bManual)
	{
		WCHAR szVal[32];
		wsprintfW(szVal, L"%u", m_entries[m_selectedIndex].nManualValue);
		SetWindowTextW(m_hwndEditManualValue, szVal);
	}
}

void CConflictResolveDialog::OnBtnKeepAll()
{
	ApplyResolutionToAll(CR_KEEP_SECONDARY);
}

void CConflictResolveDialog::OnBtnUsePrimaryAll()
{
	ApplyResolutionToAll(CR_USE_PRIMARY);
}

void CConflictResolveDialog::OnBtnAutoAll()
{
	ApplyResolutionToAll(CR_AUTO_REASSIGN);
}

void CConflictResolveDialog::ApplyResolutionToAll(EConflictResolution res)
{
	static const LPCWSTR resNames[] = {
		L"\x4FDD\x7559\x6B21\x8BED\x8A00", L"\x4F7F\x7528\x4E3B\x8BED\x8A00", L"\x81EA\x52A8\x91CD\x5206\x914D", L"\x624B\x52A8\x8F93\x5165"
	};
	for (int i = 0; i < (int)m_entries.size(); ++i)
	{
		m_entries[i].resolution = res;
		ListView_SetItemText(m_hwndListConflicts, i, 4,
			const_cast<LPWSTR>(resNames[(int)res]));
	}

	if (m_selectedIndex >= 0)
	{
		SendMessageW(m_hwndComboResolution, CB_SETCURSEL, (WPARAM)res, 0);
		EnableWindow(m_hwndEditManualValue, res == CR_MANUAL);
	}
}
