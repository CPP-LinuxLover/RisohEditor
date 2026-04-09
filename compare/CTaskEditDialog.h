#pragma once

// CTaskEditDialog.h --- Task configuration edit dialog (Win32 native)

#include "MWindowBase.hpp"
#include "CompareProjectConfig.h"
#include <vector>

namespace rceditor {

class CTaskEditDialog : public MDialogBase
{
public:
	explicit CTaskEditDialog(SCompareTaskConfig& task, HWND hParent = NULL);
	virtual ~CTaskEditDialog();

	INT_PTR DoEditDialog();

	const SCompareTaskConfig& GetTask() const { return m_task; }

protected:
	virtual INT_PTR CALLBACK
	DialogProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	BOOL OnInitDialog(HWND hwnd);
	void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
	void OnSize(HWND hwnd, int cx, int cy);

	void OnBrowsePrimaryDll();
	void OnBrowsePrimaryRc();
	void OnBrowsePrimaryH();
	void OnBrowseSecondaryDll();
	void OnBrowseSecondaryRc();
	void OnBrowseSecondaryH();
	void OnBrowseOutputDir();

private:
	// Control IDs
	enum ECtrlID
	{
		IDC_BASE = 6000,

		// Primary lang
		IDC_STATIC_PRIMARY_GROUP,
		IDC_STATIC_PRI_DLL,
		IDC_EDIT_PRI_DLL,
		IDC_BTN_PRI_DLL,
		IDC_STATIC_PRI_RC,
		IDC_EDIT_PRI_RC,
		IDC_BTN_PRI_RC,
		IDC_STATIC_PRI_H,
		IDC_EDIT_PRI_H,
		IDC_BTN_PRI_H,

		// Secondary lang
		IDC_STATIC_SEC_GROUP,
		IDC_STATIC_SEC_DLL,
		IDC_EDIT_SEC_DLL,
		IDC_BTN_SEC_DLL,
		IDC_STATIC_SEC_RC,
		IDC_EDIT_SEC_RC,
		IDC_BTN_SEC_RC,
		IDC_STATIC_SEC_H,
		IDC_EDIT_SEC_H,
		IDC_BTN_SEC_H,

		// Output
		IDC_STATIC_OUT_GROUP,
		IDC_STATIC_OUT_DIR,
		IDC_EDIT_OUT_DIR,
		IDC_BTN_OUT_DIR,
	};

	void CreateControls();
	void LayoutControls();
	void CollectData();
	void PopulateData();

	// Browse helpers using Win32 API (XP compatible)
	void BrowseForFile(HWND hwndEdit, LPCWSTR pszFilter, LPCWSTR pszTitle);
	void BrowseForFolder(HWND hwndEdit, LPCWSTR pszTitle);
	std::wstring BuildDefaultOutputDir() const;

	// Create a label + edit + browse button row
	int CreatePathRow(int y, UINT idLabel, UINT idEdit, UINT idBtn,
	                  LPCWSTR pszLabelText,
	                  HWND& hLabel, HWND& hEdit, HWND& hBtn);

	SCompareTaskConfig& m_task;
	HWND m_hParent;
	HFONT m_hFont;

	// Primary
	HWND m_hwndLblPriGroup;
	HWND m_hwndLblPriDll;   HWND m_hwndEdtPriDll;   HWND m_hwndBtnPriDll;
	HWND m_hwndLblPriRc;    HWND m_hwndEdtPriRc;    HWND m_hwndBtnPriRc;
	HWND m_hwndLblPriH;     HWND m_hwndEdtPriH;     HWND m_hwndBtnPriH;

	// Secondary
	HWND m_hwndLblSecGroup;
	HWND m_hwndLblSecDll;   HWND m_hwndEdtSecDll;   HWND m_hwndBtnSecDll;
	HWND m_hwndLblSecRc;    HWND m_hwndEdtSecRc;    HWND m_hwndBtnSecRc;
	HWND m_hwndLblSecH;     HWND m_hwndEdtSecH;     HWND m_hwndBtnSecH;

	// Output
	HWND m_hwndLblOutGroup;
	HWND m_hwndLblOutDir;   HWND m_hwndEdtOutDir;   HWND m_hwndBtnOutDir;

	HWND m_hwndBtnOk;
	HWND m_hwndBtnCancel;

	std::vector<BYTE> m_dlgTemplateBuffer;
};

} // namespace rceditor
