// CFileDropListCtrl.cpp --- File drop list control (Win32 native)
#include "CFileDropListCtrl.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <cstdio>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

CFileDropListCtrl::CFileDropListCtrl()
	: m_bAllowDuplicates(false)
	, m_wsFileFilter(L"Executable Files (*.exe;*.dll)\0*.exe;*.dll\0All Files (*.*)\0*.*\0")
{
}

CFileDropListCtrl::~CFileDropListCtrl()
{
}

void CFileDropListCtrl::SubclassListView(HWND hListView)
{
	SubclassDx(hListView);
}

void CFileDropListCtrl::InitColumns(LPCWSTR pszIndexHeader,
                                    LPCWSTR pszFileNameHeader,
                                    LPCWSTR pszFilePathHeader)
{
	HWND hwnd = m_hwnd;
	if (!hwnd)
		return;

	// Set extended styles
	DWORD dwExStyle = ListView_GetExtendedListViewStyle(hwnd);
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;
	ListView_SetExtendedListViewStyle(hwnd, dwExStyle);

	// Delete existing columns
	while (ListView_DeleteColumn(hwnd, 0)) {}

	// Add three columns: index, filename, path
	LVCOLUMNW lvc;
	ZeroMemory(&lvc, sizeof(lvc));
	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
	lvc.fmt = LVCFMT_LEFT;

	lvc.pszText = const_cast<LPWSTR>(pszIndexHeader);
	lvc.cx = 50;
	ListView_InsertColumn(hwnd, 0, &lvc);

	lvc.pszText = const_cast<LPWSTR>(pszFileNameHeader);
	lvc.cx = 150;
	ListView_InsertColumn(hwnd, 1, &lvc);

	lvc.pszText = const_cast<LPWSTR>(pszFilePathHeader);
	lvc.cx = 300;
	ListView_InsertColumn(hwnd, 2, &lvc);

	// Enable drag-drop
	DragAcceptFiles(hwnd, TRUE);

	AdjustColumnWidths();
}

void CFileDropListCtrl::RemoveAllFiles()
{
	if (m_hwnd)
		ListView_DeleteAllItems(m_hwnd);

	m_vecItems.clear();

	if (m_pfnListChanged)
		m_pfnListChanged();
}

int CFileDropListCtrl::AddFile(LPCWSTR pszFilePath, DWORD_PTR dwData)
{
	if (!pszFilePath || !pszFilePath[0])
		return -1;

	// Check if file exists
	if (!PathFileExistsW(pszFilePath))
		return -1;

	// Check duplicates
	if (!m_bAllowDuplicates && IsFileExists(pszFilePath))
		return -1;

	SFileItem item;
	item.wsFilePath = pszFilePath;
	item.wsFileName = ExtractFileName(pszFilePath);
	item.dwData = dwData;

	int nIndex = (m_hwnd) ? ListView_GetItemCount(m_hwnd) : (int)m_vecItems.size();

	if (m_hwnd)
	{
		// Insert row
		WCHAR szIndex[16];
		wsprintfW(szIndex, L"%d", nIndex + 1);

		LVITEMW lvi;
		ZeroMemory(&lvi, sizeof(lvi));
		lvi.mask = LVIF_TEXT | LVIF_PARAM;
		lvi.iItem = nIndex;
		lvi.pszText = szIndex;
		lvi.lParam = (LPARAM)m_vecItems.size();
		ListView_InsertItem(m_hwnd, &lvi);

		ListView_SetItemText(m_hwnd, nIndex, 1,
			const_cast<LPWSTR>(item.wsFileName.c_str()));
		ListView_SetItemText(m_hwnd, nIndex, 2,
			const_cast<LPWSTR>(item.wsFilePath.c_str()));
	}

	m_vecItems.push_back(item);

	if (m_pfnListChanged)
		m_pfnListChanged();

	return nIndex;
}

bool CFileDropListCtrl::RemoveFile(int nIndex)
{
	if (!m_hwnd)
		return false;

	if (nIndex < 0 || nIndex >= ListView_GetItemCount(m_hwnd))
		return false;

	LVITEMW lvi;
	ZeroMemory(&lvi, sizeof(lvi));
	lvi.mask = LVIF_PARAM;
	lvi.iItem = nIndex;
	ListView_GetItem(m_hwnd, &lvi);

	size_t idx = (size_t)lvi.lParam;
	if (idx >= m_vecItems.size())
		return false;

	m_vecItems.erase(m_vecItems.begin() + idx);
	ListView_DeleteItem(m_hwnd, nIndex);

	UpdateRowIndices();

	if (m_pfnListChanged)
		m_pfnListChanged();

	return true;
}

int CFileDropListCtrl::RemoveSelectedFiles()
{
	if (!m_hwnd)
		return 0;

	int nDeleted = 0;

	// Delete from back to front to avoid index shift
	for (int i = ListView_GetItemCount(m_hwnd) - 1; i >= 0; --i)
	{
		if (ListView_GetItemState(m_hwnd, i, LVIS_SELECTED) & LVIS_SELECTED)
		{
			LVITEMW lvi;
			ZeroMemory(&lvi, sizeof(lvi));
			lvi.mask = LVIF_PARAM;
			lvi.iItem = i;
			ListView_GetItem(m_hwnd, &lvi);

			size_t idx = (size_t)lvi.lParam;
			if (idx < m_vecItems.size())
			{
				m_vecItems.erase(m_vecItems.begin() + idx);
				ListView_DeleteItem(m_hwnd, i);
				++nDeleted;
			}
		}
	}

	if (nDeleted > 0)
	{
		UpdateRowIndices();

		if (m_pfnListChanged)
			m_pfnListChanged();
	}

	return nDeleted;
}

int CFileDropListCtrl::BrowseAndAddFiles()
{
	// Use GetOpenFileNameW (XP compatible)
	const int BUFFER_SIZE = 65535;
	WCHAR* pszBuffer = new WCHAR[BUFFER_SIZE];
	ZeroMemory(pszBuffer, BUFFER_SIZE * sizeof(WCHAR));

	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd ? GetParent(m_hwnd) : NULL;
	ofn.lpstrFile = pszBuffer;
	ofn.nMaxFile = BUFFER_SIZE;
	ofn.lpstrFilter = m_wsFileFilter.c_str();
	ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	int nAdded = 0;

	if (GetOpenFileNameW(&ofn))
	{
		LPCWSTR pDir = pszBuffer;
		LPCWSTR pFile = pszBuffer + wcslen(pszBuffer) + 1;

		if (*pFile == L'\0')
		{
			// Single file selected
			if (AddFile(pDir) >= 0)
				++nAdded;
		}
		else
		{
			// Multiple files: first string is directory, rest are file names
			std::wstring wsDir = pDir;
			while (*pFile != L'\0')
			{
				std::wstring wsFullPath = wsDir + L"\\" + pFile;
				if (AddFile(wsFullPath.c_str()) >= 0)
					++nAdded;

				pFile += wcslen(pFile) + 1;
			}
		}
	}

	delete[] pszBuffer;
	return nAdded;
}

bool CFileDropListCtrl::GetFileItem(int nIndex, SFileItem& outItem) const
{
	if (!m_hwnd)
		return false;

	if (nIndex < 0 || nIndex >= ListView_GetItemCount(m_hwnd))
		return false;

	LVITEMW lvi;
	ZeroMemory(&lvi, sizeof(lvi));
	lvi.mask = LVIF_PARAM;
	lvi.iItem = nIndex;
	ListView_GetItem(m_hwnd, &lvi);

	size_t idx = (size_t)lvi.lParam;
	if (idx >= m_vecItems.size())
		return false;

	outItem = m_vecItems[idx];
	return true;
}

void CFileDropListCtrl::GetAllFilePaths(std::vector<std::wstring>& vecPaths) const
{
	vecPaths.clear();
	vecPaths.reserve(m_vecItems.size());

	for (const auto& item : m_vecItems)
	{
		vecPaths.push_back(item.wsFilePath);
	}
}

void CFileDropListCtrl::OnDropFiles(HDROP hDropInfo)
{
	UINT nFileCount = DragQueryFileW(hDropInfo, 0xFFFFFFFF, NULL, 0);

	for (UINT i = 0; i < nFileCount; ++i)
	{
		WCHAR szFilePath[MAX_PATH] = { 0 };
		DragQueryFileW(hDropInfo, i, szFilePath, MAX_PATH);

		// Skip directories
		if (PathIsDirectoryW(szFilePath))
			continue;

		AddFile(szFilePath);
	}

	DragFinish(hDropInfo);
}

void CFileDropListCtrl::OnContextMenu(HWND hwnd, int x, int y)
{
	HMENU hMenu = CreatePopupMenu();
	if (!hMenu)
		return;

	AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD, L"Add Files(&A)...");
	AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

	// Enable delete only if items are selected
	int nSelCount = 0;
	if (m_hwnd)
	{
		int nItem = -1;
		while ((nItem = ListView_GetNextItem(m_hwnd, nItem, LVNI_SELECTED)) >= 0)
			++nSelCount;
	}

	UINT nDeleteFlags = MF_STRING;
	if (nSelCount == 0)
		nDeleteFlags |= MF_GRAYED;
	AppendMenuW(hMenu, nDeleteFlags, ID_MENU_DELETE, L"Delete Selected(&D)");

	// Enable clear all only if list is non-empty
	UINT nDeleteAllFlags = MF_STRING;
	if (m_hwnd && ListView_GetItemCount(m_hwnd) == 0)
		nDeleteAllFlags |= MF_GRAYED;
	AppendMenuW(hMenu, nDeleteAllFlags, ID_MENU_DELETE_ALL, L"Clear All(&C)");

	// If position is invalid, use control center
	if (x == -1 && y == -1)
	{
		RECT rc;
		if (m_hwnd)
		{
			GetClientRect(m_hwnd, &rc);
			POINT pt;
			pt.x = (rc.left + rc.right) / 2;
			pt.y = (rc.top + rc.bottom) / 2;
			ClientToScreen(m_hwnd, &pt);
			x = pt.x;
			y = pt.y;
		}
	}

	int nCmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		x, y, 0, m_hwnd, NULL);

	DestroyMenu(hMenu);

	switch (nCmd)
	{
	case ID_MENU_ADD:
		BrowseAndAddFiles();
		break;
	case ID_MENU_DELETE:
		RemoveSelectedFiles();
		break;
	case ID_MENU_DELETE_ALL:
		RemoveAllFiles();
		break;
	}
}

void CFileDropListCtrl::OnKeyDown(UINT nChar)
{
	if (nChar == VK_DELETE)
	{
		RemoveSelectedFiles();
	}
}

void CFileDropListCtrl::UpdateRowIndices()
{
	if (!m_hwnd)
		return;

	int nCount = ListView_GetItemCount(m_hwnd);
	int nItemCount = (int)m_vecItems.size();
	int nMin = (nCount < nItemCount) ? nCount : nItemCount;

	for (int i = 0; i < nMin; ++i)
	{
		WCHAR szIndex[16];
		wsprintfW(szIndex, L"%d", i + 1);
		ListView_SetItemText(m_hwnd, i, 0, szIndex);

		// Update lParam to correct vector index
		LVITEMW lvi;
		ZeroMemory(&lvi, sizeof(lvi));
		lvi.mask = LVIF_PARAM;
		lvi.iItem = i;
		lvi.lParam = (LPARAM)i;
		ListView_SetItem(m_hwnd, &lvi);
	}
}

void CFileDropListCtrl::AdjustColumnWidths()
{
	if (!m_hwnd)
		return;

	RECT rc;
	GetClientRect(m_hwnd, &rc);

	int totalWidth = rc.right - rc.left;
	if (totalWidth <= 0)
		return;

	// Subtract scrollbar width
	int scrollWidth = GetSystemMetrics(SM_CXVSCROLL);
	totalWidth -= scrollWidth;

	// Index column fixed, filename and path proportional
	int indexWidth = 50;
	int remainingWidth = totalWidth - indexWidth;
	int fileNameWidth = remainingWidth * 30 / 100;
	int pathWidth = remainingWidth - fileNameWidth;

	ListView_SetColumnWidth(m_hwnd, 0, indexWidth);
	ListView_SetColumnWidth(m_hwnd, 1, fileNameWidth);
	ListView_SetColumnWidth(m_hwnd, 2, pathWidth);
}

bool CFileDropListCtrl::IsFileExists(LPCWSTR pszFilePath) const
{
	for (const auto& item : m_vecItems)
	{
		if (_wcsicmp(item.wsFilePath.c_str(), pszFilePath) == 0)
			return true;
	}
	return false;
}

std::wstring CFileDropListCtrl::ExtractFileName(const std::wstring& wsFilePath)
{
	size_t nPos = wsFilePath.rfind(L'\\');
	if (nPos == std::wstring::npos)
		nPos = wsFilePath.rfind(L'/');

	if (nPos != std::wstring::npos)
		return wsFilePath.substr(nPos + 1);

	return wsFilePath;
}

bool CFileDropListCtrl::IsValidFileExtension(const std::wstring& wsFilePath)
{
	LPCWSTR pszExt = PathFindExtensionW(wsFilePath.c_str());
	if (!pszExt)
		return false;

	return (_wcsicmp(pszExt, L".exe") == 0 || _wcsicmp(pszExt, L".dll") == 0);
}

LRESULT CALLBACK
CFileDropListCtrl::WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DROPFILES:
		OnDropFiles((HDROP)wParam);
		return 0;

	case WM_CONTEXTMENU:
		OnContextMenu((HWND)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));
		return 0;

	case WM_KEYDOWN:
		OnKeyDown((UINT)wParam);
		break;

	case WM_SIZE:
		AdjustColumnWidths();
		break;
	}

	return DefaultProcDx();
}
