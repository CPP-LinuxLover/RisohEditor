// CFormRcCompare.cpp --- PE resource compare main window (Win32 native)

#include "CFormRcCompare.h"
#include "CTaskEditDialog.h"
#include "CRcCompareViewer.h"
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

CFormRcCompare::CFormRcCompare(HWND hParent /*= NULL*/)
	: m_bInitialized(false)
	, m_bProcessing(false)
	, m_hFont(NULL)
	, m_hParent(hParent)
	, m_hwndProjectGroup(NULL)
	, m_hwndListTasks(NULL)
	, m_hwndBtnLoadProject(NULL)
	, m_hwndBtnSaveProject(NULL)
	, m_hwndBtnAddTask(NULL)
	, m_hwndBtnEditTask(NULL)
	, m_hwndBtnRemoveTask(NULL)
	, m_hwndBtnBatchCompare(NULL)
	, m_hwndBtnBatchExport(NULL)
	, m_hwndStaticStatus(NULL)
	, m_hwndProgressBar(NULL)
	, m_hwndBtnOpenViewer(NULL)
{
}

CFormRcCompare::~CFormRcCompare()
{
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = NULL;
	}
}

INT_PTR CFormRcCompare::DoCompareDialog()
{
	// Create a dialog template at runtime
	RECT rc = { 0, 0, 800, 500 };
	DWORD dwStyle = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU |
	                WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	DLGTEMPLATE* pTemplate = CreateTemplate(dwStyle, rc,
	                                        GetInitialCaption(), WS_EX_APPWINDOW);
	if (!pTemplate)
		return -1;

	return DialogBoxIndirectDx(m_hParent, pTemplate);
}

INT_PTR CALLBACK
CFormRcCompare::DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		return OnInitDialog(hwnd);

	case WM_SIZE:
		OnSize(hwnd, LOWORD(lParam), HIWORD(lParam));
		return 0;

	case WM_GETMINMAXINFO:
		OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam));
		return 0;

	case WM_ERASEBKGND:
		{
			HDC hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hwnd, &rc);
			HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
			FillRect(hdc, &rc, hBr);
			DeleteObject(hBr);
		}
		return TRUE;

	case WM_COMMAND:
		OnCommand(hwnd, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
		return 0;

	case WM_CLOSE:
		OnBeforeDialogClose();
		EndDialog(hwnd, IDCANCEL);
		return 0;

	default:
		// Worker thread custom messages
		if (uMsg == rceditor::WM_COMPARE_PROGRESS)
			return OnCompareProgress(wParam, lParam);
		if (uMsg == rceditor::WM_COMPARE_TASK_DONE)
			return OnCompareTaskDone(wParam, lParam);
		if (uMsg == rceditor::WM_COMPARE_ALL_DONE)
			return OnCompareAllDone(wParam, lParam);
		if (uMsg == rceditor::WM_COMPARE_ERROR)
			return OnCompareError(wParam, lParam);
		if (uMsg == rceditor::WM_COMPARE_STATUS)
			return OnCompareStatus(wParam, lParam);
		break;
	}

	return DefaultProcDx();
}

BOOL CFormRcCompare::OnInitDialog(HWND hwnd)
{
	// Create font
	m_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Microsoft YaHei UI");
	if (!m_hFont)
	{
		m_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	}

	CreateProjectControls();
	LayoutProjectControls();

	// Center window
	RECT rcParent, rcSelf;
	if (m_hParent)
		GetWindowRect(m_hParent, &rcParent);
	else
	{
		rcParent.left = 0;
		rcParent.top = 0;
		rcParent.right = GetSystemMetrics(SM_CXSCREEN);
		rcParent.bottom = GetSystemMetrics(SM_CYSCREEN);
	}
	GetWindowRect(hwnd, &rcSelf);
	int cx = rcSelf.right - rcSelf.left;
	int cy = rcSelf.bottom - rcSelf.top;
	int x = (rcParent.left + rcParent.right - cx) / 2;
	int y = (rcParent.top + rcParent.bottom - cy) / 2;
	SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

	m_bInitialized = true;
	return TRUE;
}

void CFormRcCompare::OnBeforeDialogClose()
{
	m_controler.ClearAll();
}

void CFormRcCompare::OnSize(HWND hwnd, int cx, int cy)
{
	if (m_bInitialized)
	{
		LayoutProjectControls();
		InvalidateRect(hwnd, NULL, TRUE);
	}
}

void CFormRcCompare::OnGetMinMaxInfo(MINMAXINFO* pMMI)
{
	pMMI->ptMinTrackSize.x = 700;
	pMMI->ptMinTrackSize.y = 400;
}

void CFormRcCompare::OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDC_BTN_LOAD_PROJECT:  OnBtnLoadProject(); break;
	case IDC_BTN_SAVE_PROJECT:  OnBtnSaveProject(); break;
	case IDC_BTN_ADD_TASK:      OnBtnAddTask(); break;
	case IDC_BTN_EDIT_TASK:     OnBtnEditTask(); break;
	case IDC_BTN_REMOVE_TASK:   OnBtnRemoveTask(); break;
	case IDC_BTN_BATCH_COMPARE: OnBtnBatchCompare(); break;
	case IDC_BTN_BATCH_EXPORT:  OnBtnBatchExport(); break;
	case IDC_BTN_OPEN_VIEWER:   OnBtnOpenViewer(); break;
	case IDCANCEL:
		OnBeforeDialogClose();
		EndDialog(hwnd, IDCANCEL);
		break;
	}
}

// ============================================================================
// Control creation and layout
// ============================================================================

void CFormRcCompare::CreateProjectControls()
{
	HWND hwnd = m_hwnd;
	HINSTANCE hInst = GetModuleHandle(NULL);

	// Title label
	m_hwndProjectGroup = CreateWindowExW(0, L"STATIC", L"\x6BD4\x5BF9\x4EFB\x52A1\x5217\x8868",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 200, 20, hwnd, (HMENU)(UINT_PTR)IDC_STATIC_PROJECT_GROUP, hInst, NULL);
	SendMessageW(m_hwndProjectGroup, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Task list (ListView)
	m_hwndListTasks = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
		WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 100, 100, hwnd, (HMENU)(UINT_PTR)IDC_LIST_TASKS, hInst, NULL);
	ListView_SetExtendedListViewStyle(m_hwndListTasks, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

	LVCOLUMNW lvc;
	ZeroMemory(&lvc, sizeof(lvc));
	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
	lvc.fmt = LVCFMT_LEFT;
	lvc.pszText = const_cast<LPWSTR>(L"#"); lvc.cx = 30;
	ListView_InsertColumn(m_hwndListTasks, 0, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x4E3B\x8BED\x8A00"); lvc.cx = 150;
	ListView_InsertColumn(m_hwndListTasks, 1, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x6B21\x8BED\x8A00"); lvc.cx = 150;
	ListView_InsertColumn(m_hwndListTasks, 2, &lvc);
	lvc.pszText = const_cast<LPWSTR>(L"\x72B6\x6001"); lvc.cx = 80;
	ListView_InsertColumn(m_hwndListTasks, 3, &lvc);
	SendMessageW(m_hwndListTasks, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Buttons
	auto CreateBtn = [&](LPCWSTR text, int id, int w, int h) -> HWND
	{
		HWND hBtn = CreateWindowExW(0, L"BUTTON", text,
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			0, 0, w, h, hwnd, (HMENU)(UINT_PTR)id, hInst, NULL);
		SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
		return hBtn;
	};

	m_hwndBtnLoadProject = CreateBtn(L"\x52A0\x8F7D\x9879\x76EE", IDC_BTN_LOAD_PROJECT, 100, 28);
	m_hwndBtnSaveProject = CreateBtn(L"\x4FDD\x5B58\x9879\x76EE", IDC_BTN_SAVE_PROJECT, 100, 28);
	m_hwndBtnAddTask     = CreateBtn(L"\x6DFB\x52A0\x4EFB\x52A1", IDC_BTN_ADD_TASK, 90, 28);
	m_hwndBtnEditTask    = CreateBtn(L"\x7F16\x8F91", IDC_BTN_EDIT_TASK, 60, 28);
	m_hwndBtnRemoveTask  = CreateBtn(L"\x5220\x9664", IDC_BTN_REMOVE_TASK, 70, 28);
	m_hwndBtnBatchCompare = CreateBtn(L"\x6279\x91CF\x6BD4\x5BF9", IDC_BTN_BATCH_COMPARE, 120, 36);
	m_hwndBtnBatchExport  = CreateBtn(L"\x6279\x91CF\x5BFC\x51FA", IDC_BTN_BATCH_EXPORT, 100, 36);
	m_hwndBtnOpenViewer   = CreateBtn(L"\x6253\x5F00\x67E5\x770B\x5668", IDC_BTN_OPEN_VIEWER, 120, 36);

	// Status label
	m_hwndStaticStatus = CreateWindowExW(0, L"STATIC", L"\x5C31\x7EEA",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		0, 0, 200, 20, hwnd, (HMENU)(UINT_PTR)IDC_STATIC_STATUS, hInst, NULL);
	SendMessageW(m_hwndStaticStatus, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// Progress bar
	m_hwndProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
		WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		0, 0, 200, 18, hwnd, (HMENU)(UINT_PTR)IDC_PROGRESS_BAR, hInst, NULL);
	SendMessageW(m_hwndProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessageW(m_hwndProgressBar, PBM_SETPOS, 0, 0);

	UpdateBatchButtonState();
}

void CFormRcCompare::LayoutProjectControls()
{
	if (!m_hwndListTasks)
		return;

	RECT rcClient;
	GetClientRect(m_hwnd, &rcClient);

	const int MARGIN = 10;
	const int TITLE_H = 22;
	const int BTN_H = 28;
	const int BTN_BATCH_H = 36;
	const int GAP = 6;
	const int STATUS_H = 20;

	const int width = rcClient.right - rcClient.left - MARGIN * 2;
	int y = MARGIN;

	// Title
	MoveWindow(m_hwndProjectGroup, MARGIN, y, width, TITLE_H, TRUE);
	y += TITLE_H + GAP;

	// Toolbar buttons row
	int bx = MARGIN;
	MoveWindow(m_hwndBtnLoadProject, bx, y, 110, BTN_H, TRUE); bx += 115;
	MoveWindow(m_hwndBtnSaveProject, bx, y, 110, BTN_H, TRUE); bx += 115;
	MoveWindow(m_hwndBtnAddTask, bx, y, 90, BTN_H, TRUE); bx += 95;
	MoveWindow(m_hwndBtnEditTask, bx, y, 70, BTN_H, TRUE); bx += 75;
	MoveWindow(m_hwndBtnRemoveTask, bx, y, 80, BTN_H, TRUE);
	y += BTN_H + GAP;

	// Bottom area (batch buttons + viewer + status + progress)
	const int bottomY = rcClient.bottom - rcClient.top - MARGIN;
	const int statusY = bottomY - STATUS_H;
	const int batchY = statusY - GAP - BTN_BATCH_H;

	MoveWindow(m_hwndBtnBatchCompare, MARGIN, batchY, 140, BTN_BATCH_H, TRUE);
	MoveWindow(m_hwndBtnBatchExport, MARGIN + 145, batchY, 120, BTN_BATCH_H, TRUE);
	MoveWindow(m_hwndBtnOpenViewer, MARGIN + 270, batchY, 140, BTN_BATCH_H, TRUE);

	int statusWidth = width / 2;
	MoveWindow(m_hwndStaticStatus, MARGIN, statusY, statusWidth, STATUS_H, TRUE);
	MoveWindow(m_hwndProgressBar, MARGIN + statusWidth + GAP, statusY,
		width - statusWidth - GAP, STATUS_H, TRUE);

	// Task list fills remaining space
	int listTop = y;
	int listBottom = batchY - GAP;
	int listH = listBottom - listTop;
	if (listH < 80)
		listH = 80;
	MoveWindow(m_hwndListTasks, MARGIN, listTop, width, listH, TRUE);

	// Auto-fit column widths
	ListView_SetColumnWidth(m_hwndListTasks, 0, 40);
	ListView_SetColumnWidth(m_hwndListTasks, 1, (width - 40 - 100) / 2);
	ListView_SetColumnWidth(m_hwndListTasks, 2, (width - 40 - 100) / 2);
	ListView_SetColumnWidth(m_hwndListTasks, 3, 100);
}

void CFormRcCompare::RefreshTaskList()
{
	ListView_DeleteAllItems(m_hwndListTasks);

	for (int i = 0; i < (int)m_project.tasks.size(); ++i)
	{
		const auto& t = m_project.tasks[i];

		WCHAR szNum[16];
		wsprintfW(szNum, L"%d", i + 1);

		LVITEMW lvi;
		ZeroMemory(&lvi, sizeof(lvi));
		lvi.mask = LVIF_TEXT;
		lvi.iItem = i;
		lvi.pszText = szNum;
		int idx = ListView_InsertItem(m_hwndListTasks, &lvi);

		// Show file name only
		std::wstring priName = t.primaryDllPath;
		size_t pos = priName.rfind(L'\\');
		if (pos != std::wstring::npos)
			priName = priName.substr(pos + 1);
		ListView_SetItemText(m_hwndListTasks, idx, 1,
			const_cast<LPWSTR>(priName.c_str()));

		std::wstring secName = t.secondaryDllPath;
		pos = secName.rfind(L'\\');
		if (pos != std::wstring::npos)
			secName = secName.substr(pos + 1);
		ListView_SetItemText(m_hwndListTasks, idx, 2,
			const_cast<LPWSTR>(secName.c_str()));

		// Status
		LPCWSTR pszStatus = L"\x5C31\x7EEA";
		if (i < (int)m_controler.GetTaskDataVec().size())
		{
			const auto* td = m_controler.GetTaskData(i);
			if (td)
			{
				if (td->bExported)        pszStatus = L"\x5DF2\x5BFC\x51FA";
				else if (td->bMerged)     pszStatus = L"\x5DF2\x5408\x5E76";
				else if (td->bLoaded)     pszStatus = L"\x5DF2\x52A0\x8F7D";
				if (!td->wsLastError.empty()) pszStatus = L"\x9519\x8BEF";
			}
		}
		ListView_SetItemText(m_hwndListTasks, idx, 3, const_cast<LPWSTR>(pszStatus));
	}

	UpdateBatchButtonState();
}

void CFormRcCompare::UpdateBatchButtonState()
{
	bool hasTasks = !m_project.tasks.empty();
	EnableWindow(m_hwndBtnBatchCompare, hasTasks && !m_bProcessing);
	EnableWindow(m_hwndBtnBatchExport, hasTasks && !m_bProcessing);
	EnableWindow(m_hwndBtnAddTask, !m_bProcessing);
	EnableWindow(m_hwndBtnEditTask, !m_bProcessing);
	EnableWindow(m_hwndBtnRemoveTask, !m_bProcessing);
	EnableWindow(m_hwndBtnLoadProject, !m_bProcessing);
	EnableWindow(m_hwndBtnSaveProject, !m_bProcessing);
}

void CFormRcCompare::SetProcessingUI(bool bProcessing)
{
	m_bProcessing = bProcessing;
	UpdateBatchButtonState();
}

// ============================================================================
// Project management event handlers
// ============================================================================

void CFormRcCompare::OnBtnLoadProject()
{
	WCHAR szFile[MAX_PATH] = { 0 };
	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"\x9879\x76EE\x6587\x4EF6 (*.json)\0*.json\0\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"\x52A0\x8F7D\x6BD4\x5BF9\x9879\x76EE";
	ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;

	if (!GetOpenFileNameW(&ofn))
		return;

	rceditor::CompareProjectConfigManager mgr;
	rceditor::SCompareProject proj;
	if (!mgr.LoadProjectConfig(szFile, proj))
	{
		MessageBoxW(m_hwnd, L"\x52A0\x8F7D\x9879\x76EE\x6587\x4EF6\x5931\x8D25\x3002", L"\x9519\x8BEF", MB_ICONERROR);
		return;
	}

	m_project = proj;
	m_controler.SetProject(m_project);
	RefreshTaskList();
	SetWindowTextW(m_hwndStaticStatus, L"\x9879\x76EE\x5DF2\x52A0\x8F7D\x3002");
}

void CFormRcCompare::OnBtnSaveProject()
{
	WCHAR szFile[MAX_PATH] = { 0 };
	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"\x9879\x76EE\x6587\x4EF6 (*.json)\0*.json\0\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"\x4FDD\x5B58\x6BD4\x5BF9\x9879\x76EE";
	ofn.lpstrDefExt = L"json";
	ofn.Flags = OFN_OVERWRITEPROMPT;

	if (!GetSaveFileNameW(&ofn))
		return;

	rceditor::CompareProjectConfigManager mgr;
	if (!mgr.SaveProjectConfig(szFile, m_project))
	{
		MessageBoxW(m_hwnd, L"\x4FDD\x5B58\x9879\x76EE\x6587\x4EF6\x5931\x8D25\x3002", L"\x9519\x8BEF", MB_ICONERROR);
		return;
	}
	SetWindowTextW(m_hwndStaticStatus, L"\x9879\x76EE\x5DF2\x4FDD\x5B58\x3002");
}

void CFormRcCompare::OnBtnAddTask()
{
	rceditor::SCompareTaskConfig newTask;
	rceditor::CTaskEditDialog dlg(newTask, m_hwnd);
	if (dlg.DoEditDialog() == IDOK)
	{
		m_project.tasks.push_back(newTask);
		m_controler.SetProject(m_project);
		RefreshTaskList();
	}
}

void CFormRcCompare::OnBtnEditTask()
{
	int sel = ListView_GetNextItem(m_hwndListTasks, -1, LVNI_SELECTED);
	if (sel < 0 || sel >= (int)m_project.tasks.size())
	{
		MessageBoxW(m_hwnd, L"\x8BF7\x9009\x62E9\x8981\x7F16\x8F91\x7684\x4EFB\x52A1\x3002", L"\x63D0\x793A", MB_ICONINFORMATION);
		return;
	}

	rceditor::CTaskEditDialog dlg(m_project.tasks[sel], m_hwnd);
	if (dlg.DoEditDialog() == IDOK)
	{
		m_controler.SetProject(m_project);
		RefreshTaskList();
	}
}

void CFormRcCompare::OnBtnRemoveTask()
{
	int sel = ListView_GetNextItem(m_hwndListTasks, -1, LVNI_SELECTED);
	if (sel < 0 || sel >= (int)m_project.tasks.size())
	{
		MessageBoxW(m_hwnd, L"\x8BF7\x9009\x62E9\x8981\x5220\x9664\x7684\x4EFB\x52A1\x3002", L"\x63D0\x793A", MB_ICONINFORMATION);
		return;
	}

	m_project.tasks.erase(m_project.tasks.begin() + sel);
	m_controler.SetProject(m_project);
	RefreshTaskList();
}

void CFormRcCompare::OnBtnBatchCompare()
{
	if (m_project.tasks.empty())
		return;

	// Pre-check: verify files are accessible
	for (int i = 0; i < (int)m_project.tasks.size(); ++i)
	{
		std::wstring wsLockedFile;
		if (!rceditor::CCompareWorkerThread::CheckTaskFilesAccessible(
				m_project.tasks[i], wsLockedFile))
		{
			WCHAR szMsg[1024];
			wsprintfW(szMsg,
				L"\x4EFB\x52A1 %d (%s): \x6587\x4EF6\x88AB\x5176\x4ED6\x8FDB\x7A0B\x9501\x5B9A:\n%s\n\n"
				L"\x8BF7\x5173\x95ED\x4F7F\x7528\x6B64\x6587\x4EF6\x7684\x7A0B\x5E8F\x540E\x91CD\x8BD5\x3002",
				i + 1, m_project.tasks[i].taskName.c_str(), wsLockedFile.c_str());
			MessageBoxW(m_hwnd, szMsg, L"\x9519\x8BEF", MB_ICONERROR);
			return;
		}
	}

	// Languages will be auto-detected by the viewer per task
	// Clear and set up controller with project
	m_controler.ClearAll();
	m_controler.SetProject(m_project);
	m_controler.GetTaskDataVec().resize(m_project.tasks.size());

	SetProcessingUI(true);
	SendMessageW(m_hwndProgressBar, PBM_SETPOS, 0, 0);
	SetWindowTextW(m_hwndStaticStatus, L"\x6279\x91CF\x6BD4\x5BF9\x4E2D...");

	// Build worker function
	rceditor::TaskWorkerFunc workerFunc = [this](int taskIndex,
		const rceditor::SCompareTaskConfig& task,
		const std::atomic<bool>& cancelFlag,
		rceditor::ProgressCallback progressCb)
	{
		m_controler.ProcessSingleTask(taskIndex, task, cancelFlag, progressCb);
	};

	// Enable concurrent loading for multiple tasks
	m_worker.SetConcurrentLoading(m_project.tasks.size() > 1);
	m_worker.Start(m_hwnd, m_project.tasks, workerFunc);
}

void CFormRcCompare::OnBtnBatchExport()
{
	if (m_project.tasks.empty())
		return;

	// Validate all tasks before export
	std::wstring wsAllWarnings;
	int warningTaskCount = 0;
	for (int i = 0; i < (int)m_project.tasks.size(); ++i)
	{
		std::vector<std::wstring> taskWarnings;
		m_controler.ValidateBeforeExport(i, taskWarnings);
		if (!taskWarnings.empty())
		{
			++warningTaskCount;
			WCHAR szHeader[128];
			wsprintfW(szHeader, L"\nTask %d (%s):\n", i + 1,
				m_project.tasks[i].taskName.c_str());
			wsAllWarnings += szHeader;
			for (const auto& w : taskWarnings)
			{
				wsAllWarnings += L"  - ";
				wsAllWarnings += w;
				wsAllWarnings += L"\n";
			}
		}
	}

	// Show warnings and confirm
	if (!wsAllWarnings.empty())
	{
		std::wstring wsConfirm;
		WCHAR szFmt[128];
		wsprintfW(szFmt, L"\x5728 %d \x4E2A\x4EFB\x52A1\x4E2D\x53D1\x73B0\x9A8C\x8BC1\x8B66\x544A:\n",
			warningTaskCount);
		wsConfirm = szFmt;
		wsConfirm += wsAllWarnings;
		wsConfirm += L"\n\x7EE7\x7EED\x5BFC\x51FA\xFF1F";
		if (MessageBoxW(m_hwnd, wsConfirm.c_str(), L"\x8B66\x544A",
				MB_YESNO | MB_ICONWARNING) != IDYES)
			return;
	}

	int exported = 0;
	int failed = 0;
	std::wstring wsFirstError;
	for (int i = 0; i < (int)m_project.tasks.size(); ++i)
	{
		if (m_controler.ExportTask(i))
		{
			++exported;
		}
		else
		{
			++failed;
			const auto* td = m_controler.GetTaskData(i);
			if (td && !td->wsLastError.empty() && wsFirstError.empty())
				wsFirstError = td->wsLastError;
		}
	}

	WCHAR szMsg[512];
	if (failed > 0 && !wsFirstError.empty())
		wsprintfW(szMsg, L"\x5BFC\x51FA %d / %d \x4E2A\x4EFB\x52A1\xFF0C%d \x4E2A\x5931\x8D25\x3002\n\x9996\x4E2A\x9519\x8BEF: %s",
			exported, (int)m_project.tasks.size(), failed, wsFirstError.c_str());
	else
		wsprintfW(szMsg, L"\x5BFC\x51FA %d / %d \x4E2A\x4EFB\x52A1\x3002", exported, (int)m_project.tasks.size());
	MessageBoxW(m_hwnd, szMsg, L"\x5BFC\x51FA\x7ED3\x679C", MB_ICONINFORMATION);
	RefreshTaskList();
}

// ============================================================================
// Worker thread message handlers
// ============================================================================

LRESULT CFormRcCompare::OnCompareProgress(WPARAM wParam, LPARAM lParam)
{
	int taskIndex = (int)wParam;
	int progress = (int)lParam;

	int total = (int)m_project.tasks.size();
	if (total > 0)
	{
		int overall = (taskIndex * 100 + progress) / total;
		SendMessageW(m_hwndProgressBar, PBM_SETPOS, overall, 0);
	}
	return 0;
}

LRESULT CFormRcCompare::OnCompareTaskDone(WPARAM wParam, LPARAM lParam)
{
	int taskIndex = (int)wParam;
	m_controler.MergeTask(taskIndex);

	WCHAR szStatus[128];
	wsprintfW(szStatus, L"\x4EFB\x52A1 %d/%d \x5B8C\x6210\x3002", taskIndex + 1, (int)m_project.tasks.size());
	SetWindowTextW(m_hwndStaticStatus, szStatus);

	RefreshTaskList();
	return 0;
}

LRESULT CFormRcCompare::OnCompareAllDone(WPARAM wParam, LPARAM lParam)
{
	SetProcessingUI(false);
	SendMessageW(m_hwndProgressBar, PBM_SETPOS, 100, 0);

	int total = m_controler.GetResultCount();
	WCHAR szMsg[256];
	wsprintfW(szMsg, L"\x6279\x91CF\x6BD4\x5BF9\x5B8C\x6210\x3002\x53D1\x73B0 %d \x5904\x5DEE\x5F02\x3002", total);
	SetWindowTextW(m_hwndStaticStatus, szMsg);

	if (total > 0)
	{
		std::wstring wsQuestion = szMsg;
		wsQuestion += L"\n\x6253\x5F00\x67E5\x770B\x5668\xFF1F";
		if (MessageBoxW(m_hwnd, wsQuestion.c_str(), L"\x6BD4\x5BF9\x5B8C\x6210",
				MB_YESNO | MB_ICONQUESTION) == IDYES)
		{
			// TODO: Open CRcCompareViewer dialog
		}
	}
	else
	{
		MessageBoxW(m_hwnd, L"\x672A\x53D1\x73B0\x5DEE\x5F02\x3002", L"\x6BD4\x5BF9\x5B8C\x6210",
			MB_ICONINFORMATION);
	}

	RefreshTaskList();
	return 0;
}

LRESULT CFormRcCompare::OnCompareError(WPARAM wParam, LPARAM lParam)
{
	std::wstring* pMsg = reinterpret_cast<std::wstring*>(lParam);
	if (pMsg)
	{
		SetWindowTextW(m_hwndStaticStatus, pMsg->c_str());
		delete pMsg;
	}
	return 0;
}

LRESULT CFormRcCompare::OnCompareStatus(WPARAM wParam, LPARAM lParam)
{
	std::wstring* pMsg = reinterpret_cast<std::wstring*>(lParam);
	if (pMsg)
	{
		SetWindowTextW(m_hwndStaticStatus, pMsg->c_str());
		delete pMsg;
	}
	return 0;
}

// ============================================================================
// Designer and language toggle
// ============================================================================

void CFormRcCompare::OnBtnOpenViewer()
{
	// Get selected task
	int sel = ListView_GetNextItem(m_hwndListTasks, -1, LVNI_SELECTED);
	if (sel < 0 || sel >= (int)m_project.tasks.size())
	{
		MessageBoxW(m_hwnd, L"\x8BF7\x5148\x9009\x62E9\x4E00\x4E2A\x4EFB\x52A1\x3002",
			L"\x63D0\x793A", MB_ICONINFORMATION);
		return;
	}

	auto* pData = m_controler.GetTaskData(sel);
	if (!pData || !pData->bLoaded)
	{
		MessageBoxW(m_hwnd, L"\x8BF7\x5148\x6267\x884C\x6279\x91CF\x6BD4\x5BF9\x3002",
			L"\x63D0\x793A", MB_ICONINFORMATION);
		return;
	}

	// Open the viewer window for the selected task
	m_controler.SetCurrentTaskIndex(sel);
	CRcCompareViewer viewer(&m_controler, m_hwnd);
	viewer.DoViewerWindow();
}
