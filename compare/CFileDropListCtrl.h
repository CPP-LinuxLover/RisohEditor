#pragma once

// CFileDropListCtrl.h --- File drop list control (Win32 native)
// Subclasses a WC_LISTVIEW control to support drag-and-drop of PE files.

#include "MWindowBase.hpp"
#include <commctrl.h>
#include <vector>
#include <string>
#include <functional>

class CFileDropListCtrl : public MWindowBase
{
public:
	struct SFileItem
	{
		std::wstring wsFileName;       // File name (no path)
		std::wstring wsFilePath;       // Full file path
		DWORD_PTR dwData;

		SFileItem() : dwData(0) {}
	};

	using ListChangedCallback = std::function<void()>;

	CFileDropListCtrl();
	virtual ~CFileDropListCtrl();

	// Initialize columns and enable drag-drop
	void InitColumns(LPCWSTR pszIndexHeader = L"#",
	                 LPCWSTR pszFileNameHeader = L"FileName",
	                 LPCWSTR pszFilePathHeader = L"Path");

	void RemoveAllFiles();
	int AddFile(LPCWSTR pszFilePath, DWORD_PTR dwData = 0);
	bool RemoveFile(int nIndex);
	int RemoveSelectedFiles();
	int BrowseAndAddFiles();

	bool GetFileItem(int nIndex, SFileItem& outItem) const;
	void GetAllFilePaths(std::vector<std::wstring>& vecPaths) const;
	void SetListChangedCallback(ListChangedCallback callback) { m_pfnListChanged = callback; }
	int GetFileCount() const { return (int)m_vecItems.size(); }
	void SetFileFilter(LPCWSTR pszFilter) { m_wsFileFilter = pszFilter ? pszFilter : L""; }
	void SetAllowDuplicates(bool bAllow) { m_bAllowDuplicates = bAllow; }

	// Subclass an existing ListView control by HWND
	void SubclassListView(HWND hListView);

	virtual LRESULT CALLBACK
	WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
	std::vector<SFileItem> m_vecItems;
	ListChangedCallback m_pfnListChanged;
	std::wstring m_wsFileFilter;
	bool m_bAllowDuplicates;

	enum
	{
		ID_MENU_ADD = 40001,
		ID_MENU_DELETE,
		ID_MENU_DELETE_ALL,
	};

	void OnDropFiles(HDROP hDropInfo);
	void OnContextMenu(HWND hwnd, int x, int y);
	void OnKeyDown(UINT nChar);
	void UpdateRowIndices();
	void AdjustColumnWidths();
	bool IsFileExists(LPCWSTR pszFilePath) const;
	static std::wstring ExtractFileName(const std::wstring& wsFilePath);
	static bool IsValidFileExtension(const std::wstring& wsFilePath);
};
